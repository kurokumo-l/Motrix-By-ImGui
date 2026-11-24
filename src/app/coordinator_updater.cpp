module motrix.app;

import std;
import motrix.core;
import motrix.domain;
import motrix.engine;
import motrix.platform;
import :engine_runtime;

namespace motrix::app {

namespace {

[[nodiscard]] EngineUpdateStatus MapPhaseToStatus(motrix::engine::Aria2NextUpdatePhase phase) noexcept {
    switch (phase) {
        case motrix::engine::Aria2NextUpdatePhase::Checking: return EngineUpdateStatus::Checking;
        case motrix::engine::Aria2NextUpdatePhase::Downloading: return EngineUpdateStatus::Downloading;
        case motrix::engine::Aria2NextUpdatePhase::Verifying: return EngineUpdateStatus::Verifying;
    }
    return EngineUpdateStatus::Checking;
}

[[nodiscard]] bool IsUpdateInProgress(EngineUpdateStatus st) noexcept {
    return st == EngineUpdateStatus::Checking || st == EngineUpdateStatus::Downloading
           || st == EngineUpdateStatus::Verifying || st == EngineUpdateStatus::Restarting;
}

}  // namespace

void AppCoordinator::StartAria2NextUpdateAsync(bool forceReinstall) {
    std::jthread finishedWorker;
    {
        std::scoped_lock lk(workerMutex_);
        if (stopping_.load(std::memory_order_acquire) || engineUpdateRunning_) {
            return;
        }
        engineUpdateRunning_ = true;
        finishedWorker = std::move(engineUpdateWorker_);
    }
    if (finishedWorker.joinable()) {
        finishedWorker.join();
    }

    const auto initialSnap = Snapshot();
    if (IsUpdateInProgress(initialSnap->engineUpdateStatus)) {
        std::scoped_lock lk(workerMutex_);
        engineUpdateRunning_ = false;
        return;
    }

    const std::filesystem::path dataDir =
        appDataDir_.empty() ? (std::filesystem::temp_directory_path() / "MotrixNativeEngine")
                            : appDataDir_;
    const std::string srcUrl = initialSnap->prefs.aria2NextUpdateUrl.empty()
                                   ? std::string(motrix::domain::kDefaultAria2NextUpdateUrl)
                                   : initialSnap->prefs.aria2NextUpdateUrl;

    motrix::engine::Aria2NextUpdateSpec spec{
        .updateUrl = srcUrl,
        .mirrorPrefix = initialSnap->prefs.aria2NextMirrorPrefix,
        .proxyUrl = initialSnap->prefs.useProxy ? initialSnap->sysConfig.allProxy : std::string{},
        .targetExePath = motrix::engine::ManagedAria2NextExecutablePath(dataDir),
        .currentVersion = initialSnap->engineInfo.version,
        .currentKind = initialSnap->engineInfo.kind,
        .timeout = std::chrono::milliseconds(30000),
        .forceReinstall = forceReinstall,
    };

    PublishMutated([](AppStateSnapshot& s) {
        s.engineUpdateStatus = EngineUpdateStatus::Checking;
        s.engineUpdateMessage.clear();
        s.engineUpdateVersion.clear();
    });

    std::scoped_lock lk(workerMutex_);
    if (stopping_.load(std::memory_order_acquire)) {
        engineUpdateRunning_ = false;
        return;
    }
    engineUpdateWorker_ = std::jthread([this, s = std::move(spec)](std::stop_token st) mutable {
        struct RunningGuard {
            AppCoordinator& self;
            ~RunningGuard() {
                std::scoped_lock lock(self.workerMutex_);
                self.engineUpdateRunning_ = false;
            }
        } runningGuard{*this};
        ExecuteAria2NextUpdate(std::move(st), std::move(s));
    });
}

void AppCoordinator::ExecuteAria2NextUpdate(std::stop_token st,
                                            motrix::engine::Aria2NextUpdateSpec spec) {
    auto onPhase = [this, &st](motrix::engine::Aria2NextUpdatePhase phase) {
        if (st.stop_requested()) {
            return;
        }
        PublishMutated([phase](AppStateSnapshot& s) { s.engineUpdateStatus = MapPhaseToStatus(phase); });
    };

    spec.stopToken = st;
    auto stageRes = motrix::engine::StageAria2NextUpdate(spec, onPhase);
    if (st.stop_requested()) {
        return;
    }
    if (!stageRes) {
        logger_.Warn("updater", "aria2-next update failed: {}", stageRes.error().message);
        PublishMutated([&](AppStateSnapshot& s) {
            s.engineUpdateStatus = EngineUpdateStatus::Failed;
            s.engineUpdateMessage = stageRes.error().message;
        });
        return;
    }
    if (stageRes->alreadyUpToDate) {
        PublishMutated([&](AppStateSnapshot& s) {
            s.engineUpdateStatus = EngineUpdateStatus::Succeeded;
            s.engineUpdateVersion = stageRes->detectedVersion.empty()
                                        ? stageRes->release.normalizedVersion
                                        : stageRes->detectedVersion;
            s.engineUpdateMessage.clear();
        });
        return;
    }

    PublishMutated([](AppStateSnapshot& s) { s.engineUpdateStatus = EngineUpdateStatus::Restarting; });
    FinishAria2NextInstall(st, spec, *stageRes);
}

void AppCoordinator::FinishAria2NextInstall(const std::stop_token& st,
                                            const motrix::engine::Aria2NextUpdateSpec& spec,
                                            const motrix::engine::Aria2NextStageOutcome& stage) {
    std::unique_ptr<EngineRuntime> stoppedRt;
    {
        std::scoped_lock lk(engineLifecycleMu_);
        hasLiveRuntime_.store(false, std::memory_order_release);
        engineReplacing_ = true;
        stoppedRt = std::move(engineRuntime_);
    }
    stoppedRt.reset();

    struct ReplacingResetGuard {
        AppCoordinator& self;
        ~ReplacingResetGuard() {
            std::scoped_lock lk(self.engineLifecycleMu_);
            if (self.engineReplacing_) {
                self.engineReplacing_ = false;
                self.deferredEngineOps_.clear();
            }
        }
    } replaceGuard{*this};

    const auto instRes = motrix::engine::InstallStagedAria2Next(stage.stagedExePath, spec.targetExePath);
    if (!instRes) {
        logger_.Warn("updater", "Failed installing aria2-next: {}", instRes.error().message);
        if (!st.stop_requested() && enableEngine_) {
            CreateAndStartEngineRuntime(*Snapshot());
        }
        PublishMutated([&](AppStateSnapshot& s) {
            s.engineUpdateStatus = EngineUpdateStatus::Failed;
            s.engineUpdateMessage = instRes.error().message;
        });
        return;
    }

    const std::string installedUtf8 = motrix::core::PathToUtf8(spec.targetExePath);
    bool hasCustomCliOverride = false;
    {
        std::scoped_lock lk(engineLifecycleMu_);
        if (!enginePathOverride_.empty()) {
            const auto normPath = [](std::filesystem::path p) {
                p.make_preferred();
                return motrix::core::AsciiLower(motrix::core::PathToUtf8(p.lexically_normal()));
            };
            if (normPath(motrix::core::PathFromUtf8(enginePathOverride_))
                == normPath(spec.targetExePath)) {
                enginePathOverride_.clear();
            } else {
                hasCustomCliOverride = true;
            }
        }
    }
    PublishMutated([&](AppStateSnapshot& s) {
        s.prefs.engineBinPath = installedUtf8;
        if (!hasCustomCliOverride) {
            s.resolvedEnginePath = installedUtf8;
            s.engineError.clear();
            s.engineInfo.kind = stage.detectedKind;
            s.engineInfo.version = stage.detectedVersion;
            s.engineInfo.product = "aria2-next";
        }
        s.engineUpdateStatus = EngineUpdateStatus::Succeeded;
        s.engineUpdateVersion = stage.detectedVersion;
        s.engineUpdateMessage = std::format("v{}", stage.detectedVersion);
    });
    if (persistConfig_) {
        PersistConfigs(*Snapshot(), true, false, true);
    }
    if (!st.stop_requested() && enableEngine_) {
        CreateAndStartEngineRuntime(*Snapshot());
    }
}

}  // namespace motrix::app

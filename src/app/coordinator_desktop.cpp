module motrix.app;

import std;
import motrix.core;
import motrix.domain;
import motrix.engine;
import motrix.platform;
import :engine_runtime;

namespace motrix::app {

namespace {

[[nodiscard]] bool ShouldTreatAsTaskbarActive(const motrix::domain::DownloadTaskView& t) noexcept {
    return t.status == motrix::domain::TaskStatus::Active && !t.isSeeding;
}

}  // namespace

void AppCoordinator::HandleEngineDetailUpdate(std::optional<motrix::engine::TaskDetailResult> detail,
                                              std::vector<motrix::domain::TaskPeerView> peers) {
    PublishMutated([&](AppStateSnapshot& state) {
        if (state.detailedTaskGid.empty()) {
            state.detailedTask.reset();
            state.detailedTrackers.clear();
            state.detailedPeers.clear();
            return;
        }
        if (detail && detail->task.gid == state.detailedTaskGid) {
            state.detailedTask = std::move(detail->task);
            state.detailedTrackers = std::move(detail->trackers);
            state.detailedPeers = std::move(peers);
        }
    });
}

void AppCoordinator::HandleEngineSyncUpdate(const EngineSyncUpdate& update) {
    PublishMutated([&](AppStateSnapshot& state) {
        state.engineState = update.connState;
        state.engineError = std::string(update.errorMsg);
        if (!update.enginePath.empty()) {
            state.resolvedEnginePath = std::string(update.enginePath);
        }
        if (update.version) {
            state.engineInfo = *update.version;
        }
        if (!update.sync) {
            return;
        }
        state.downloadingTasks = update.sync->active;
        state.waitingTasks = update.sync->waiting;
        state.stoppedTasks = update.sync->stopped;
        state.globalStat = GlobalStatSnapshot{
            .downloadSpeed = update.sync->globalStat.downloadSpeed,
            .uploadSpeed = update.sync->globalStat.uploadSpeed,
            .numActive = update.sync->globalStat.numActive,
            .numWaiting = update.sync->globalStat.numWaiting,
            .numStopped = update.sync->globalStat.numStopped,
        };
        PruneSelectionGids(state);
        if (state.detailedTaskGid.empty() || FindTaskByGid(state, state.detailedTaskGid)) {
            return;
        }
        state.detailedTaskGid.clear();
        state.detailedTask.reset();
        state.detailedTrackers.clear();
        state.detailedPeers.clear();
        std::scoped_lock lk(engineLifecycleMu_);
        if (engineRuntime_) {
            engineRuntime_->SetDetailGid({});
        }
    });
    if (update.sync) {
        const auto snap = Snapshot();
        DetectAndNotifyCompletions(*snap);
        UpdateLivePlatformStatus(*snap);
    } else if (update.connState != EngineConnectionState::Connected) {
        UpdateLivePlatformStatus(*Snapshot());
    }
}

void AppCoordinator::ApplyDesktopIntegration(const AppStateSnapshot& state) {
    const bool showTray = state.prefs.runMode != motrix::domain::RunMode::HideTray;
    const bool bindClose = showTray;

    if (auto* host = windowHost_.load(std::memory_order_acquire)) {
        host->SetTrayEnabled(showTray);
        host->SetBindCloseToHide(bindClose);
        host->SetAutoHideOnBlur(state.prefs.autoHideWindow && showTray);
        const bool isDark =
            state.prefs.theme == motrix::domain::ThemePreference::Dark
            || (state.prefs.theme == motrix::domain::ThemePreference::Auto && host->IsSystemDarkMode());
        host->SetDarkFrame(isDark);
    }

    if (!enableOsIntegration_) {
        return;
    }
    const auto exePath = motrix::platform::CurrentExecutablePath();
    if (exePath.empty()) {
        return;
    }
    if (auto res = motrix::platform::SetOpenAtLoginEnabled(state.prefs.openAtLogin, exePath); !res) {
        logger_.Warn("desktop", "Failed updating openAtLogin registry: {}", res.error().message);
    }
    const motrix::platform::ProtocolAssociationOptions protoOpts{
        .enableMotrix = state.prefs.protocolMotrix,
        .enableMagnet = state.prefs.protocolMagnet,
        .enableThunder = state.prefs.protocolThunder,
        .enableTorrent = state.prefs.protocolTorrent,
    };
    if (auto res = motrix::platform::ApplyProtocolAssociations(exePath, protoOpts); !res) {
        logger_.Warn("desktop", "Failed updating protocol associations: {}", res.error().message);
    }
}

void AppCoordinator::UpdateLivePlatformStatus(const AppStateSnapshot& state) {
    std::scoped_lock statusLk(platformStatusMu_);
    auto* host = windowHost_.load(std::memory_order_acquire);
    if (!host) {
        return;
    }

    const bool connected = state.engineState == EngineConnectionState::Connected;
    std::uint64_t totalBytes = 0;
    std::uint64_t completedBytes = 0;
    int activeTransferCount = 0;
    if (connected) {
        for (const auto& t : state.downloadingTasks) {
            if (!ShouldTreatAsTaskbarActive(t)) {
                continue;
            }
            ++activeTransferCount;
            if (t.totalLength > 0) {
                totalBytes += t.totalLength;
                completedBytes += std::min(t.completedLength, t.totalLength);
            } else if (t.mediaProgress >= 0.0f) {
                constexpr std::uint64_t kMediaWeight = 1'000'000ull;
                totalBytes += kMediaWeight;
                const float clamped = std::clamp(t.mediaProgress, 0.0f, 1.0f);
                completedBytes += static_cast<std::uint64_t>(clamped * static_cast<float>(kMediaWeight));
            }
        }
    }

    const bool hasActive = connected && (activeTransferCount > 0 || !state.downloadingTasks.empty());
    std::string trayTip;
    if (connected && state.prefs.traySpeedometer
        && (hasActive || state.globalStat.downloadSpeed > 0 || state.globalStat.uploadSpeed > 0)) {
        trayTip = std::format("Motrix  ↓ {}  ↑ {}",
                              motrix::domain::FormatSpeed(state.globalStat.downloadSpeed),
                              motrix::domain::FormatSpeed(state.globalStat.uploadSpeed));
    }
    host->UpdateTrayStatus(hasActive, state.prefs.locale == motrix::core::Locale::ZhCN, trayTip);

    const bool wantSleepBlock = connected && activeTransferCount > 0;
    if (wantSleepBlock != sleepBlockerActive_.load(std::memory_order_acquire)) {
        sleepBlockerActive_.store(wantSleepBlock, std::memory_order_release);
        host->SetSleepBlockerEnabled(wantSleepBlock);
    }

    if (!state.prefs.showProgressBar || activeTransferCount == 0) {
        host->UpdateTaskbarProgress(motrix::platform::TaskbarProgressMode::None, 0, 0);
    } else if (totalBytes == 0) {
        host->UpdateTaskbarProgress(motrix::platform::TaskbarProgressMode::Indeterminate, 0, 0);
    } else {
        host->UpdateTaskbarProgress(
            motrix::platform::TaskbarProgressMode::Normal, completedBytes, totalBytes);
    }
}

void AppCoordinator::DetectAndNotifyCompletions(const AppStateSnapshot& state) {
    const auto notices = UpdateTaskCompletionTracker(completionTracker_, state);
    if (notices.empty() || !state.prefs.taskNotification) {
        return;
    }
    auto* host = windowHost_.load(std::memory_order_acquire);
    if (!host) {
        return;
    }
    const motrix::core::Translator tr(state.prefs.locale);
    for (const auto& item : notices) {
        if (item.btSeeding) {
            host->ShowToastNotification(tr.Tr("notify.bt_complete_title"),
                                        std::format("{}\n{}", item.name, tr.Tr("notify.bt_seeding_tip")),
                                        item.dir);
        } else {
            host->ShowToastNotification(tr.Tr("notify.complete_title"), item.name, item.dir);
        }
    }
}

void AppCoordinator::StartTrackerSyncAsync(std::vector<std::string> sources, std::string proxyUrl) {
    if (sources.empty()) {
        PublishMutated([](AppStateSnapshot& s) {
            s.isSyncingTrackers = false;
            s.trackerSyncStatus = "No tracker sources selected";
        });
        return;
    }

    if (trackerSyncWorker_.joinable()) {
        trackerSyncWorker_.request_stop();
        trackerSyncWorker_.join();
    }

    PublishMutated([&](AppStateSnapshot& s) {
        s.isSyncingTrackers = true;
        s.prefs.trackerSource = sources;
        s.trackerSyncStatus.clear();
    });

    trackerSyncWorker_ =
        std::jthread([this, srcs = std::move(sources), proxy = std::move(proxyUrl)](std::stop_token st) {
            if (st.stop_requested()) {
                return;
            }
            auto fetched =
                motrix::engine::FetchTrackersFromUrls(srcs, proxy, std::chrono::milliseconds(15000), st);
            if (st.stop_requested()) {
                return;
            }
            if (!fetched) {
                logger_.Warn("tracker", "Tracker sync failed: {}", fetched.error().message);
                PublishMutated([&](AppStateSnapshot& s) {
                    s.isSyncingTrackers = false;
                    s.trackerSyncStatus = fetched.error().message;
                });
                return;
            }

            const std::string commaTrackers =
                motrix::domain::ConvertTrackerDataToComma(fetched->rawCombinedText);
            const auto nowSec = std::chrono::duration_cast<std::chrono::seconds>(
                                    std::chrono::system_clock::now().time_since_epoch())
                                    .count();

            std::vector<std::pair<std::string, std::string>> optsCopy;
            PublishMutated([&](AppStateSnapshot& s) {
                s.isSyncingTrackers = false;
                s.prefs.lastSyncTrackerTime = nowSec;
                s.prefs.trackerSource = srcs;
                s.sysConfig.btTracker = commaTrackers;
                s.trackerSyncStatus = std::format(
                    "Synced {} sources ({} bytes)", fetched->succeededSources, commaTrackers.size());
                const auto eff =
                    motrix::domain::BuildEffectiveSystemConfig(s.sysConfig, s.prefs, s.speedLimitPaused);
                optsCopy = motrix::domain::SystemConfigToOptionPairs(eff);
            });

            if (persistConfig_) {
                PersistConfigs(*Snapshot(), true, true, true);
            }
            EnqueueGlobalOptionsWork(std::move(optsCopy));
        });
}

void AppCoordinator::HandleTrayAction(motrix::platform::TrayMenuAction action) {
    using A = motrix::platform::TrayMenuAction;
    auto* host = windowHost_.load(std::memory_order_acquire);

    switch (action) {
        case A::ShowWindow:
            if (host) {
                host->ShowFromTray();
            }
            break;
        case A::NewTask:
            PublishMutated([](AppStateSnapshot& s) {
                s.activeSection = 0;
                s.activeModal = ActiveModal::AddTask;
                s.pendingDroppedTorrentPath = "__TAB_URI__";
                ++s.dropSeq;
            });
            if (host) {
                host->ShowFromTray();
            }
            break;
        case A::NewBtTask:
            PublishMutated([](AppStateSnapshot& s) {
                s.activeSection = 0;
                s.activeModal = ActiveModal::AddTask;
                s.pendingDroppedTorrentPath = "__TAB_TORRENT__";
                ++s.dropSeq;
            });
            if (host) {
                host->ShowFromTray();
            }
            break;
        case A::OpenTorrentFile:
            if (host) {
                host->ShowFromTray();
                if (auto path = host->PickTorrentFile()) {
                    Dispatch(QueueDroppedFilesCmd{{*path}});
                }
            }
            break;
        case A::ResumeAll: Dispatch(ResumeAllCmd{}); break;
        case A::PauseAll: Dispatch(PauseAllCmd{}); break;
        case A::Tasks:
            Dispatch(SelectSectionCmd{0});
            if (host) {
                host->ShowFromTray();
            }
            break;
        case A::Preferences:
            Dispatch(SelectSectionCmd{1});
            if (host) {
                host->ShowFromTray();
            }
            break;
        case A::Quit: Dispatch(QuitAppCmd{}); break;
    }
}

namespace {

struct CollectedStartupInput {
    std::string firstTorrent;
    std::string joinedUris;
    std::optional<motrix::domain::DeepLinkAction> pendingAction;
};

[[nodiscard]] CollectedStartupInput CollectStartupInput(const HandleStartupPayloadCmd& cmd) {
    CollectedStartupInput out;
    std::vector<std::string> collectedUris = cmd.uris;
    if (!cmd.torrentFiles.empty()) {
        out.firstTorrent = cmd.torrentFiles.front();
    }
    for (const auto& link : cmd.deepLinks) {
        const auto parsed = motrix::domain::ParseMotrixDeepLink(link);
        if (parsed.action == motrix::domain::DeepLinkAction::None) {
            continue;
        }
        if (!parsed.uriParam.empty()) {
            collectedUris.push_back(parsed.uriParam);
        }
        out.pendingAction = parsed.action;
    }
    for (const auto& raw : collectedUris) {
        const auto classified = motrix::domain::ClassifyStartupArg(raw);
        const std::string& val = !classified.uri.empty() ? classified.uri : raw;
        if (!out.joinedUris.empty()) {
            out.joinedUris.push_back('\n');
        }
        out.joinedUris += val;
    }
    return out;
}

void ApplyDeepLinkAction(AppStateSnapshot& s, motrix::domain::DeepLinkAction action) {
    switch (action) {
        case motrix::domain::DeepLinkAction::TaskList:
            s.activeSection = 0;
            s.activeModal = ActiveModal::None;
            break;
        case motrix::domain::DeepLinkAction::NewTask:
            s.activeSection = 0;
            s.pendingDroppedTorrentPath = "__TAB_URI__";
            ++s.dropSeq;
            s.activeModal = ActiveModal::AddTask;
            break;
        case motrix::domain::DeepLinkAction::NewBtTask:
            s.activeSection = 0;
            s.pendingDroppedTorrentPath = "__TAB_TORRENT__";
            ++s.dropSeq;
            s.activeModal = ActiveModal::AddTask;
            break;
        case motrix::domain::DeepLinkAction::Preferences:
            s.activeSection = 1;
            s.activeModal = ActiveModal::None;
            break;
        case motrix::domain::DeepLinkAction::About: s.activeModal = ActiveModal::About; break;
        case motrix::domain::DeepLinkAction::PauseAll:
        case motrix::domain::DeepLinkAction::ResumeAll:
        case motrix::domain::DeepLinkAction::None: break;
    }
}

}  // namespace

void AppCoordinator::HandleStartupPayload(const HandleStartupPayloadCmd& cmd) {
    const CollectedStartupInput input = CollectStartupInput(cmd);

    PublishMutated([&](AppStateSnapshot& s) {
        if (!input.firstTorrent.empty()) {
            s.pendingDroppedTorrentPath = input.firstTorrent;
            ++s.dropSeq;
            s.activeModal = ActiveModal::AddTask;
            s.activeSection = 0;
        }
        if (!input.joinedUris.empty()) {
            s.pendingPrefillUri = input.joinedUris;
            ++s.prefillUriSeq;
            s.activeModal = ActiveModal::AddTask;
            s.activeSection = 0;
        }
        if (input.firstTorrent.empty() && input.joinedUris.empty() && input.pendingAction) {
            ApplyDeepLinkAction(s, *input.pendingAction);
        }
    });

    if (input.pendingAction == motrix::domain::DeepLinkAction::PauseAll) {
        Dispatch(PauseAllCmd{});
    } else if (input.pendingAction == motrix::domain::DeepLinkAction::ResumeAll) {
        Dispatch(ResumeAllCmd{});
    }

    const bool silentBulkAction =
        input.firstTorrent.empty() && input.joinedUris.empty()
        && (input.pendingAction == motrix::domain::DeepLinkAction::PauseAll
            || input.pendingAction == motrix::domain::DeepLinkAction::ResumeAll);
    if (cmd.activateWindow && !silentBulkAction) {
        if (auto* host = windowHost_.load(std::memory_order_acquire)) {
            host->ShowFromTray();
        }
    }
}

}  // namespace motrix::app

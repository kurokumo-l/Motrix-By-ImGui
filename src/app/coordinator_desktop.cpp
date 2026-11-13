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

void AppCoordinator::HandleEngineSyncUpdate(EngineConnectionState connState,
                                            const motrix::engine::EngineVersionInfo* version,
                                            const motrix::engine::EngineSyncSnapshot* sync,
                                            std::string_view enginePath,
                                            std::string_view errorMsg) {
    PublishMutated([&](AppStateSnapshot& state) {
        state.engineState = connState;
        state.engineError = std::string(errorMsg);
        if (!enginePath.empty()) {
            state.resolvedEnginePath = std::string(enginePath);
        }
        if (version) {
            state.engineInfo = *version;
        }
        if (sync) {
            state.downloadingTasks = sync->active;
            state.waitingTasks = sync->waiting;
            state.stoppedTasks = sync->stopped;
            state.globalStat = GlobalStatSnapshot{
                .downloadSpeed = sync->globalStat.downloadSpeed,
                .uploadSpeed = sync->globalStat.uploadSpeed,
                .numActive = sync->globalStat.numActive,
                .numWaiting = sync->globalStat.numWaiting,
                .numStopped = sync->globalStat.numStopped,
            };
            if (!state.detailedTaskGid.empty() && !FindTaskByGid(state, state.detailedTaskGid)) {
                state.detailedTaskGid.clear();
                state.detailedTask.reset();
                state.detailedTrackers.clear();
                state.detailedPeers.clear();
                if (engineRuntime_) {
                    engineRuntime_->SetDetailGid({});
                }
            }
        }
    });
    if (sync) {
        const auto snap = Snapshot();
        DetectAndNotifyCompletions(*snap);
        UpdateLivePlatformStatus(*snap);
    }
}

void AppCoordinator::ApplyDesktopIntegration(const AppStateSnapshot& state) {
    const bool showTray = state.prefs.runMode != motrix::domain::RunMode::HideTray;
    const bool bindClose = showTray;

    if (auto* host = windowHost_.load(std::memory_order_acquire)) {
        host->SetTrayEnabled(showTray);
        host->SetBindCloseToHide(bindClose);
        host->SetAutoHideOnBlur(state.prefs.autoHideWindow && showTray);
        host->SetDarkFrame(state.prefs.theme == motrix::domain::ThemePreference::Dark);
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
    if (auto res = motrix::platform::ApplyProtocolAssociations(exePath,
                                                               state.prefs.protocolMotrix,
                                                               state.prefs.protocolMagnet,
                                                               state.prefs.protocolThunder);
        !res) {
        logger_.Warn("desktop", "Failed updating protocol associations: {}", res.error().message);
    }
}

void AppCoordinator::UpdateLivePlatformStatus(const AppStateSnapshot& state) {
    auto* host = windowHost_.load(std::memory_order_acquire);
    if (!host) {
        return;
    }

    std::uint64_t totalBytes = 0;
    std::uint64_t completedBytes = 0;
    int activeTransferCount = 0;
    for (const auto& t : state.downloadingTasks) {
        if (ShouldTreatAsTaskbarActive(t)) {
            ++activeTransferCount;
            totalBytes += t.totalLength;
            completedBytes += t.completedLength;
        }
    }

    const bool hasActive = activeTransferCount > 0 || !state.downloadingTasks.empty();
    host->UpdateTrayStatus(hasActive, state.prefs.locale == motrix::core::Locale::ZhCN);

    const bool wantSleepBlock = activeTransferCount > 0;
    if (wantSleepBlock != sleepBlockerActive_) {
        sleepBlockerActive_ = wantSleepBlock;
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
    struct ToastItem {
        std::string title;
        std::string body;
        std::string dir;
    };
    std::vector<ToastItem> pendingToasts;
    const motrix::core::Translator tr(state.prefs.locale);

    auto collectList = [&](const std::vector<motrix::domain::DownloadTaskView>& list, bool activeGroup) {
        for (const auto& t : list) {
            const bool completedOrSeeding =
                (t.status == motrix::domain::TaskStatus::Complete)
                || (activeGroup && t.taskType == motrix::domain::TaskType::BitTorrent && t.isSeeding);
            const motrix::domain::TaskStatus effectiveStatus =
                completedOrSeeding ? motrix::domain::TaskStatus::Complete : t.status;

            auto it = lastTaskStatuses_.find(t.gid);
            if (!initialSyncSeen_) {
                lastTaskStatuses_[t.gid] = effectiveStatus;
                continue;
            }
            const bool newlyDone =
                completedOrSeeding
                && (it == lastTaskStatuses_.end() || it->second != motrix::domain::TaskStatus::Complete);
            lastTaskStatuses_[t.gid] = effectiveStatus;

            if (newlyDone && state.prefs.taskNotification) {
                if (t.taskType == motrix::domain::TaskType::BitTorrent && t.isSeeding) {
                    pendingToasts.push_back(ToastItem{
                        .title = std::string(tr.Tr("notify.bt_complete_title")),
                        .body = std::format("{}\n{}", t.name, tr.Tr("notify.bt_seeding_tip")),
                        .dir = t.dir,
                    });
                } else {
                    pendingToasts.push_back(ToastItem{
                        .title = std::string(tr.Tr("notify.complete_title")),
                        .body = t.name,
                        .dir = t.dir,
                    });
                }
            }
        }
    };

    collectList(state.downloadingTasks, true);
    collectList(state.waitingTasks, false);
    collectList(state.stoppedTasks, false);
    initialSyncSeen_ = true;

    if (auto* host = windowHost_.load(std::memory_order_acquire)) {
        for (const auto& toast : pendingToasts) {
            host->ShowToastNotification(toast.title, toast.body, toast.dir);
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
            auto fetched = motrix::engine::FetchTrackersFromUrls(srcs, proxy);
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
                optsCopy = motrix::domain::SystemConfigToOptionPairs(s.sysConfig);
            });

            if (persistConfig_) {
                PersistConfigs(*Snapshot(), true, true, true);
            }
            if (engineRuntime_) {
                engineRuntime_->Enqueue(EngineWorkCommand{
                    .kind = EngineWorkCommand::Kind::ApplyGlobalOptions,
                    .options = std::move(optsCopy),
                });
            }
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

void AppCoordinator::HandleStartupPayload(const HandleStartupPayloadCmd& cmd) {
    std::vector<std::string> collectedUris = cmd.uris;
    std::string firstTorrent;
    if (!cmd.torrentFiles.empty()) {
        firstTorrent = cmd.torrentFiles.front();
    }

    std::optional<motrix::domain::DeepLinkAction> pendingAction;
    for (const auto& link : cmd.deepLinks) {
        const auto parsed = motrix::domain::ParseMotrixDeepLink(link);
        if (parsed.action == motrix::domain::DeepLinkAction::None) {
            continue;
        }
        if (!parsed.uriParam.empty()) {
            collectedUris.push_back(parsed.uriParam);
        }
        pendingAction = parsed.action;
    }

    std::string joinedUris;
    for (std::size_t i = 0; i < collectedUris.size(); ++i) {
        const auto classified = motrix::domain::ClassifyStartupArg(collectedUris[i]);
        const std::string& val = !classified.uri.empty() ? classified.uri : collectedUris[i];
        if (!joinedUris.empty()) {
            joinedUris.push_back('\n');
        }
        joinedUris += val;
    }

    PublishMutated([&](AppStateSnapshot& s) {
        if (!firstTorrent.empty()) {
            s.pendingDroppedTorrentPath = firstTorrent;
            ++s.dropSeq;
            s.activeModal = ActiveModal::AddTask;
            s.activeSection = 0;
        } else if (!joinedUris.empty()) {
            s.pendingPrefillUri = joinedUris;
            ++s.prefillUriSeq;
            s.activeModal = ActiveModal::AddTask;
            s.activeSection = 0;
        } else if (pendingAction) {
            switch (*pendingAction) {
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
    });

    if (pendingAction == motrix::domain::DeepLinkAction::PauseAll) {
        Dispatch(PauseAllCmd{});
    } else if (pendingAction == motrix::domain::DeepLinkAction::ResumeAll) {
        Dispatch(ResumeAllCmd{});
    }

    if (cmd.activateWindow) {
        if (auto* host = windowHost_.load(std::memory_order_acquire)) {
            host->ShowFromTray();
        }
    }
}

}  // namespace motrix::app

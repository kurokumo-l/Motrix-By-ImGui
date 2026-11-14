export module motrix.app:snapshot;

import std;
import motrix.domain;
import motrix.engine;
import :command;

namespace motrix::app {

export enum class EngineConnectionState {
    Starting = 0,
    Connected,
    Reconnecting,
    Offline,
    PreviewMock,
};

export struct GlobalStatSnapshot {
    std::uint64_t downloadSpeed = 0;
    std::uint64_t uploadSpeed = 0;
    int numActive = 0;
    int numWaiting = 0;
    int numStopped = 0;
};

export struct AppStateSnapshot {
    std::uint64_t revision = 0;
    motrix::domain::TaskCategory activeCategory = motrix::domain::TaskCategory::Downloading;
    int activeSection = 0;
    ActiveModal activeModal = ActiveModal::None;
    std::string selectedTaskGid;
    std::string detailedTaskGid;
    int activeDetailTab = 0;
    EngineConnectionState engineState = EngineConnectionState::Starting;
    motrix::engine::EngineVersionInfo engineInfo;
    std::string engineError;
    std::string resolvedEnginePath;
    std::string engineConfPath;
    std::string sessionFilePath;
    std::string appLogPath;
    GlobalStatSnapshot globalStat;
    motrix::domain::UserPreferences prefs = motrix::domain::DefaultUserPreferences();
    motrix::domain::SystemConfig sysConfig = motrix::domain::DefaultSystemConfig();
    std::vector<motrix::domain::DownloadTaskView> downloadingTasks;
    std::vector<motrix::domain::DownloadTaskView> waitingTasks;
    std::vector<motrix::domain::DownloadTaskView> stoppedTasks;

    std::optional<motrix::domain::DownloadTaskView> detailedTask;
    std::vector<std::string> detailedTrackers;
    std::vector<motrix::domain::TaskPeerView> detailedPeers;
    std::string pendingDroppedTorrentPath;
    std::uint64_t dropSeq = 0;
    std::string pendingPrefillUri;
    std::uint64_t prefillUriSeq = 0;
    bool isSyncingTrackers = false;
    std::string trackerSyncStatus;
};

}  // namespace motrix::app

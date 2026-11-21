export module motrix.domain:task_model;

import std;

namespace motrix::domain {

export enum class TaskStatus {
    Active = 0,
    Waiting,
    Paused,
    Complete,
    Error,
    Removed,
};

export enum class TaskCategory {
    Downloading = 0,
    Waiting,
    Stopped,
};

export enum class TaskType {
    Http = 0,
    BitTorrent,
    Metalink,
};

export struct TaskFileView {
    int index = 0;
    std::string path;
    std::uint64_t length = 0;
    std::uint64_t completedLength = 0;
    bool selected = true;
};

export struct TaskPeerView {
    std::string peerId;
    std::string client;
    std::string ip;
    std::uint16_t port = 0;
    std::string bitfieldHex;
    std::uint64_t downloadSpeed = 0;
    std::uint64_t uploadSpeed = 0;
    bool amChoking = false;
    bool peerChoking = false;
    bool seeder = false;
    float progress = 0.0f;
};

export struct TaskTrackerView {
    std::string url;
    std::string status;
};

export struct TaskBittorrentInfo {
    std::string name;
    std::string infoHash;
    std::string creationDate;
    std::string comment;
    std::int64_t creationTimestamp = 0;
};

export struct DownloadTaskView {
    std::string gid;
    std::string name;
    TaskStatus status = TaskStatus::Waiting;
    TaskType taskType = TaskType::Http;
    std::uint64_t totalLength = 0;
    std::uint64_t completedLength = 0;
    std::uint64_t uploadLength = 0;
    std::uint64_t downloadSpeed = 0;
    std::uint64_t uploadSpeed = 0;
    int connections = 0;
    int numSeeders = 0;
    bool isSeeding = false;
    std::string dir;
    std::string uri;
    std::string bitfieldHex;
    std::uint32_t numPieces = 0;
    std::uint64_t pieceLength = 0;
    float mediaProgress = -1.0f;
    int errorCode = 0;
    std::string errorMessage;
    std::size_t filesTotal = 0;
    std::vector<TaskFileView> files;
    std::vector<TaskPeerView> peers;
    std::vector<TaskTrackerView> trackers;
    std::optional<TaskBittorrentInfo> btInfo;
};

export [[nodiscard]] TaskCategory CategoryOf(TaskStatus status) noexcept;
export [[nodiscard]] float ProgressOf(const DownloadTaskView& task) noexcept;
export [[nodiscard]] std::uint64_t RemainingBytesOf(const DownloadTaskView& task) noexcept;
export [[nodiscard]] std::optional<std::uint64_t> EtaSecondsOf(const DownloadTaskView& task) noexcept;
export [[nodiscard]] std::string ResolveTaskFullPath(const DownloadTaskView& task);

}  // namespace motrix::domain

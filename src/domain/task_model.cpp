module motrix.domain;

import std;
import motrix.core;

namespace motrix::domain {

TaskCategory CategoryOf(TaskStatus status) noexcept {
    switch (status) {
        case TaskStatus::Active: return TaskCategory::Downloading;
        case TaskStatus::Waiting:
        case TaskStatus::Paused: return TaskCategory::Waiting;
        case TaskStatus::Complete:
        case TaskStatus::Error:
        case TaskStatus::Removed: return TaskCategory::Stopped;
    }
    return TaskCategory::Stopped;
}

float ProgressOf(const DownloadTaskView& task) noexcept {
    if (task.totalLength == 0) {
        return 0.0f;
    }
    if (task.completedLength >= task.totalLength) {
        return 1.0f;
    }
    return static_cast<float>(static_cast<double>(task.completedLength)
                              / static_cast<double>(task.totalLength));
}

std::uint64_t RemainingBytesOf(const DownloadTaskView& task) noexcept {
    if (task.completedLength >= task.totalLength) {
        return 0;
    }
    return task.totalLength - task.completedLength;
}

std::optional<std::uint64_t> EtaSecondsOf(const DownloadTaskView& task) noexcept {
    if (task.status != TaskStatus::Active || task.downloadSpeed == 0 || task.totalLength == 0
        || task.completedLength >= task.totalLength) {
        return std::nullopt;
    }
    const std::uint64_t remaining = task.totalLength - task.completedLength;
    return (remaining + task.downloadSpeed - 1) / task.downloadSpeed;
}

std::string ResolveTaskFullPath(const DownloadTaskView& task) {
    if (task.dir.empty()) {
        return (!task.files.empty()) ? task.files[0].path : std::string{};
    }
    const std::filesystem::path baseDir = motrix::core::PathFromUtf8(task.dir).lexically_normal();
    const bool isMagnetPending = task.taskType == TaskType::BitTorrent
                                 && (!task.btInfo || task.btInfo->name.empty()) && task.files.empty();
    if (isMagnetPending) {
        return motrix::core::PathToUtf8(baseDir);
    }
    if (task.btInfo && !task.btInfo->name.empty()) {
        std::filesystem::path full = baseDir / motrix::core::PathFromUtf8(task.btInfo->name);
        return motrix::core::PathToUtf8(full.lexically_normal());
    }
    if (!task.files.empty() && !task.files[0].path.empty()) {
        return motrix::core::PathToUtf8(
            motrix::core::PathFromUtf8(task.files[0].path).lexically_normal());
    }
    return motrix::core::PathToUtf8(baseDir);
}

}  // namespace motrix::domain

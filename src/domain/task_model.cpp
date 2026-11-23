module motrix.domain;

import std;
import motrix.core;

namespace motrix::domain {

namespace {

[[nodiscard]] bool HasPathSegmentDotDot(const std::filesystem::path& p) {
    for (const auto& part : p) {
        if (part == L"..") {
            return true;
        }
    }
    return false;
}

[[nodiscard]] bool IsLexicallyWithinBase(const std::filesystem::path& baseDir,
                                         const std::filesystem::path& candidate) {
    const std::filesystem::path normBase = baseDir.lexically_normal();
    const std::filesystem::path normCand = candidate.lexically_normal();
    if (normBase.has_root_name() || normCand.has_root_name()) {
        const std::string baseRoot =
            motrix::core::AsciiLower(motrix::core::PathToUtf8(normBase.root_name()));
        const std::string candRoot =
            motrix::core::AsciiLower(motrix::core::PathToUtf8(normCand.root_name()));
        if (baseRoot != candRoot) {
            return false;
        }
        const std::string baseUtf8 = motrix::core::AsciiLower(motrix::core::PathToUtf8(normBase));
        const std::string candUtf8 = motrix::core::AsciiLower(motrix::core::PathToUtf8(normCand));
        const std::filesystem::path lowerRel = motrix::core::PathFromUtf8(candUtf8).lexically_relative(
            motrix::core::PathFromUtf8(baseUtf8));
        if (lowerRel.empty()) {
            return false;
        }
        const auto first = *lowerRel.begin();
        return first != L".." && !lowerRel.has_root_name() && !lowerRel.has_root_path();
    }

    const std::filesystem::path rel = normCand.lexically_relative(normBase);
    if (rel.empty()) {
        return false;
    }
    const auto first = *rel.begin();
    return first != L".." && !rel.has_root_name() && !rel.has_root_path();
}

[[nodiscard]] std::string JoinSafeRelativePath(const std::filesystem::path& baseDir,
                                               std::string_view relUtf8) {
    if (relUtf8.empty() || relUtf8.find('\0') != std::string_view::npos) {
        return motrix::core::PathToUtf8(baseDir);
    }
    const std::filesystem::path sub = motrix::core::PathFromUtf8(relUtf8);
    if (sub.empty() || sub.has_root_name() || sub.has_root_path() || HasPathSegmentDotDot(sub)) {
        return motrix::core::PathToUtf8(baseDir);
    }
    const std::filesystem::path joined = (baseDir / sub).lexically_normal();
    if (!IsLexicallyWithinBase(baseDir, joined)) {
        return motrix::core::PathToUtf8(baseDir);
    }
    return motrix::core::PathToUtf8(joined);
}

}  // namespace

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
    if (task.status == TaskStatus::Complete) {
        return 1.0f;
    }
    if (task.mediaProgress >= 0.0f
        && (task.totalLength == 0 || task.completedLength >= task.totalLength)) {
        return std::clamp(task.mediaProgress, 0.0f, 1.0f);
    }
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
    return 1u + (remaining - 1u) / task.downloadSpeed;
}

std::string ResolveTaskFullPath(const DownloadTaskView& task) {
    if (task.dir.empty()) {
        if (!task.files.empty() && task.files[0].path.find('\0') == std::string::npos) {
            return task.files[0].path;
        }
        return {};
    }
    const std::filesystem::path baseDir = motrix::core::PathFromUtf8(task.dir).lexically_normal();
    const bool isMagnetPending = task.taskType == TaskType::BitTorrent
                                 && (!task.btInfo || task.btInfo->name.empty()) && task.files.empty();
    if (isMagnetPending) {
        return motrix::core::PathToUtf8(baseDir);
    }
    if (task.btInfo && !task.btInfo->name.empty()) {
        return JoinSafeRelativePath(baseDir, task.btInfo->name);
    }
    if (!task.files.empty() && !task.files[0].path.empty()) {
        const std::string& fileUtf8 = task.files[0].path;
        if (fileUtf8.find('\0') != std::string::npos) {
            return motrix::core::PathToUtf8(baseDir);
        }
        const std::filesystem::path filePath = motrix::core::PathFromUtf8(fileUtf8).lexically_normal();
        if (!filePath.has_root_name() && !filePath.has_root_path()) {
            return JoinSafeRelativePath(baseDir, fileUtf8);
        }
        return IsLexicallyWithinBase(baseDir, filePath) ? motrix::core::PathToUtf8(filePath)
                                                        : motrix::core::PathToUtf8(baseDir);
    }
    return motrix::core::PathToUtf8(baseDir);
}

}  // namespace motrix::domain

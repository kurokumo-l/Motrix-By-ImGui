module motrix.core;

import std;

namespace motrix::core {

namespace {

[[nodiscard]] std::string_view LevelTag(LogLevel level) noexcept {
    switch (level) {
        case LogLevel::Debug: return "DEBUG";
        case LogLevel::Info: return "INFO";
        case LogLevel::Warn: return "WARN";
        case LogLevel::Error: return "ERROR";
    }
    return "INFO";
}

[[nodiscard]] std::string FormatTimestamp() {
    const auto now = std::chrono::system_clock::now();
    try {
        const auto local = std::chrono::zoned_time{std::chrono::current_zone(), now};
        return std::format("{:%Y-%m-%d %H:%M:%S}",
                           std::chrono::floor<std::chrono::milliseconds>(local.get_local_time()));
    } catch (...) {
        return std::format("{:%Y-%m-%d %H:%M:%S}", std::chrono::floor<std::chrono::milliseconds>(now));
    }
}

[[nodiscard]] bool RotateBackupFiles(const std::filesystem::path& logFilePath, std::size_t maxBackups) {
    std::error_code ec;
    if (maxBackups == 0) {
        const bool existed = std::filesystem::exists(logFilePath, ec);
        if (ec) {
            return false;
        }
        if (existed) {
            ec.clear();
            std::filesystem::remove(logFilePath, ec);
            if (ec) {
                return false;
            }
        }
        return true;
    }

    for (std::size_t idx = maxBackups; idx >= 1; --idx) {
        std::filesystem::path src = logFilePath;
        if (idx > 1) {
            src += std::format(".{}", idx - 1);
        }
        std::filesystem::path dst = logFilePath;
        dst += std::format(".{}", idx);
        ec.clear();
        const bool srcExists = std::filesystem::exists(src, ec);
        if (ec) {
            return false;
        }
        if (srcExists) {
            ec.clear();
            std::filesystem::rename(src, dst, ec);
            if (ec) {
                return false;
            }
        }
    }
    return true;
}

struct RotateRegistryState {
    std::mutex mu;
    std::unordered_map<const Logger*, std::size_t> thresholds;
};

[[nodiscard]] RotateRegistryState& RotateRegistry() {
    static RotateRegistryState state;
    return state;
}

[[nodiscard]] std::size_t GetNextRotateThreshold(const Logger* logger, std::size_t fallback) {
    auto& reg = RotateRegistry();
    std::scoped_lock lk(reg.mu);
    if (const auto it = reg.thresholds.find(logger); it != reg.thresholds.end()) {
        return it->second;
    }
    return fallback;
}

void SetNextRotateThreshold(const Logger* logger, std::size_t threshold) {
    auto& reg = RotateRegistry();
    std::scoped_lock lk(reg.mu);
    reg.thresholds[logger] = threshold;
}

void EraseNextRotateThreshold(const Logger* logger) {
    auto& reg = RotateRegistry();
    std::scoped_lock lk(reg.mu);
    reg.thresholds.erase(logger);
}

}  // namespace

LogLevel ParseLogLevel(std::string_view raw) noexcept {
    if (EqualsAsciiInsensitive(raw, "debug")) {
        return LogLevel::Debug;
    }
    if (EqualsAsciiInsensitive(raw, "warn") || EqualsAsciiInsensitive(raw, "notice")) {
        return LogLevel::Warn;
    }
    if (EqualsAsciiInsensitive(raw, "error")) {
        return LogLevel::Error;
    }
    return LogLevel::Info;
}

Logger::Logger(LoggerOptions options) : options_(std::move(options)) {
    minLevel_.store(static_cast<int>(options_.minLevel), std::memory_order_relaxed);
    SetNextRotateThreshold(this, options_.maxFileBytes);
    OpenLogStream();
    worker_ = std::jthread([this](std::stop_token st) { WorkerLoop(st); });
}

Logger::~Logger() {
    if (worker_.joinable()) {
        worker_.request_stop();
        cv_.notify_all();
        worker_.join();
    }
    EraseNextRotateThreshold(this);
}

void Logger::SetMinLevel(LogLevel level) noexcept {
    minLevel_.store(static_cast<int>(level), std::memory_order_relaxed);
}

LogLevel Logger::MinLevel() const noexcept {
    return static_cast<LogLevel>(minLevel_.load(std::memory_order_relaxed));
}

void Logger::Log(LogLevel level, std::string_view category, std::string_view message) {
    if (static_cast<int>(level) < minLevel_.load(std::memory_order_relaxed)) {
        return;
    }
    std::string line =
        std::format("[{}] [{}] [{}] {}\n", FormatTimestamp(), LevelTag(level), category, message);
    {
        std::scoped_lock lock(mutex_);
        pending_.push_back(std::move(line));
    }
    cv_.notify_one();
}

void Logger::Flush() {
    std::unique_lock lock(mutex_);
    flushRequested_ = true;
    cv_.notify_one();
    cv_.wait(lock, [this]() { return pending_.empty() && !flushRequested_; });
}

void Logger::WorkerLoop(std::stop_token stopToken) {
    std::vector<std::string> batch;
    while (true) {
        {
            std::unique_lock lock(mutex_);
            cv_.wait(lock, stopToken, [this]() { return !pending_.empty() || flushRequested_; });
            if (pending_.empty() && stopToken.stop_requested()) {
                flushRequested_ = false;
                cv_.notify_all();
                break;
            }
            batch.swap(pending_);
        }

        if (!batch.empty()) {
            WriteBatch(batch);
            batch.clear();
        }

        {
            std::scoped_lock lock(mutex_);
            if (pending_.empty() && flushRequested_) {
                flushRequested_ = false;
                cv_.notify_all();
            }
        }
    }
}

void Logger::WriteBatch(std::vector<std::string>& batch) {
    for (const auto& line : batch) {
        if (options_.alsoStderr) {
            std::cerr.write(line.data(), static_cast<std::streamsize>(line.size()));
        }
        if (stream_.is_open() && stream_.good()) {
            stream_.write(line.data(), static_cast<std::streamsize>(line.size()));
            if (stream_.good()) {
                currentBytes_ += line.size();
                RotateIfNeeded();
            }
        }
    }
    if (stream_.is_open() && stream_.good()) {
        stream_.flush();
    }
}

void Logger::RotateIfNeeded() {
    const std::size_t threshold = GetNextRotateThreshold(this, options_.maxFileBytes);
    if (options_.logFilePath.empty() || options_.maxFileBytes == 0 || currentBytes_ < threshold) {
        return;
    }

    stream_.close();
    const std::size_t prevBytes = currentBytes_;
    currentBytes_ = 0;

    const bool rotated = RotateBackupFiles(options_.logFilePath, options_.maxBackups);
    OpenLogStream();
    if (rotated) {
        SetNextRotateThreshold(this, options_.maxFileBytes);
        return;
    }
    const std::size_t baseBytes = std::max(currentBytes_, prevBytes);
    const std::size_t maxSz = (std::numeric_limits<std::size_t>::max)();
    const std::size_t nextAt =
        (baseBytes <= maxSz - options_.maxFileBytes) ? (baseBytes + options_.maxFileBytes) : maxSz;
    SetNextRotateThreshold(this, nextAt);
}

void Logger::OpenLogStream() {
    if (options_.logFilePath.empty()) {
        return;
    }
    std::error_code ec;
    if (options_.logFilePath.has_parent_path()) {
        std::filesystem::create_directories(options_.logFilePath.parent_path(), ec);
    }
    stream_.clear();
    stream_.open(options_.logFilePath, std::ios::binary | std::ios::app);
    if (stream_.is_open()) {
        ec.clear();
        currentBytes_ = static_cast<std::size_t>(std::filesystem::file_size(options_.logFilePath, ec));
        if (ec) {
            currentBytes_ = 0;
        }
    }
}

}  // namespace motrix::core

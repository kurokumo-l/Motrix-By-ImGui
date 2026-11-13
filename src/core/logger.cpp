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
    const auto local = std::chrono::zoned_time{std::chrono::current_zone(), now};
    return std::format("{:%Y-%m-%d %H:%M:%S}",
                       std::chrono::floor<std::chrono::milliseconds>(local.get_local_time()));
}

}  // namespace

Logger::Logger(LoggerOptions options) : options_(std::move(options)) {
    minLevel_.store(static_cast<int>(options_.minLevel), std::memory_order_relaxed);
    OpenLogStream();
    worker_ = std::jthread([this](std::stop_token st) { WorkerLoop(st); });
}

Logger::~Logger() {
    if (worker_.joinable()) {
        worker_.request_stop();
        cv_.notify_all();
        worker_.join();
    }
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
        if (stream_.is_open()) {
            stream_.write(line.data(), static_cast<std::streamsize>(line.size()));
            currentBytes_ += line.size();
            RotateIfNeeded();
        }
    }
    if (stream_.is_open()) {
        stream_.flush();
    }
}

void Logger::RotateIfNeeded() {
    if (options_.logFilePath.empty() || options_.maxFileBytes == 0
        || currentBytes_ < options_.maxFileBytes) {
        return;
    }

    stream_.close();
    currentBytes_ = 0;

    std::error_code ec;
    for (std::size_t idx = options_.maxBackups; idx >= 1; --idx) {
        std::filesystem::path src = options_.logFilePath;
        if (idx > 1) {
            src += std::format(".{}", idx - 1);
        }
        std::filesystem::path dst = options_.logFilePath;
        dst += std::format(".{}", idx);
        if (std::filesystem::exists(src, ec)) {
            std::filesystem::rename(src, dst, ec);
        }
    }
    OpenLogStream();
}

void Logger::OpenLogStream() {
    if (options_.logFilePath.empty()) {
        return;
    }
    std::error_code ec;
    if (options_.logFilePath.has_parent_path()) {
        std::filesystem::create_directories(options_.logFilePath.parent_path(), ec);
    }
    stream_.open(options_.logFilePath, std::ios::binary | std::ios::app);
    if (stream_.is_open()) {
        currentBytes_ = static_cast<std::size_t>(std::filesystem::file_size(options_.logFilePath, ec));
        if (ec) {
            currentBytes_ = 0;
        }
    }
}

}  // namespace motrix::core

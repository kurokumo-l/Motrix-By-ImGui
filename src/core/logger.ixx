export module motrix.core:logger;

import std;

namespace motrix::core {

export enum class LogLevel {
    Debug = 0,
    Info,
    Warn,
    Error,
};

export [[nodiscard]] LogLevel ParseLogLevel(std::string_view raw) noexcept;

export struct LoggerOptions {
    std::filesystem::path logFilePath;
    LogLevel minLevel = LogLevel::Info;
    std::size_t maxFileBytes = 5u * 1024u * 1024u;
    std::size_t maxBackups = 3;
    bool alsoStderr = false;
};

export class Logger {
public:
    explicit Logger(LoggerOptions options = {});
    ~Logger();

    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;

    void SetMinLevel(LogLevel level) noexcept;
    [[nodiscard]] LogLevel MinLevel() const noexcept;

    void Log(LogLevel level, std::string_view category, std::string_view message);
    void Flush();

    template <typename... Args>
    void Debug(std::string_view category, std::format_string<Args...> fmt, Args&&... args) {
        if (static_cast<int>(LogLevel::Debug) >= minLevel_.load(std::memory_order_relaxed)) {
            Log(LogLevel::Debug, category, std::format(fmt, std::forward<Args>(args)...));
        }
    }

    template <typename... Args>
    void Info(std::string_view category, std::format_string<Args...> fmt, Args&&... args) {
        if (static_cast<int>(LogLevel::Info) >= minLevel_.load(std::memory_order_relaxed)) {
            Log(LogLevel::Info, category, std::format(fmt, std::forward<Args>(args)...));
        }
    }

    template <typename... Args>
    void Warn(std::string_view category, std::format_string<Args...> fmt, Args&&... args) {
        if (static_cast<int>(LogLevel::Warn) >= minLevel_.load(std::memory_order_relaxed)) {
            Log(LogLevel::Warn, category, std::format(fmt, std::forward<Args>(args)...));
        }
    }

    template <typename... Args>
    void Error(std::string_view category, std::format_string<Args...> fmt, Args&&... args) {
        if (static_cast<int>(LogLevel::Error) >= minLevel_.load(std::memory_order_relaxed)) {
            Log(LogLevel::Error, category, std::format(fmt, std::forward<Args>(args)...));
        }
    }

private:
    void WorkerLoop(std::stop_token stopToken);
    void WriteBatch(std::vector<std::string>& batch);
    void RotateIfNeeded();
    void OpenLogStream();

    LoggerOptions options_;
    std::atomic<int> minLevel_{static_cast<int>(LogLevel::Info)};
    std::mutex mutex_;
    std::condition_variable_any cv_;
    std::vector<std::string> pending_;
    bool flushRequested_ = false;
    std::ofstream stream_;
    std::size_t currentBytes_ = 0;
    std::jthread worker_;
};

}  // namespace motrix::core

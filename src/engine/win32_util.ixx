export module motrix.engine:win32_util;

import std;

namespace motrix::engine::detail {

export class ScopedWinHandle {
public:
    ScopedWinHandle() noexcept = default;
    explicit ScopedWinHandle(void* h) noexcept : handle_(h) {}
    ~ScopedWinHandle();

    ScopedWinHandle(const ScopedWinHandle&) = delete;
    ScopedWinHandle& operator=(const ScopedWinHandle&) = delete;

    ScopedWinHandle(ScopedWinHandle&& other) noexcept;
    ScopedWinHandle& operator=(ScopedWinHandle&& other) noexcept;

    [[nodiscard]] void* Get() const noexcept { return handle_; }
    [[nodiscard]] bool IsValid() const noexcept;
    void Reset(void* newHandle = nullptr) noexcept;

private:
    void* handle_ = nullptr;
};

export [[nodiscard]] std::wstring QuoteWin32Arg(std::wstring_view arg);
export [[nodiscard]] std::wstring BuildCommandLineW(const std::filesystem::path& exe,
                                                    const std::vector<std::string>& args);
export [[nodiscard]] std::filesystem::path GetExecutableDir();
export [[nodiscard]] std::filesystem::path GetLocalAppDataDir();
export [[nodiscard]] bool IsRegularFileSafe(const std::filesystem::path& p);

}  // namespace motrix::engine::detail

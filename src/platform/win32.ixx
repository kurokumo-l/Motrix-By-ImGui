export module motrix.platform:win32;

import std;
import motrix.core;
import :window_host;

namespace motrix::platform {

export class UniqueHandle {
public:
    UniqueHandle() noexcept = default;
    explicit UniqueHandle(void* raw) noexcept : handle_(raw) {}
    ~UniqueHandle();

    UniqueHandle(const UniqueHandle&) = delete;
    UniqueHandle& operator=(const UniqueHandle&) = delete;

    UniqueHandle(UniqueHandle&& other) noexcept;
    UniqueHandle& operator=(UniqueHandle&& other) noexcept;

    [[nodiscard]] void* Get() const noexcept { return handle_; }
    [[nodiscard]] bool IsValid() const noexcept;
    void Reset(void* newHandle = nullptr) noexcept;
    [[nodiscard]] void* Release() noexcept;

private:
    void* handle_ = nullptr;
};

export class UniqueFindHandle {
public:
    UniqueFindHandle() noexcept = default;
    explicit UniqueFindHandle(void* raw) noexcept : handle_(raw) {}
    ~UniqueFindHandle();

    UniqueFindHandle(const UniqueFindHandle&) = delete;
    UniqueFindHandle& operator=(const UniqueFindHandle&) = delete;

    UniqueFindHandle(UniqueFindHandle&& other) noexcept;
    UniqueFindHandle& operator=(UniqueFindHandle&& other) noexcept;

    [[nodiscard]] bool IsValid() const noexcept;
    void Reset(void* newHandle = nullptr) noexcept;

private:
    void* handle_ = nullptr;
};

export template <typename T> class ComPtr {
public:
    ComPtr() noexcept = default;
    ~ComPtr() { Reset(); }

    ComPtr(const ComPtr&) = delete;
    ComPtr& operator=(const ComPtr&) = delete;

    ComPtr(ComPtr&& other) noexcept : ptr_(other.ptr_) { other.ptr_ = nullptr; }
    ComPtr& operator=(ComPtr&& other) noexcept {
        if (this != &other) {
            Reset();
            ptr_ = other.ptr_;
            other.ptr_ = nullptr;
        }
        return *this;
    }

    [[nodiscard]] T* Get() const noexcept { return ptr_; }
    [[nodiscard]] T* operator->() const noexcept { return ptr_; }
    [[nodiscard]] explicit operator bool() const noexcept { return ptr_ != nullptr; }

    [[nodiscard]] T** ReleaseAndGetAddressOf() noexcept {
        Reset();
        return &ptr_;
    }

    void Reset(T* newPtr = nullptr) noexcept {
        if (ptr_) {
            ptr_->Release();
        }
        ptr_ = newPtr;
    }

private:
    T* ptr_ = nullptr;
};

export class Win32WindowHost final : public IWindowHost {
public:
    using FrameCallback = std::function<void(IWindowHost&)>;
    using InitCallback = std::function<void(IWindowHost&)>;

    Win32WindowHost();
    ~Win32WindowHost() override;

    Win32WindowHost(const Win32WindowHost&) = delete;
    Win32WindowHost& operator=(const Win32WindowHost&) = delete;

    [[nodiscard]] motrix::core::Result<int>
    Run(const WindowHostConfig& config, const InitCallback& onImGuiInit, const FrameCallback& onFrame);

    [[nodiscard]] float DpiScale() const noexcept override;
    [[nodiscard]] bool IsMaximized() const noexcept override;
    [[nodiscard]] bool IsHiddenToTray() const noexcept override;
    [[nodiscard]] HostMetricsSnapshot Metrics() const noexcept override;

    void Minimize() override;
    void ToggleMaximize() override;
    void Close() override;
    void ShowFromTray() override;
    void HideToTray() override;
    void SetDarkFrame(bool dark) override;
    void SetWindowTitle(std::string_view utf8Title) override;

    void RequestFrame() override;
    void NotifyAnimationActive() override;
    void SetTitlebarHeight(float heightPx) override;
    void RegisterTitlebarInteractiveRect(TitlebarRect rect) override;
    void CopyTextToClipboard(std::string_view utf8) override;
    void OpenPathInExplorer(std::string_view utf8Path) override;
    [[nodiscard]] std::optional<std::string> PickTorrentFile() override;
    [[nodiscard]] std::optional<std::string> PickFolder(std::string_view initialDir = {}) override;

    // Called from WndProc bridge inside win32_host.cpp
    struct Impl;

private:
    std::unique_ptr<Impl> impl_;
};

export void InstallCrashMinidumpHandler(const std::filesystem::path& dumpDir);
export [[nodiscard]] std::filesystem::path ResolveDefaultDownloadsDir();
export [[nodiscard]] std::filesystem::path ResolveAppDataDir();

}  // namespace motrix::platform

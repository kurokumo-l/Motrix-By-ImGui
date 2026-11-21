export module motrix.platform:win32_internal;

import std;
import motrix.core;
import :window_host;

namespace motrix::platform {

export inline constexpr std::uint32_t kWmTrayCallback = 0x8000u + 102u;  // WM_APP + 102

export struct Win32WindowState {
    WindowHostConfig config;
    void* hwnd = nullptr;
    void* hInstance = nullptr;
    void* trayIdleIcon = nullptr;
    void* trayActiveIcon = nullptr;
    void* taskbarList = nullptr;
    float dpiScale = 1.0f;
    float titlebarHeightPx = 36.0f;
    bool darkFrame = false;
    bool systemDark = false;
    bool hiddenToTray = false;
    bool bindCloseToHide = false;
    bool autoHideOnBlur = false;
    bool enableTrayIcon = true;
    bool trayInstalled = false;
    bool trayActiveDownloads = false;
    bool trayIsZhCn = false;
    bool trayDarkTaskbar = true;
    bool explicitQuitRequested = false;
    bool titlebarBlocked = false;
    bool inPickerDialog = false;
    bool trimRequested = false;
    std::uint32_t wmTaskbarCreated = 0;
    std::uint32_t wmTaskbarButtonCreated = 0;
    TaskbarProgressMode lastProgressMode = TaskbarProgressMode::None;
    std::uint64_t lastProgressCompleted = 0;
    std::uint64_t lastProgressTotal = 0;
    std::uint32_t trayDpi = 0;
    std::uint32_t pendingResizeW = 0;
    std::uint32_t pendingResizeH = 0;
    int activeFramesRemaining = 3;
    std::string trayTooltipUtf8;
    std::string lastToastTargetDir;
    std::vector<TitlebarRect> titlebarRects;
};

export void Win32EnableDpiAwareness() noexcept;
export [[nodiscard]] float Win32GetPrimaryMonitorDpiScale() noexcept;
export [[nodiscard]] float Win32GetWindowDpiScale(void* hwnd) noexcept;

export [[nodiscard]] motrix::core::Result<void> Win32CreateWindow(Win32WindowState& state);
export void Win32DestroyWindow(Win32WindowState& state) noexcept;
export void Win32ShowWindow(void* hwnd, bool show, bool activate) noexcept;
export void Win32MinimizeWindow(void* hwnd) noexcept;
export void Win32ToggleMaximizeWindow(void* hwnd) noexcept;
export void Win32PostCloseWindow(void* hwnd) noexcept;
export void Win32ApplyDwmChrome(void* hwnd, bool dark) noexcept;
export void Win32SetWindowTitle(void* hwnd, std::string_view utf8Title);
export [[nodiscard]] bool Win32IsWindowZoomed(void* hwnd) noexcept;
export [[nodiscard]] bool Win32IsWindowIconic(void* hwnd) noexcept;
export [[nodiscard]] WindowBounds Win32ReadWindowBounds(void* hwnd) noexcept;
export void
Win32ApplyInitialWindowBounds(void* hwnd, const WindowHostConfig& cfg, float dpiScale) noexcept;
export void Win32CopyTextToClipboard(void* hwnd, std::string_view utf8);
export void Win32OpenPathInExplorer(std::string_view utf8Path);
export [[nodiscard]] std::optional<std::string> Win32PickTorrentFile(void* ownerHwnd);
export [[nodiscard]] std::optional<std::string> Win32PickFolder(void* ownerHwnd,
                                                                std::string_view initialDir);

export void Win32SetWindowIcons(Win32WindowState& state);
export void Win32RefreshIconsOnThemeOrDpi(Win32WindowState& state);
export void Win32DestroyTrayIcons(Win32WindowState& state) noexcept;
export void Win32InstallOrUpdateTray(Win32WindowState& state);
export void Win32RemoveTray(Win32WindowState& state) noexcept;
export void Win32ShowTrayBalloon(Win32WindowState& state,
                                 std::string_view title,
                                 std::string_view body,
                                 std::string_view clickTargetDir);
export void Win32HandleTrayCallback(Win32WindowState& state, std::uint64_t lParam);
export void Win32UpdateTaskbarProgress(void* hwnd,
                                       TaskbarProgressMode mode,
                                       std::uint64_t completed,
                                       std::uint64_t total) noexcept;
export void Win32SetCachedTaskbarProgress(Win32WindowState& state,
                                          TaskbarProgressMode mode,
                                          std::uint64_t completed,
                                          std::uint64_t total) noexcept;
export void Win32ReleaseTaskbarList(Win32WindowState& state) noexcept;
export void Win32RegisterShellMessages(Win32WindowState& state) noexcept;
export [[nodiscard]] bool Win32HandleShellOrEndSession(Win32WindowState& state,
                                                       std::uint32_t msg,
                                                       std::uint64_t wParam,
                                                       std::int64_t& resultOut);
export void Win32HandleImmersiveColorChange(Win32WindowState& state, std::int64_t lParam);
export class ScopedComInit {
public:
    ScopedComInit() noexcept;
    ~ScopedComInit();
    ScopedComInit(const ScopedComInit&) = delete;
    ScopedComInit& operator=(const ScopedComInit&) = delete;
    [[nodiscard]] bool IsUsable() const noexcept { return usable_; }

private:
    bool shouldUninit_ = false;
    bool usable_ = false;
};

export void Win32ShowWindowMode(void* hwnd, bool maximized, bool activate) noexcept;
export void Win32SetSleepBlocker(bool active) noexcept;
export void Win32TrimWorkingSet() noexcept;
export void FreeWin32CoTaskMem(void* ptr) noexcept;
export struct CoTaskMemDeleter {
    void operator()(void* ptr) const noexcept { FreeWin32CoTaskMem(ptr); }
};
export template <typename T = wchar_t> using UniqueCoTaskPtr = std::unique_ptr<T, CoTaskMemDeleter>;
export [[nodiscard]] bool QueryTaskbarDarkMode() noexcept;

}  // namespace motrix::platform

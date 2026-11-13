export module motrix.platform:win32_internal;

import std;
import motrix.core;
import :window_host;

namespace motrix::platform {

export struct Win32WindowState {
    WindowHostConfig config;
    void* hwnd = nullptr;
    void* hInstance = nullptr;
    float dpiScale = 1.0f;
    float titlebarHeightPx = 36.0f;
    bool darkFrame = false;
    bool hiddenToTray = false;
    std::uint32_t pendingResizeW = 0;
    std::uint32_t pendingResizeH = 0;
    int activeFramesRemaining = 3;
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
export void Win32CopyTextToClipboard(void* hwnd, std::string_view utf8);
export void Win32OpenPathInExplorer(std::string_view utf8Path);

}  // namespace motrix::platform

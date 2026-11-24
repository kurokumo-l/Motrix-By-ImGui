module;

// clang-format off
#include <windows.h>
#include <shobjidl.h>
// clang-format on

module motrix.platform;

import std;
import motrix.core;
import :window_host;
import :win32;
import :win32_internal;

namespace motrix::platform {

namespace {

[[nodiscard]] TBPFLAG ToWin32ProgressFlag(TaskbarProgressMode mode) noexcept {
    switch (mode) {
        case TaskbarProgressMode::None: return TBPF_NOPROGRESS;
        case TaskbarProgressMode::Indeterminate: return TBPF_INDETERMINATE;
        case TaskbarProgressMode::Normal: return TBPF_NORMAL;
        case TaskbarProgressMode::Error: return TBPF_ERROR;
        case TaskbarProgressMode::Paused: return TBPF_PAUSED;
    }
    return TBPF_NOPROGRESS;
}

struct TaskbarProgressValue {
    TaskbarProgressMode mode = TaskbarProgressMode::None;
    std::uint64_t completed = 0;
    std::uint64_t total = 0;
};

void ApplyTaskbarProgress(ITaskbarList3* taskbar, HWND hwnd, const TaskbarProgressValue& val) noexcept {
    if (!taskbar || !hwnd) {
        return;
    }
    const TBPFLAG flag = ToWin32ProgressFlag(val.mode);
    taskbar->SetProgressState(hwnd, flag);
    if (val.mode == TaskbarProgressMode::Normal || val.mode == TaskbarProgressMode::Error
        || val.mode == TaskbarProgressMode::Paused) {
        const std::uint64_t safeTotal = std::max<std::uint64_t>(val.total, 1ull);
        const std::uint64_t safeCompleted = std::min(val.completed, safeTotal);
        taskbar->SetProgressValue(hwnd, safeCompleted, safeTotal);
    }
}

[[nodiscard]] ITaskbarList3* AcquireCachedTaskbarList(Win32WindowState& state) noexcept {
    if (state.taskbarList) {
        return static_cast<ITaskbarList3*>(state.taskbarList);
    }
    ComPtr<ITaskbarList3> taskbar;
    const HRESULT hrCreate = ::CoCreateInstance(CLSID_TaskbarList,
                                                nullptr,
                                                CLSCTX_INPROC_SERVER,
                                                IID_PPV_ARGS(taskbar.ReleaseAndGetAddressOf()));
    if (FAILED(hrCreate) || !taskbar || FAILED(taskbar->HrInit())) {
        return nullptr;
    }
    state.taskbarList = taskbar.Detach();
    return static_cast<ITaskbarList3*>(state.taskbarList);
}

}  // namespace

void Win32UpdateTaskbarProgress(void* hwndVoid,
                                TaskbarProgressMode mode,
                                std::uint64_t completed,
                                std::uint64_t total) noexcept {
    if (!hwndVoid) {
        return;
    }

    ScopedComInit com;
    if (!com.IsUsable()) {
        return;
    }

    ComPtr<ITaskbarList3> taskbar;
    const HRESULT hrCreate = ::CoCreateInstance(CLSID_TaskbarList,
                                                nullptr,
                                                CLSCTX_INPROC_SERVER,
                                                IID_PPV_ARGS(taskbar.ReleaseAndGetAddressOf()));
    if (FAILED(hrCreate) || !taskbar || FAILED(taskbar->HrInit())) {
        return;
    }
    ApplyTaskbarProgress(taskbar.Get(), static_cast<HWND>(hwndVoid), {mode, completed, total});
}

void Win32SetCachedTaskbarProgress(Win32WindowState& state,
                                   TaskbarProgressMode mode,
                                   std::uint64_t completed,
                                   std::uint64_t total) noexcept {
    const auto next = std::tuple(mode, completed, total);
    if (std::tie(state.lastProgressMode, state.lastProgressCompleted, state.lastProgressTotal) == next) {
        return;
    }
    std::tie(state.lastProgressMode, state.lastProgressCompleted, state.lastProgressTotal) = next;
    if (!state.hwnd) {
        return;
    }
    ApplyTaskbarProgress(
        AcquireCachedTaskbarList(state), static_cast<HWND>(state.hwnd), {mode, completed, total});
}

void Win32ReleaseTaskbarList(Win32WindowState& state) noexcept {
    if (state.taskbarList) {
        static_cast<ITaskbarList3*>(state.taskbarList)->Release();
        state.taskbarList = nullptr;
    }
}

void Win32RegisterShellMessages(Win32WindowState& state) noexcept {
    state.wmTaskbarCreated = ::RegisterWindowMessageW(L"TaskbarCreated");
    state.wmTaskbarButtonCreated = ::RegisterWindowMessageW(L"TaskbarButtonCreated");
}

bool Win32HandleShellOrEndSession(Win32WindowState& state,
                                  std::uint32_t msg,
                                  std::uint64_t wParam,
                                  std::int64_t& resultOut) {
    if (msg == WM_QUERYENDSESSION) {
        if (state.config.onSessionEnding) {
            state.config.onSessionEnding(true);
        }
        resultOut = TRUE;
        return true;
    }
    if (msg == WM_ENDSESSION) {
        if (wParam != FALSE) {
            state.explicitQuitRequested = true;
            if (state.config.onSessionEnding) {
                state.config.onSessionEnding(true);
            }
            if (state.config.onBoundsChanged && state.hwnd) {
                state.config.onBoundsChanged(Win32ReadWindowBounds(state.hwnd));
            }
            if (state.config.onQuitRequested) {
                state.config.onQuitRequested();
            }
            ::PostQuitMessage(0);
        } else if (state.config.onSessionEnding) {
            state.config.onSessionEnding(false);
        }
        resultOut = 0;
        return true;
    }
    if (state.wmTaskbarCreated != 0 && msg == state.wmTaskbarCreated) {
        if (state.enableTrayIcon && state.config.screenshotPath.empty()) {
            state.trayInstalled = false;
            Win32InstallOrUpdateTray(state);
        }
        resultOut = 0;
        return true;
    }
    if (state.wmTaskbarButtonCreated != 0 && msg == state.wmTaskbarButtonCreated) {
        Win32ReleaseTaskbarList(state);
        if (state.hwnd && state.lastProgressMode != TaskbarProgressMode::None) {
            ApplyTaskbarProgress(
                AcquireCachedTaskbarList(state),
                static_cast<HWND>(state.hwnd),
                {state.lastProgressMode, state.lastProgressCompleted, state.lastProgressTotal});
        }
        resultOut = 0;
        return true;
    }
    return false;
}

void Win32HandleImmersiveColorChange(Win32WindowState& state, std::int64_t lParam) {
    if (lParam != 0 && ::_wcsicmp(reinterpret_cast<const wchar_t*>(lParam), L"ImmersiveColorSet") == 0) {
        state.systemDark = QuerySystemDarkMode();
        Win32RefreshIconsOnThemeOrDpi(state);
        state.BumpActiveFrames(3);
    }
}

void Win32ShowWindowMode(void* hwndVoid, bool maximized, bool activate) noexcept {
    if (!maximized || !hwndVoid) {
        Win32ShowWindow(hwndVoid, true, activate);
        return;
    }
    HWND hwnd = static_cast<HWND>(hwndVoid);
    ::ShowWindow(hwnd, SW_SHOWMAXIMIZED);
    if (activate) {
        ::SetForegroundWindow(hwnd);
    }
    ::UpdateWindow(hwnd);
}

void Win32SetSleepBlocker(bool active) noexcept {
    const EXECUTION_STATE flags = active ? (ES_CONTINUOUS | ES_SYSTEM_REQUIRED) : ES_CONTINUOUS;
    ::SetThreadExecutionState(flags);
}

WindowBounds Win32ReadWindowBounds(void* hwndVoid) noexcept {
    if (!hwndVoid) {
        return WindowBounds{};
    }
    HWND hwnd = static_cast<HWND>(hwndVoid);
    WINDOWPLACEMENT wp{};
    wp.length = sizeof(WINDOWPLACEMENT);
    if (!::GetWindowPlacement(hwnd, &wp)) {
        return WindowBounds{};
    }

    const bool zoomed = (::IsZoomed(hwnd) != FALSE);
    const bool iconic = (::IsIconic(hwnd) != FALSE);
    const bool restorableToMax = iconic && ((wp.flags & WPF_RESTORETOMAXIMIZED) != 0);

    RECT rc{};
    if (!zoomed && !iconic && ::GetWindowRect(hwnd, &rc)) {
        return WindowBounds{
            .x = static_cast<int>(rc.left),
            .y = static_cast<int>(rc.top),
            .width = std::max(1, static_cast<int>(rc.right - rc.left)),
            .height = std::max(1, static_cast<int>(rc.bottom - rc.top)),
            .hasPosition = true,
            .maximized = false,
        };
    }

    // Convert rcNormalPosition from primary workspace coords back to virtual screen coords.
    int offsetX = 0;
    int offsetY = 0;
    MONITORINFO primaryInfo{};
    primaryInfo.cbSize = sizeof(MONITORINFO);
    if (HMONITOR primaryMon = ::MonitorFromPoint(POINT{0, 0}, MONITOR_DEFAULTTOPRIMARY);
        primaryMon && ::GetMonitorInfoW(primaryMon, &primaryInfo)) {
        offsetX = static_cast<int>(primaryInfo.rcWork.left - primaryInfo.rcMonitor.left);
        offsetY = static_cast<int>(primaryInfo.rcWork.top - primaryInfo.rcMonitor.top);
    }

    rc = wp.rcNormalPosition;
    return WindowBounds{
        .x = static_cast<int>(rc.left) + offsetX,
        .y = static_cast<int>(rc.top) + offsetY,
        .width = std::max(1, static_cast<int>(rc.right - rc.left)),
        .height = std::max(1, static_cast<int>(rc.bottom - rc.top)),
        .hasPosition = true,
        .maximized = zoomed || restorableToMax,
    };
}

void Win32ApplyInitialWindowBounds(void* hwndVoid,
                                   const WindowHostConfig& cfg,
                                   float dpiScale) noexcept {
    if (!hwndVoid || !cfg.initialBounds) {
        return;
    }

    HWND hwnd = static_cast<HWND>(hwndVoid);
    const WindowBounds& bounds = *cfg.initialBounds;
    const float effectiveScale = dpiScale > 0.0f ? dpiScale : 1.0f;
    const int minW = std::max(1, static_cast<int>(static_cast<float>(cfg.minWidth) * effectiveScale));
    const int minH = std::max(1, static_cast<int>(static_cast<float>(cfg.minHeight) * effectiveScale));

    HMONITOR mon = nullptr;
    if (bounds.hasPosition) {
        const POINT anchor{
            bounds.x + std::max(bounds.width, minW) / 2,
            bounds.y + std::max(bounds.height, minH) / 2,
        };
        mon = ::MonitorFromPoint(anchor, MONITOR_DEFAULTTONEAREST);
    } else {
        mon = ::MonitorFromWindow(hwnd, MONITOR_DEFAULTTOPRIMARY);
    }

    MONITORINFO mi{};
    mi.cbSize = sizeof(MONITORINFO);
    if (!mon || !::GetMonitorInfoW(mon, &mi)) {
        return;
    }

    const RECT& work = mi.rcWork;
    const int workW = std::max(1, static_cast<int>(work.right - work.left));
    const int workH = std::max(1, static_cast<int>(work.bottom - work.top));

    const int clampedW = std::clamp(bounds.width, std::min(minW, workW), workW);
    const int clampedH = std::clamp(bounds.height, std::min(minH, workH), workH);

    int clampedX = static_cast<int>(work.left) + (workW - clampedW) / 2;
    int clampedY = static_cast<int>(work.top) + (workH - clampedH) / 2;
    if (bounds.hasPosition) {
        clampedX =
            std::clamp(bounds.x, static_cast<int>(work.left), static_cast<int>(work.right) - clampedW);
        clampedY =
            std::clamp(bounds.y, static_cast<int>(work.top), static_cast<int>(work.bottom) - clampedH);
    }

    ::SetWindowPos(hwnd, nullptr, clampedX, clampedY, clampedW, clampedH, SWP_NOZORDER | SWP_NOACTIVATE);
}

}  // namespace motrix::platform

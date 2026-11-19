module;

// clang-format off
#include <windows.h>
#include <windowsx.h>
#include <dwmapi.h>
#include <shellapi.h>
// clang-format on

#include "imgui.h"
#include "imgui_impl_win32.h"

module motrix.platform;

import std;
import motrix.core;
import :window_host;
import :win32_internal;

extern "C++" IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd,
                                                                   UINT msg,
                                                                   WPARAM wParam,
                                                                   LPARAM lParam);

namespace motrix::platform {

namespace {

constexpr wchar_t kWindowClassName[] = L"MotrixNativeWindowClass";
constexpr UINT kWmAppWakeup = WM_APP + 101;
constexpr DWORD kDwmUseImmersiveDarkMode = 20;
constexpr DWORD kDwmWindowCornerPreference = 33;
constexpr int kDwmRoundCorner = 2;

[[nodiscard]] bool ShouldWakeFrameForMessage(UINT msg) noexcept {
    static constexpr std::array<UINT, 31> kWakeMessages{WM_PAINT,
                                                        WM_MOUSEMOVE,
                                                        WM_NCMOUSEMOVE,
                                                        WM_MOUSELEAVE,
                                                        WM_NCMOUSELEAVE,
                                                        WM_LBUTTONDOWN,
                                                        WM_LBUTTONUP,
                                                        WM_LBUTTONDBLCLK,
                                                        WM_RBUTTONDOWN,
                                                        WM_RBUTTONUP,
                                                        WM_MBUTTONDOWN,
                                                        WM_MBUTTONUP,
                                                        WM_MOUSEWHEEL,
                                                        WM_MOUSEHWHEEL,
                                                        WM_KEYDOWN,
                                                        WM_KEYUP,
                                                        WM_SYSKEYDOWN,
                                                        WM_SYSKEYUP,
                                                        WM_CHAR,
                                                        WM_IME_CHAR,
                                                        WM_IME_COMPOSITION,
                                                        WM_IME_STARTCOMPOSITION,
                                                        WM_IME_ENDCOMPOSITION,
                                                        WM_SETFOCUS,
                                                        WM_KILLFOCUS,
                                                        WM_SIZE,
                                                        WM_DPICHANGED,
                                                        WM_ACTIVATE,
                                                        WM_SETCURSOR,
                                                        WM_DROPFILES,
                                                        kWmAppWakeup};
    return std::ranges::find(kWakeMessages, msg) != kWakeMessages.end();
}

[[nodiscard]] LRESULT HitTestBorder(POINT pt, const RECT& rc, int border) noexcept {
    static constexpr LRESULT kHit[3][3]{
        {HTTOPLEFT, HTTOP, HTTOPRIGHT},
        {HTLEFT, HTCLIENT, HTRIGHT},
        {HTBOTTOMLEFT, HTBOTTOM, HTBOTTOMRIGHT},
    };
    const int xIdx = (pt.x < border) ? 0 : ((pt.x >= rc.right - border) ? 2 : 1);
    const int yIdx = (pt.y < border) ? 0 : ((pt.y >= rc.bottom - border) ? 2 : 1);
    return kHit[yIdx][xIdx];
}

void DispatchDroppedFiles(Win32WindowState& state, HDROP hDrop) {
    if (!hDrop) {
        return;
    }
    const UINT count = ::DragQueryFileW(hDrop, 0xFFFFFFFFu, nullptr, 0);
    std::vector<std::string> paths;
    paths.reserve(count);
    for (UINT i = 0; i < count; ++i) {
        const UINT len = ::DragQueryFileW(hDrop, i, nullptr, 0);
        if (len == 0) {
            continue;
        }
        std::wstring buf(len + 1, L'\0');
        if (::DragQueryFileW(hDrop, i, buf.data(), len + 1) > 0) {
            buf.resize(len);
            paths.push_back(motrix::core::WideToUtf8(buf));
        }
    }
    ::DragFinish(hDrop);
    if (!paths.empty() && state.config.onFilesDropped) {
        state.config.onFilesDropped(std::move(paths));
        state.activeFramesRemaining = std::max(state.activeFramesRemaining, 3);
    }
}

[[nodiscard]] LRESULT HandleNcHitTest(const Win32WindowState& state, HWND hWnd, LPARAM lParam) {
    POINT pt = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
    ::ScreenToClient(hWnd, &pt);

    RECT rc{};
    ::GetClientRect(hWnd, &rc);

    if (!::IsZoomed(hWnd)) {
        const int border = static_cast<int>(6.0f * state.dpiScale);
        const LRESULT edge = HitTestBorder(pt, rc, border);
        if (edge != HTCLIENT) {
            return edge;
        }
    }

    const int barH = static_cast<int>(state.titlebarHeightPx * state.dpiScale);
    if (pt.y >= 0 && pt.y < barH) {
        const bool popupOpen =
            ImGui::GetCurrentContext() && ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId);
        if (state.titlebarBlocked || popupOpen) {
            return HTCLIENT;
        }
        for (const auto& r : state.titlebarRects) {
            if (static_cast<float>(pt.x) >= r.x0 && static_cast<float>(pt.x) <= r.x1
                && static_cast<float>(pt.y) >= r.y0 && static_cast<float>(pt.y) <= r.y1) {
                return HTCLIENT;
            }
        }
        return HTCAPTION;
    }
    return HTCLIENT;
}

struct WindowMsg {
    HWND hWnd = nullptr;
    UINT msg = 0;
    WPARAM wParam = 0;
    LPARAM lParam = 0;
};

void AdjustZoomedNcCalcSize(HWND hWnd, WPARAM wParam, LPARAM lParam) noexcept {
    if (wParam != TRUE || !::IsZoomed(hWnd)) {
        return;
    }
    auto* params = reinterpret_cast<NCCALCSIZE_PARAMS*>(lParam);
    const UINT dpi = ::GetDpiForWindow(hWnd);
    const int pad = ::GetSystemMetricsForDpi(SM_CXPADDEDBORDER, dpi);
    const int cx = ::GetSystemMetricsForDpi(SM_CXSIZEFRAME, dpi) + pad;
    const int cy = ::GetSystemMetricsForDpi(SM_CYSIZEFRAME, dpi) + pad;
    params->rgrc[0].left += cx;
    params->rgrc[0].right -= cx;
    params->rgrc[0].top += cy;
    params->rgrc[0].bottom -= cy;
}

void HandleSizeMessage(Win32WindowState& state, const WindowMsg& m) {
    if (m.wParam == SIZE_MINIMIZED) {
        return;
    }
    state.pendingResizeW = static_cast<std::uint32_t>(LOWORD(m.lParam));
    state.pendingResizeH = static_cast<std::uint32_t>(HIWORD(m.lParam));
    if ((m.wParam == SIZE_MAXIMIZED || m.wParam == SIZE_RESTORED) && state.config.onBoundsChanged
        && ::IsWindowVisible(m.hWnd)) {
        state.config.onBoundsChanged(Win32ReadWindowBounds(m.hWnd));
    }
}

void HandleBlurAutoHide(Win32WindowState& state, const WindowMsg& m) {
    if (LOWORD(m.wParam) != WA_INACTIVE || !state.autoHideOnBlur || state.inPickerDialog
        || !state.config.screenshotPath.empty() || !::IsWindowVisible(m.hWnd) || ::IsIconic(m.hWnd)) {
        return;
    }
    HWND next = reinterpret_cast<HWND>(m.lParam);
    DWORD nextPid = 0;
    if (next) {
        ::GetWindowThreadProcessId(next, &nextPid);
    }
    if (!next || nextPid != ::GetCurrentProcessId()) {
        Win32ShowWindow(m.hWnd, false, false);
        state.hiddenToTray = true;
        state.trimRequested = true;
    }
}

LRESULT HandleMessage(Win32WindowState& state, const WindowMsg& m) {
    if (ShouldWakeFrameForMessage(m.msg)) {
        state.activeFramesRemaining = std::max(state.activeFramesRemaining, 3);
    }

    switch (m.msg) {
        case WM_NCCALCSIZE:
            AdjustZoomedNcCalcSize(m.hWnd, m.wParam, m.lParam);
            return (m.wParam == TRUE) ? 0 : ::DefWindowProcW(m.hWnd, m.msg, m.wParam, m.lParam);
        case WM_NCHITTEST: return HandleNcHitTest(state, m.hWnd, m.lParam);
        case WM_GETMINMAXINFO: {
            auto* mmi = reinterpret_cast<MINMAXINFO*>(m.lParam);
            mmi->ptMinTrackSize.x =
                static_cast<LONG>(static_cast<float>(state.config.minWidth) * state.dpiScale);
            mmi->ptMinTrackSize.y =
                static_cast<LONG>(static_cast<float>(state.config.minHeight) * state.dpiScale);
            return 0;
        }
        case WM_DPICHANGED: {
            const UINT newDpi = HIWORD(m.wParam);
            if (newDpi > 0) {
                state.dpiScale = static_cast<float>(newDpi) / 96.0f;
            }
            const auto* s = reinterpret_cast<const RECT*>(m.lParam);
            ::SetWindowPos(m.hWnd,
                           nullptr,
                           s->left,
                           s->top,
                           s->right - s->left,
                           s->bottom - s->top,
                           SWP_NOZORDER | SWP_NOACTIVATE);
            Win32RefreshIconsOnThemeOrDpi(state);
            return 0;
        }
        case WM_SIZE: HandleSizeMessage(state, m); return 0;
        case WM_EXITSIZEMOVE:
            if (state.config.onBoundsChanged && !::IsIconic(m.hWnd)) {
                state.config.onBoundsChanged(Win32ReadWindowBounds(m.hWnd));
            }
            return 0;
        case WM_ACTIVATE: HandleBlurAutoHide(state, m); break;
        case WM_SETTINGCHANGE: Win32HandleImmersiveColorChange(state, m.lParam); break;
        case kWmTrayCallback:
            Win32HandleTrayCallback(state, static_cast<std::uint64_t>(m.lParam));
            return 0;
        case WM_CLOSE:
            if (state.config.onBoundsChanged) {
                state.config.onBoundsChanged(Win32ReadWindowBounds(m.hWnd));
            }
            if (state.bindCloseToHide && !state.explicitQuitRequested) {
                Win32ShowWindow(m.hWnd, false, false);
                state.hiddenToTray = true;
                state.trimRequested = true;
                return 0;
            }
            if (state.config.onQuitRequested) {
                state.config.onQuitRequested();
            }
            break;
        case WM_SYSCOMMAND:
            if ((m.wParam & 0xFFF0u) == SC_KEYMENU) {
                return 0;
            }
            break;
        case WM_DESTROY:
            Win32DestroyTrayIcons(state);
            state.hwnd = nullptr;
            ::PostQuitMessage(0);
            return 0;
        default:
            if (LRESULT sr = 0; Win32HandleShellOrEndSession(state, m.msg, m.wParam, sr)) {
                return sr;
            }
            break;
    }
    return ::DefWindowProcW(m.hWnd, m.msg, m.wParam, m.lParam);
}

LRESULT CALLBACK MainWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == WM_NCCREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
        auto* state = static_cast<Win32WindowState*>(cs->lpCreateParams);
        ::SetWindowLongPtrW(hWnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state));
        state->hwnd = hWnd;
    }

    auto* state = reinterpret_cast<Win32WindowState*>(::GetWindowLongPtrW(hWnd, GWLP_USERDATA));
    if (msg == WM_DROPFILES && state) {
        DispatchDroppedFiles(*state, reinterpret_cast<HDROP>(wParam));
        return 0;
    }
    if (ImGui::GetCurrentContext() != nullptr
        && ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam)) {
        if (state) {
            state->activeFramesRemaining = std::max(state->activeFramesRemaining, 3);
        }
        return TRUE;
    }

    if (state) {
        return HandleMessage(*state, WindowMsg{hWnd, msg, wParam, lParam});
    }
    return ::DefWindowProcW(hWnd, msg, wParam, lParam);
}

}  // namespace

void Win32EnableDpiAwareness() noexcept {
    ImGui_ImplWin32_EnableDpiAwareness();
}

float Win32GetPrimaryMonitorDpiScale() noexcept {
    const float scale =
        ImGui_ImplWin32_GetDpiScaleForMonitor(::MonitorFromPoint(POINT{0, 0}, MONITOR_DEFAULTTOPRIMARY));
    return scale > 0.0f ? scale : 1.0f;
}

float Win32GetWindowDpiScale(void* hwnd) noexcept {
    if (!hwnd) {
        return 1.0f;
    }
    const float scale = ImGui_ImplWin32_GetDpiScaleForMonitor(
        ::MonitorFromWindow(static_cast<HWND>(hwnd), MONITOR_DEFAULTTONEAREST));
    return scale > 0.0f ? scale : 1.0f;
}

motrix::core::Result<void> Win32CreateWindow(Win32WindowState& state) {
    HINSTANCE hInst = ::GetModuleHandleW(nullptr);
    state.hInstance = hInst;

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.style = CS_CLASSDC | CS_DBLCLKS;
    wc.lpfnWndProc = MainWndProc;
    wc.hInstance = hInst;
    wc.hCursor = ::LoadCursorW(nullptr, IDC_ARROW);
    wc.lpszClassName = kWindowClassName;
    ::RegisterClassExW(&wc);

    RECT workArea{};
    ::SystemParametersInfoW(SPI_GETWORKAREA, 0, &workArea, 0);
    const int logicalWorkW =
        static_cast<int>(static_cast<float>(workArea.right - workArea.left) / state.dpiScale);
    const int logicalWorkH =
        static_cast<int>(static_cast<float>(workArea.bottom - workArea.top) / state.dpiScale);
    const float wScale = (logicalWorkW > 0 && logicalWorkW < 1280) ? 0.875f : 1.0f;
    const float hScale = (logicalWorkH > 0 && logicalWorkH < 800) ? 0.875f : 1.0f;

    const int winW = static_cast<int>(static_cast<float>(state.config.width) * wScale * state.dpiScale);
    const int winH = static_cast<int>(static_cast<float>(state.config.height) * hScale * state.dpiScale);
    const int winX = workArea.left + std::max<LONG>(0, ((workArea.right - workArea.left) - winW) / 2);
    const int winY = workArea.top + std::max<LONG>(0, ((workArea.bottom - workArea.top) - winH) / 2);

    const std::wstring wideTitle = motrix::core::Utf8ToWide(state.config.title);
    HWND created = ::CreateWindowExW(0,
                                     kWindowClassName,
                                     wideTitle.c_str(),
                                     WS_OVERLAPPEDWINDOW,
                                     winX,
                                     winY,
                                     winW,
                                     winH,
                                     nullptr,
                                     nullptr,
                                     hInst,
                                     &state);
    if (!created) {
        ::UnregisterClassW(kWindowClassName, hInst);
        return std::unexpected(motrix::core::MakeError(motrix::core::ErrorDomain::Platform,
                                                       motrix::core::ErrorCode::IoFailure,
                                                       "Failed to create Win32 window"));
    }
    state.hwnd = created;
    Win32RegisterShellMessages(state);
    ::DragAcceptFiles(created, TRUE);
    Win32ApplyDwmChrome(created, state.darkFrame);
    Win32ApplyInitialWindowBounds(created, state.config, state.dpiScale);
    state.dpiScale = Win32GetWindowDpiScale(created);
    Win32SetWindowIcons(state);
    ::SetWindowPos(
        created, nullptr, 0, 0, 0, 0, SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER);
    return {};
}

void Win32DestroyWindow(Win32WindowState& state) noexcept {
    Win32DestroyTrayIcons(state);
    if (state.hwnd) {
        ::DestroyWindow(static_cast<HWND>(state.hwnd));
        state.hwnd = nullptr;
    }
    if (state.hInstance) {
        ::UnregisterClassW(kWindowClassName, static_cast<HINSTANCE>(state.hInstance));
        state.hInstance = nullptr;
    }
}

void Win32ShowWindow(void* hwnd, bool show, bool activate) noexcept {
    if (!hwnd) {
        return;
    }
    HWND h = static_cast<HWND>(hwnd);
    if (!show) {
        ::ShowWindow(h, SW_HIDE);
        return;
    }
    int showCmd = activate ? SW_SHOW : SW_SHOWNOACTIVATE;
    if (::IsIconic(h)) {
        showCmd = activate ? SW_RESTORE : SW_SHOWNOACTIVATE;
    } else if (::IsZoomed(h) && activate) {
        showCmd = SW_SHOWMAXIMIZED;
    }
    ::ShowWindow(h, showCmd);
    if (activate) {
        ::SetForegroundWindow(h);
    }
    ::UpdateWindow(h);
}

void Win32MinimizeWindow(void* hwnd) noexcept {
    if (hwnd)
        ::ShowWindow(static_cast<HWND>(hwnd), SW_MINIMIZE);
}

void Win32ToggleMaximizeWindow(void* hwnd) noexcept {
    if (hwnd) {
        HWND h = static_cast<HWND>(hwnd);
        ::ShowWindow(h, ::IsZoomed(h) ? SW_RESTORE : SW_MAXIMIZE);
    }
}

void Win32PostCloseWindow(void* hwnd) noexcept {
    if (hwnd)
        ::PostMessageW(static_cast<HWND>(hwnd), WM_CLOSE, 0, 0);
}

void Win32ApplyDwmChrome(void* hwnd, bool dark) noexcept {
    if (!hwnd) {
        return;
    }
    HWND h = static_cast<HWND>(hwnd);
    MARGINS margins = {0, 0, 0, 1};
    ::DwmExtendFrameIntoClientArea(h, &margins);

    const BOOL useDark = dark ? TRUE : FALSE;
    ::DwmSetWindowAttribute(h, kDwmUseImmersiveDarkMode, &useDark, sizeof(useDark));

    const int cornerPref = kDwmRoundCorner;
    ::DwmSetWindowAttribute(h, kDwmWindowCornerPreference, &cornerPref, sizeof(cornerPref));
}

}  // namespace motrix::platform

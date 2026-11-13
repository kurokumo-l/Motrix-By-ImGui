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
    switch (msg) {
        case WM_PAINT:
        case WM_MOUSEMOVE:
        case WM_NCMOUSEMOVE:
        case WM_MOUSELEAVE:
        case WM_NCMOUSELEAVE:
        case WM_LBUTTONDOWN:
        case WM_LBUTTONUP:
        case WM_LBUTTONDBLCLK:
        case WM_RBUTTONDOWN:
        case WM_RBUTTONUP:
        case WM_MBUTTONDOWN:
        case WM_MBUTTONUP:
        case WM_MOUSEWHEEL:
        case WM_MOUSEHWHEEL:
        case WM_KEYDOWN:
        case WM_KEYUP:
        case WM_SYSKEYDOWN:
        case WM_SYSKEYUP:
        case WM_CHAR:
        case WM_IME_CHAR:
        case WM_IME_COMPOSITION:
        case WM_IME_STARTCOMPOSITION:
        case WM_IME_ENDCOMPOSITION:
        case WM_SETFOCUS:
        case WM_KILLFOCUS:
        case WM_SIZE:
        case WM_DPICHANGED:
        case WM_ACTIVATE:
        case WM_SETCURSOR:
        case WM_DROPFILES:
        case kWmAppWakeup: return true;
        default: return false;
    }
}

[[nodiscard]] LRESULT HitTestBorder(POINT pt, const RECT& rc, int border) noexcept {
    const bool left = pt.x < border;
    const bool right = pt.x >= rc.right - border;
    const bool top = pt.y < border;
    const bool bottom = pt.y >= rc.bottom - border;

    if (top && left)
        return HTTOPLEFT;
    if (top && right)
        return HTTOPRIGHT;
    if (bottom && left)
        return HTBOTTOMLEFT;
    if (bottom && right)
        return HTBOTTOMRIGHT;
    if (top)
        return HTTOP;
    if (bottom)
        return HTBOTTOM;
    if (left)
        return HTLEFT;
    if (right)
        return HTRIGHT;
    return HTCLIENT;
}

WNDPROC gPrevWndProcForDrop = nullptr;
Win32WindowState* gDropWindowState = nullptr;

void DispatchDroppedFiles(Win32WindowState* state, HDROP hDrop) {
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
    if (state && !paths.empty() && state->config.onFilesDropped) {
        state->config.onFilesDropped(std::move(paths));
        state->activeFramesRemaining = std::max(state->activeFramesRemaining, 3);
    }
}

LRESULT CALLBACK DropAwareWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == WM_DROPFILES) {
        DispatchDroppedFiles(gDropWindowState, reinterpret_cast<HDROP>(wParam));
        return 0;
    }
    if (gPrevWndProcForDrop) {
        return ::CallWindowProcW(gPrevWndProcForDrop, hWnd, msg, wParam, lParam);
    }
    return ::DefWindowProcW(hWnd, msg, wParam, lParam);
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
        if (ImGui::GetCurrentContext() && ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId)) {
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

LRESULT HandleMessage(Win32WindowState& state, HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (ShouldWakeFrameForMessage(msg)) {
        state.activeFramesRemaining = std::max(state.activeFramesRemaining, 3);
    }

    switch (msg) {
        case WM_NCCALCSIZE: {
            if (wParam == TRUE && ::IsZoomed(hWnd)) {
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
            return (wParam == TRUE) ? 0 : ::DefWindowProcW(hWnd, msg, wParam, lParam);
        }
        case WM_NCHITTEST: return HandleNcHitTest(state, hWnd, lParam);
        case WM_GETMINMAXINFO: {
            auto* mmi = reinterpret_cast<MINMAXINFO*>(lParam);
            mmi->ptMinTrackSize.x =
                static_cast<LONG>(static_cast<float>(state.config.minWidth) * state.dpiScale);
            mmi->ptMinTrackSize.y =
                static_cast<LONG>(static_cast<float>(state.config.minHeight) * state.dpiScale);
            return 0;
        }
        case WM_DPICHANGED: {
            const auto* s = reinterpret_cast<const RECT*>(lParam);
            ::SetWindowPos(hWnd,
                           nullptr,
                           s->left,
                           s->top,
                           s->right - s->left,
                           s->bottom - s->top,
                           SWP_NOZORDER | SWP_NOACTIVATE);
            return 0;
        }
        case WM_SIZE:
            if (wParam != SIZE_MINIMIZED) {
                state.pendingResizeW = static_cast<std::uint32_t>(LOWORD(lParam));
                state.pendingResizeH = static_cast<std::uint32_t>(HIWORD(lParam));
                if ((wParam == SIZE_MAXIMIZED || wParam == SIZE_RESTORED) && state.config.onBoundsChanged
                    && ::IsWindowVisible(hWnd)) {
                    state.config.onBoundsChanged(Win32ReadWindowBounds(hWnd));
                }
            }
            return 0;
        case WM_EXITSIZEMOVE:
            if (state.config.onBoundsChanged && !::IsIconic(hWnd)) {
                state.config.onBoundsChanged(Win32ReadWindowBounds(hWnd));
            }
            return 0;
        case WM_ACTIVATE:
            if (LOWORD(wParam) == WA_INACTIVE && state.autoHideOnBlur
                && state.config.screenshotPath.empty() && ::IsWindowVisible(hWnd) && !::IsIconic(hWnd)) {
                HWND next = reinterpret_cast<HWND>(lParam);
                DWORD nextPid = 0;
                if (next) {
                    ::GetWindowThreadProcessId(next, &nextPid);
                }
                if (!next || nextPid != ::GetCurrentProcessId()) {
                    Win32ShowWindow(hWnd, false, false);
                    state.hiddenToTray = true;
                }
            }
            break;
        case kWmTrayCallback:
            Win32HandleTrayCallback(state, static_cast<std::uint64_t>(lParam));
            return 0;
        case WM_CLOSE:
            if (state.config.onBoundsChanged) {
                state.config.onBoundsChanged(Win32ReadWindowBounds(hWnd));
            }
            if (state.bindCloseToHide && !state.explicitQuitRequested) {
                Win32ShowWindow(hWnd, false, false);
                state.hiddenToTray = true;
                return 0;
            }
            if (state.config.onQuitRequested) {
                state.config.onQuitRequested();
            }
            break;
        case WM_SYSCOMMAND:
            if ((wParam & 0xFFF0u) == SC_KEYMENU) {
                return 0;
            }
            break;
        case WM_DESTROY: ::PostQuitMessage(0); return 0;
        default: break;
    }
    return ::DefWindowProcW(hWnd, msg, wParam, lParam);
}

LRESULT CALLBACK MainWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == WM_NCCREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
        auto* state = static_cast<Win32WindowState*>(cs->lpCreateParams);
        ::SetWindowLongPtrW(hWnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state));
        state->hwnd = hWnd;
    }

    auto* state = reinterpret_cast<Win32WindowState*>(::GetWindowLongPtrW(hWnd, GWLP_USERDATA));
    if (ImGui::GetCurrentContext() != nullptr
        && ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam)) {
        if (state) {
            state->activeFramesRemaining = std::max(state->activeFramesRemaining, 3);
        }
        return TRUE;
    }

    if (state) {
        return HandleMessage(*state, hWnd, msg, wParam, lParam);
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
    gDropWindowState = &state;
    ::DragAcceptFiles(created, TRUE);
    const LONG_PTR prevProc =
        ::SetWindowLongPtrW(created, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(DropAwareWndProc));
    if (prevProc && prevProc != reinterpret_cast<LONG_PTR>(DropAwareWndProc)) {
        gPrevWndProcForDrop = reinterpret_cast<WNDPROC>(prevProc);
    }
    Win32ApplyDwmChrome(created, state.darkFrame);
    Win32ApplyInitialWindowBounds(created, state.config, state.dpiScale);
    state.dpiScale = Win32GetWindowDpiScale(created);
    Win32SetWindowIcons(state);
    ::SetWindowPos(
        created, nullptr, 0, 0, 0, 0, SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER);
    return {};
}

void Win32DestroyWindow(Win32WindowState& state) noexcept {
    if (gDropWindowState == &state) {
        gDropWindowState = nullptr;
    }
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
    if (hwnd) {
        ::ShowWindow(static_cast<HWND>(hwnd), SW_MINIMIZE);
    }
}

void Win32ToggleMaximizeWindow(void* hwnd) noexcept {
    if (hwnd) {
        HWND h = static_cast<HWND>(hwnd);
        ::ShowWindow(h, ::IsZoomed(h) ? SW_RESTORE : SW_MAXIMIZE);
    }
}

void Win32PostCloseWindow(void* hwnd) noexcept {
    if (hwnd) {
        ::PostMessageW(static_cast<HWND>(hwnd), WM_CLOSE, 0, 0);
    }
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

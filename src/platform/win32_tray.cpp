module;

// clang-format off
#include <windows.h>
#include <shellapi.h>
// clang-format on

module motrix.platform;

import std;
import motrix.core;
import :window_host;
import :win32;
import :win32_internal;

namespace motrix::platform {

namespace {

inline HDC AcquireScreenDc() noexcept {
    return ::GetDC(nullptr);
}
inline void ReleaseScreenDc(HDC hdc) noexcept {
    ::ReleaseDC(nullptr, hdc);
}
inline HBITMAP NullBitmap() noexcept {
    return nullptr;
}
inline void FreeBitmap(HBITMAP bmp) noexcept {
    ::DeleteObject(bmp);
}
inline HMENU AcquirePopupMenu() noexcept {
    return ::CreatePopupMenu();
}
inline void FreePopupMenu(HMENU menu) noexcept {
    ::DestroyMenu(menu);
}

template <typename H, auto InitFn, auto FreeFn> class UniqueTrayRes {
public:
    UniqueTrayRes() noexcept : h_(InitFn()) {}
    explicit UniqueTrayRes(H h) noexcept : h_(h) {}
    ~UniqueTrayRes() {
        if (h_)
            FreeFn(h_);
    }
    UniqueTrayRes(const UniqueTrayRes&) = delete;
    UniqueTrayRes& operator=(const UniqueTrayRes&) = delete;
    [[nodiscard]] H Get() const noexcept { return h_; }

private:
    H h_ = nullptr;
};

using UniqueScreenDc = UniqueTrayRes<HDC, AcquireScreenDc, ReleaseScreenDc>;
using UniqueGdiBitmap = UniqueTrayRes<HBITMAP, NullBitmap, FreeBitmap>;
using UniqueMenu = UniqueTrayRes<HMENU, AcquirePopupMenu, FreePopupMenu>;

[[nodiscard]] float
SdRoundedBox(float px, float py, float cx, float cy, float hx, float hy, float r) noexcept {
    const float dx = std::abs(px - cx) - (hx - r);
    const float dy = std::abs(py - cy) - (hy - r);
    return std::hypot(std::max(dx, 0.0f), std::max(dy, 0.0f)) + std::min(std::max(dx, dy), 0.0f) - r;
}

[[nodiscard]] float CovFromDist(float dist, float aa) noexcept {
    return std::clamp(0.5f - dist / aa, 0.0f, 1.0f);
}

struct FloatRgba {
    float r = 0.0f;
    float g = 0.0f;
    float b = 0.0f;
    float a = 0.0f;
};

void BlendOver(FloatRgba& dst, const FloatRgba& src, float cov) noexcept {
    if (cov <= 0.0f) {
        return;
    }
    const float inv = 1.0f - cov;
    dst.r = src.r * cov + dst.r * inv;
    dst.g = src.g * cov + dst.g * inv;
    dst.b = src.b * cov + dst.b * inv;
    dst.a = cov + dst.a * inv;
}

[[nodiscard]] std::uint32_t PackPremulBgra(const FloatRgba& c) noexcept {
    const auto toByte = [](float v) noexcept -> std::uint32_t {
        return static_cast<std::uint32_t>(std::clamp(std::lround(v * 255.0f), 0L, 255L));
    };
    const std::uint32_t ca = toByte(c.a);
    const std::uint32_t cr = toByte(c.r);
    const std::uint32_t cg = toByte(c.g);
    const std::uint32_t cb = toByte(c.b);
    return (ca << 24u) | (cr << 16u) | (cg << 8u) | cb;
}

struct IconSample {
    float px = 0.0f;
    float py = 0.0f;
    float aa = 0.03f;
    bool darkTaskbar = true;
};

[[nodiscard]] HICON TryLoadMotrixNativeIcon(int sizePx, MotrixIconKind kind) noexcept {
    const NativeIconResource spec = ResolveMotrixIconResource(kind);
    const auto load = [&](HINSTANCE mod, LPCWSTR name, UINT flags) noexcept {
        return static_cast<HICON>(::LoadImageW(mod, name, IMAGE_ICON, sizePx, sizePx, flags));
    };
    if (HICON res =
            load(::GetModuleHandleW(nullptr), MAKEINTRESOURCEW(spec.resourceId), LR_DEFAULTCOLOR)) {
        return res;
    }
    const auto exePath = motrix::core::CurrentExecutablePath();
    if (exePath.empty()) {
        return nullptr;
    }
    const auto p = exePath.parent_path() / L"assets" / L"icons" / spec.fileName;
    return std::filesystem::exists(p) ? load(nullptr, p.c_str(), LR_LOADFROMFILE | LR_DEFAULTCOLOR)
                                      : nullptr;
}

[[nodiscard]] FloatRgba EvaluateMotrixIconPixel(const IconSample& s, MotrixIconKind kind) noexcept {
    const auto covBox = [&](float cx, float cy, float hx, float hy, float r) noexcept {
        return CovFromDist(SdRoundedBox(s.px, s.py, cx, cy, hx, hy, r), s.aa);
    };
    const float bodyCov = covBox(0.50f, 0.50f, 0.405f, 0.405f, 0.145f);
    const float t = std::clamp((s.py - 0.10f) / 0.80f, 0.0f, 1.0f);
    const float rim = s.darkTaskbar ? 1.0f : 0.94f;
    FloatRgba out{};
    BlendOver(out,
              {(0.54f - 0.18f * t) * rim, (0.43f - 0.09f * t) * rim, (0.91f - 0.07f * t) * rim, 1.0f},
              bodyCov);
    BlendOver(
        out, {0.85f, 0.86f, 0.94f, 1.0f}, covBox(0.50f, 0.135f, 0.29f, 0.035f, 0.02f) * bodyCov * 0.75f);
    BlendOver(out, {0.94f, 0.95f, 0.98f, 1.0f}, covBox(0.50f, 0.44f, 0.215f, 0.165f, 0.055f) * bodyCov);
    const float visorCov = covBox(0.50f, 0.315f, 0.195f, 0.055f, 0.025f) * bodyCov;
    BlendOver(out, {0.14f, 0.14f, 0.16f, 1.0f}, visorCov);
    const float eyes = std::max(covBox(0.385f, 0.315f, 0.036f, 0.020f, 0.01f),
                                covBox(0.615f, 0.315f, 0.036f, 0.020f, 0.01f));
    BlendOver(out, {0.96f, 0.78f, 0.28f, 1.0f}, eyes * visorCov);
    BlendOver(out, {0.90f, 0.91f, 0.96f, 1.0f}, covBox(0.50f, 0.68f, 0.135f, 0.022f, 0.01f) * bodyCov);
    if (kind == MotrixIconKind::TrayActive) {
        const float dDot = std::hypot(s.px - 0.8125f, s.py - 0.156f);
        BlendOver(out, {0.24f, 0.86f, 0.36f, 1.0f}, CovFromDist(dDot - 0.125f, s.aa));
    }
    return out;
}

[[nodiscard]] HICON CreateMotrixHicon(int sizePx, MotrixIconKind kind, bool darkTaskbar) noexcept {
    if (sizePx <= 0) {
        return nullptr;
    }
    if (HICON nativeIcon = TryLoadMotrixNativeIcon(sizePx, kind)) {
        return nativeIcon;
    }
    UniqueScreenDc screenDc;
    if (!screenDc.Get())
        return nullptr;

    BITMAPV5HEADER bi{.bV5Size = sizeof(BITMAPV5HEADER),
                      .bV5Width = sizePx,
                      .bV5Height = -sizePx,
                      .bV5Planes = 1,
                      .bV5BitCount = 32,
                      .bV5Compression = BI_BITFIELDS,
                      .bV5RedMask = 0x00FF0000u,
                      .bV5GreenMask = 0x0000FF00u,
                      .bV5BlueMask = 0x000000FFu,
                      .bV5AlphaMask = 0xFF000000u};

    void* rawBits = nullptr;
    UniqueGdiBitmap colorBmp(::CreateDIBSection(
        screenDc.Get(), reinterpret_cast<const BITMAPINFO*>(&bi), DIB_RGB_COLORS, &rawBits, nullptr, 0));
    const int strideBytes = ((sizePx + 15) / 16) * 2;
    std::vector<std::uint8_t> maskBits(
        static_cast<std::size_t>(strideBytes) * static_cast<std::size_t>(sizePx), 0u);
    UniqueGdiBitmap maskBmp(::CreateBitmap(sizePx, sizePx, 1, 1, maskBits.data()));
    if (!colorBmp.Get() || !rawBits || !maskBmp.Get())
        return nullptr;

    const std::vector<std::uint32_t> bgra = RenderMotrixIconBgra(kind, sizePx, darkTaskbar);
    std::memcpy(rawBits, bgra.data(), bgra.size() * sizeof(std::uint32_t));

    ICONINFO ii{TRUE, 0, 0, maskBmp.Get(), colorBmp.Get()};
    return ::CreateIconIndirect(&ii);
}

void ReleaseTrayCachedIcons(Win32WindowState& state) noexcept {
    if (state.trayIdleIcon) {
        ::DestroyIcon(static_cast<HICON>(state.trayIdleIcon));
        state.trayIdleIcon = nullptr;
    }
    if (state.trayActiveIcon) {
        ::DestroyIcon(static_cast<HICON>(state.trayActiveIcon));
        state.trayActiveIcon = nullptr;
    }
}

void EnsureTrayIconsCreated(Win32WindowState& state) noexcept {
    const UINT dpi = state.hwnd ? ::GetDpiForWindow(static_cast<HWND>(state.hwnd)) : 96u;
    if (state.trayDpi != dpi) {
        ReleaseTrayCachedIcons(state);
        state.trayDpi = dpi;
    }
    const int trayPx = std::clamp(::GetSystemMetricsForDpi(SM_CXSMICON, dpi), 16, 64);
    if (!state.trayIdleIcon) {
        state.trayIdleIcon = CreateMotrixHicon(trayPx, MotrixIconKind::TrayIdle, state.trayDarkTaskbar);
    }
    if (!state.trayActiveIcon) {
        state.trayActiveIcon =
            CreateMotrixHicon(trayPx, MotrixIconKind::TrayActive, state.trayDarkTaskbar);
    }
}

void ShowTrayContextMenu(Win32WindowState& state, HWND hwnd) {
    UniqueMenu menu;
    if (!menu.Get()) {
        return;
    }

    const bool zh = state.trayIsZhCn;
    const auto addItem = [&](TrayMenuAction action, const wchar_t* en, const wchar_t* zhText) {
        ::AppendMenuW(menu.Get(), MF_STRING, static_cast<UINT_PTR>(action), zh ? zhText : en);
    };

    addItem(TrayMenuAction::NewTask, L"New Task", L"\x65B0\x5EFA\x4EFB\x52A1");
    addItem(TrayMenuAction::NewBtTask, L"New BT Task", L"\x65B0\x5EFA BT \x4EFB\x52A1");
    addItem(TrayMenuAction::OpenTorrentFile,
            L"Open Torrent File...",
            L"\x6253\x5F00\x79CD\x5B50\x6587\x4EF6...");
    ::AppendMenuW(menu.Get(), MF_SEPARATOR, 0, nullptr);
    addItem(TrayMenuAction::ResumeAll, L"Resume All Tasks", L"\x6062\x590D\x6240\x6709\x4EFB\x52A1");
    addItem(TrayMenuAction::PauseAll, L"Pause All Tasks", L"\x6682\x505C\x6240\x6709\x4EFB\x52A1");
    addItem(TrayMenuAction::ShowWindow, L"Show Motrix", L"\x663E\x793A Motrix");
    ::AppendMenuW(menu.Get(), MF_SEPARATOR, 0, nullptr);
    addItem(TrayMenuAction::Tasks, L"Tasks", L"\x4EFB\x52A1\x5217\x8868");
    addItem(TrayMenuAction::Preferences, L"Preferences...", L"\x504F\x597D\x8BBE\x7F6E...");
    addItem(TrayMenuAction::Quit, L"Quit Motrix", L"\x9000\x51FA Motrix");

    POINT pt{};
    ::GetCursorPos(&pt);
    ::SetForegroundWindow(hwnd);
    const UINT picked = static_cast<UINT>(::TrackPopupMenuEx(
        menu.Get(), TPM_RIGHTBUTTON | TPM_NONOTIFY | TPM_RETURNCMD, pt.x, pt.y, hwnd, nullptr));
    ::PostMessageW(hwnd, WM_NULL, 0, 0);

    if (picked >= static_cast<UINT>(TrayMenuAction::ShowWindow)
        && picked <= static_cast<UINT>(TrayMenuAction::Quit) && state.config.onTrayAction) {
        state.config.onTrayAction(static_cast<TrayMenuAction>(picked));
        state.activeFramesRemaining = std::max(state.activeFramesRemaining, 3);
    }
}

}  // namespace

void Win32SetWindowIcons(Win32WindowState& state) noexcept {
    if (!state.hwnd) {
        return;
    }
    HWND hwnd = static_cast<HWND>(state.hwnd);
    const UINT dpi = ::GetDpiForWindow(hwnd);
    const std::array<std::pair<WPARAM, int>, 2> specs{{
        {ICON_BIG, std::clamp(::GetSystemMetricsForDpi(SM_CXICON, dpi), 32, 64)},
        {ICON_SMALL, std::clamp(::GetSystemMetricsForDpi(SM_CXSMICON, dpi), 16, 64)},
    }};
    for (const auto& [slot, px] : specs) {
        if (HICON icon = CreateMotrixHicon(px, MotrixIconKind::AppBadge, true)) {
            if (const LRESULT prev =
                    ::SendMessageW(hwnd, WM_SETICON, slot, reinterpret_cast<LPARAM>(icon));
                prev != 0) {
                ::DestroyIcon(reinterpret_cast<HICON>(prev));
            }
        }
    }
}

void Win32RefreshIconsOnThemeOrDpi(Win32WindowState& state) noexcept {
    const bool nextTaskbarDark = QueryTaskbarDarkMode();
    if (nextTaskbarDark != state.trayDarkTaskbar) {
        state.trayDarkTaskbar = nextTaskbarDark;
        ReleaseTrayCachedIcons(state);
    }
    Win32SetWindowIcons(state);
    if (state.enableTrayIcon && state.config.screenshotPath.empty()) {
        Win32InstallOrUpdateTray(state);
    }
}

void Win32RemoveTray(Win32WindowState& state) noexcept {
    if (!state.trayInstalled || !state.hwnd) {
        state.trayInstalled = false;
        return;
    }
    NOTIFYICONDATAW nid{};
    nid.cbSize = sizeof(NOTIFYICONDATAW);
    nid.hWnd = static_cast<HWND>(state.hwnd);
    nid.uID = 1;
    ::Shell_NotifyIconW(NIM_DELETE, &nid);
    state.trayInstalled = false;
}

void Win32DestroyTrayIcons(Win32WindowState& state) noexcept {
    Win32RemoveTray(state);
    if (state.hwnd && ::IsWindow(static_cast<HWND>(state.hwnd))) {
        HWND hwnd = static_cast<HWND>(state.hwnd);
        for (WPARAM slot : {ICON_BIG, ICON_SMALL}) {
            if (const LRESULT prev = ::SendMessageW(hwnd, WM_SETICON, slot, 0); prev != 0) {
                ::DestroyIcon(reinterpret_cast<HICON>(prev));
            }
        }
    }
    ReleaseTrayCachedIcons(state);
}

void Win32InstallOrUpdateTray(Win32WindowState& state) noexcept {
    if (!state.hwnd) {
        return;
    }
    if (!state.enableTrayIcon) {
        Win32RemoveTray(state);
        return;
    }

    EnsureTrayIconsCreated(state);
    NOTIFYICONDATAW nid{};
    nid.cbSize = sizeof(NOTIFYICONDATAW);
    nid.hWnd = static_cast<HWND>(state.hwnd);
    nid.uID = 1;
    nid.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    nid.uCallbackMessage = kWmTrayCallback;
    nid.hIcon =
        static_cast<HICON>(state.trayActiveDownloads ? state.trayActiveIcon : state.trayIdleIcon);
    std::wstring tipW;
    if (!state.trayTooltipUtf8.empty()) {
        tipW = motrix::core::Utf8ToWide(state.trayTooltipUtf8);
    }
    const wchar_t* tip =
        !tipW.empty()
            ? tipW.c_str()
            : (state.trayActiveDownloads
                   ? (state.trayIsZhCn ? L"Motrix - \x4E0B\x8F7D\x4E2D" : L"Motrix - Downloading")
                   : L"Motrix");
    ::wcsncpy_s(nid.szTip, std::size(nid.szTip), tip, _TRUNCATE);

    if (state.trayInstalled) {
        if (::Shell_NotifyIconW(NIM_MODIFY, &nid) == FALSE) {
            state.trayInstalled = (::Shell_NotifyIconW(NIM_ADD, &nid) != FALSE);
        }
    } else if (::Shell_NotifyIconW(NIM_ADD, &nid) != FALSE) {
        state.trayInstalled = true;
    }
}

void Win32ShowTrayBalloon(Win32WindowState& state,
                          std::string_view title,
                          std::string_view body,
                          std::string_view clickTargetDir) {
    state.lastToastTargetDir = std::string(clickTargetDir);
    if (!state.hwnd || !state.trayInstalled) {
        return;
    }
    EnsureTrayIconsCreated(state);
    const std::wstring wideTitle = motrix::core::Utf8ToWide(title);
    const std::wstring wideBody = motrix::core::Utf8ToWide(body);
    NOTIFYICONDATAW nid{};
    nid.cbSize = sizeof(NOTIFYICONDATAW);
    nid.hWnd = static_cast<HWND>(state.hwnd);
    nid.uID = 1;
    nid.uFlags = NIF_INFO;
    nid.dwInfoFlags = NIIF_NONE;
    ::wcsncpy_s(nid.szInfoTitle, std::size(nid.szInfoTitle), wideTitle.c_str(), _TRUNCATE);
    ::wcsncpy_s(nid.szInfo, std::size(nid.szInfo), wideBody.c_str(), _TRUNCATE);
    ::Shell_NotifyIconW(NIM_MODIFY, &nid);
}

void ShowWindowFromTrayInteraction(Win32WindowState& state, HWND hwnd) {
    if (state.config.onTrayAction) {
        state.config.onTrayAction(TrayMenuAction::ShowWindow);
    } else {
        Win32ShowWindow(hwnd, true, true);
        state.hiddenToTray = false;
    }
    state.activeFramesRemaining = std::max(state.activeFramesRemaining, 3);
}

void Win32HandleTrayCallback(Win32WindowState& state, std::uint64_t lParam) {
    if (!state.hwnd) {
        return;
    }
    HWND hwnd = static_cast<HWND>(state.hwnd);
    const UINT msgId = static_cast<UINT>(LOWORD(static_cast<DWORD_PTR>(lParam)));
    switch (msgId) {
        case WM_LBUTTONUP: {
            if (state.hiddenToTray || !::IsWindowVisible(hwnd) || ::IsIconic(hwnd)) {
                ShowWindowFromTrayInteraction(state, hwnd);
            } else {
                Win32ShowWindow(hwnd, false, false);
                state.hiddenToTray = true;
                state.trimRequested = true;
            }
            break;
        }
        case WM_RBUTTONUP:
        case WM_CONTEXTMENU: {
            ShowTrayContextMenu(state, hwnd);
            break;
        }
        case NIN_BALLOONUSERCLICK: {
            if (!state.lastToastTargetDir.empty()) {
                Win32OpenPathInExplorer(state.lastToastTargetDir);
            } else {
                ShowWindowFromTrayInteraction(state, hwnd);
            }
            break;
        }
        default: break;
    }
}

std::vector<std::uint32_t> RenderMotrixIconBgra(MotrixIconKind kind, int sizePx, bool darkTaskbar) {
    if (sizePx <= 0) {
        return {};
    }
    std::vector<std::uint32_t> out(static_cast<std::size_t>(sizePx * sizePx), 0u);
    const float invSize = 1.0f / static_cast<float>(sizePx);
    const float aa = (kind == MotrixIconKind::AppBadge ? 1.05f : 0.90f) * invSize;
    std::size_t idx = 0;
    for (int y = 0; y < sizePx; ++y) {
        const float py = (static_cast<float>(y) + 0.5f) * invSize;
        for (int x = 0; x < sizePx; ++x) {
            const float px = (static_cast<float>(x) + 0.5f) * invSize;
            out[idx++] =
                PackPremulBgra(EvaluateMotrixIconPixel(IconSample{px, py, aa, darkTaskbar}, kind));
        }
    }
    return out;
}

}  // namespace motrix::platform

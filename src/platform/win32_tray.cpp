module;

// clang-format off
#include <windows.h>
#include <shellapi.h>
// clang-format on

module motrix.platform;

import std;
import motrix.core;
import :window_host;
import :win32_internal;

namespace motrix::platform {

namespace {

class UniqueScreenDc {
public:
    UniqueScreenDc() noexcept : hdc_(::GetDC(nullptr)) {}
    ~UniqueScreenDc() {
        if (hdc_) {
            ::ReleaseDC(nullptr, hdc_);
        }
    }
    UniqueScreenDc(const UniqueScreenDc&) = delete;
    UniqueScreenDc& operator=(const UniqueScreenDc&) = delete;
    [[nodiscard]] HDC Get() const noexcept { return hdc_; }

private:
    HDC hdc_ = nullptr;
};

class UniqueGdiBitmap {
public:
    UniqueGdiBitmap() noexcept = default;
    explicit UniqueGdiBitmap(HBITMAP bmp) noexcept : bmp_(bmp) {}
    ~UniqueGdiBitmap() {
        if (bmp_) {
            ::DeleteObject(bmp_);
        }
    }
    UniqueGdiBitmap(const UniqueGdiBitmap&) = delete;
    UniqueGdiBitmap& operator=(const UniqueGdiBitmap&) = delete;
    [[nodiscard]] HBITMAP Get() const noexcept { return bmp_; }

private:
    HBITMAP bmp_ = nullptr;
};

class UniqueMenu {
public:
    UniqueMenu() noexcept : menu_(::CreatePopupMenu()) {}
    ~UniqueMenu() {
        if (menu_) {
            ::DestroyMenu(menu_);
        }
    }
    UniqueMenu(const UniqueMenu&) = delete;
    UniqueMenu& operator=(const UniqueMenu&) = delete;
    [[nodiscard]] HMENU Get() const noexcept { return menu_; }

private:
    HMENU menu_ = nullptr;
};

[[nodiscard]] float
SdRoundedBox(float px, float py, float cx, float cy, float hx, float hy, float r) noexcept {
    const float dx = std::abs(px - cx) - (hx - r);
    const float dy = std::abs(py - cy) - (hy - r);
    return std::hypot(std::max(dx, 0.0f), std::max(dy, 0.0f)) + std::min(std::max(dx, dy), 0.0f) - r;
}

[[nodiscard]] float
SdTriangle(float px, float py, float x0, float y0, float x1, float y1, float x2, float y2) noexcept {
    const auto dot2 = [](float ax, float ay) noexcept { return ax * ax + ay * ay; };
    const auto cross2 = [](float ax, float ay, float bx, float by) noexcept {
        return ax * by - ay * bx;
    };

    const float e0x = x1 - x0;
    const float e0y = y1 - y0;
    const float e1x = x2 - x1;
    const float e1y = y2 - y1;
    const float e2x = x0 - x2;
    const float e2y = y0 - y2;

    const float v0x = px - x0;
    const float v0y = py - y0;
    const float v1x = px - x1;
    const float v1y = py - y1;
    const float v2x = px - x2;
    const float v2y = py - y2;

    const float t0 = std::clamp((v0x * e0x + v0y * e0y) / dot2(e0x, e0y), 0.0f, 1.0f);
    const float t1 = std::clamp((v1x * e1x + v1y * e1y) / dot2(e1x, e1y), 0.0f, 1.0f);
    const float t2 = std::clamp((v2x * e2x + v2y * e2y) / dot2(e2x, e2y), 0.0f, 1.0f);

    const float d0 = dot2(v0x - e0x * t0, v0y - e0y * t0);
    const float d1 = dot2(v1x - e1x * t1, v1y - e1y * t1);
    const float d2 = dot2(v2x - e2x * t2, v2y - e2y * t2);

    const float s = (cross2(e0x, e0y, e2x, e2y) >= 0.0f) ? 1.0f : -1.0f;
    const float c0 = s * cross2(v0x, v0y, e0x, e0y);
    const float c1 = s * cross2(v1x, v1y, e1x, e1y);
    const float c2 = s * cross2(v2x, v2y, e2x, e2y);

    const float minD = std::min({d0, d1, d2});
    const float minC = std::min({c0, c1, c2});
    return (minC >= 0.0f ? -1.0f : 1.0f) * std::sqrt(minD);
}

[[nodiscard]] float CovFromDist(float dist, float aa) noexcept {
    return std::clamp(0.5f - dist / aa, 0.0f, 1.0f);
}

void BlendOver(float& dstR,
               float& dstG,
               float& dstB,
               float& dstA,
               float srcR,
               float srcG,
               float srcB,
               float cov) noexcept {
    if (cov <= 0.0f) {
        return;
    }
    const float inv = 1.0f - cov;
    dstR = srcR * cov + dstR * inv;
    dstG = srcG * cov + dstG * inv;
    dstB = srcB * cov + dstB * inv;
    dstA = cov + dstA * inv;
}

[[nodiscard]] std::uint32_t PackPremulBgra(float r, float g, float b, float a) noexcept {
    const auto toByte = [](float v) noexcept -> std::uint32_t {
        return static_cast<std::uint32_t>(std::clamp(std::lround(v * 255.0f), 0L, 255L));
    };
    const std::uint32_t ca = toByte(a);
    const std::uint32_t cr = toByte(r);
    const std::uint32_t cg = toByte(g);
    const std::uint32_t cb = toByte(b);
    return (ca << 24u) | (cr << 16u) | (cg << 8u) | cb;
}

[[nodiscard]] HICON CreateMotrixHicon(int sizePx, bool activeDot) noexcept {
    if (sizePx <= 0) {
        return nullptr;
    }

    UniqueScreenDc screenDc;
    if (!screenDc.Get()) {
        return nullptr;
    }

    BITMAPV5HEADER bi{};
    bi.bV5Size = sizeof(BITMAPV5HEADER);
    bi.bV5Width = sizePx;
    bi.bV5Height = -sizePx;
    bi.bV5Planes = 1;
    bi.bV5BitCount = 32;
    bi.bV5Compression = BI_BITFIELDS;
    bi.bV5RedMask = 0x00FF0000u;
    bi.bV5GreenMask = 0x0000FF00u;
    bi.bV5BlueMask = 0x000000FFu;
    bi.bV5AlphaMask = 0xFF000000u;

    void* rawBits = nullptr;
    UniqueGdiBitmap colorBmp(::CreateDIBSection(
        screenDc.Get(), reinterpret_cast<const BITMAPINFO*>(&bi), DIB_RGB_COLORS, &rawBits, nullptr, 0));
    if (!colorBmp.Get() || !rawBits) {
        return nullptr;
    }

    const int strideBytes = ((sizePx + 15) / 16) * 2;
    std::vector<std::uint8_t> maskBits(
        static_cast<std::size_t>(strideBytes) * static_cast<std::size_t>(sizePx), 0u);
    UniqueGdiBitmap maskBmp(::CreateBitmap(sizePx, sizePx, 1, 1, maskBits.data()));
    if (!maskBmp.Get()) {
        return nullptr;
    }

    auto* pixels = static_cast<std::uint32_t*>(rawBits);
    const float invSize = 1.0f / static_cast<float>(sizePx);
    const float aa = 1.15f * invSize;

    // Brand indigo #5B5CFD and active emerald #22C55E
    constexpr float kBrandR = 91.0f / 255.0f;
    constexpr float kBrandG = 92.0f / 255.0f;
    constexpr float kBrandB = 253.0f / 255.0f;
    constexpr float kDotR = 34.0f / 255.0f;
    constexpr float kDotG = 197.0f / 255.0f;
    constexpr float kDotB = 94.0f / 255.0f;

    for (int y = 0; y < sizePx; ++y) {
        for (int x = 0; x < sizePx; ++x) {
            const float px = (static_cast<float>(x) + 0.5f) * invSize;
            const float py = (static_cast<float>(y) + 0.5f) * invSize;

            const float bgDist = SdRoundedBox(px, py, 0.50f, 0.50f, 0.44f, 0.44f, 0.16f);
            const float bgCov = CovFromDist(bgDist, aa);

            const float shaftDist = SdRoundedBox(px, py, 0.50f, 0.36f, 0.075f, 0.16f, 0.03f);
            const float triDist = SdTriangle(px, py, 0.27f, 0.45f, 0.73f, 0.45f, 0.50f, 0.67f) - 0.012f;
            const float barDist = SdRoundedBox(px, py, 0.50f, 0.76f, 0.23f, 0.045f, 0.025f);
            const float arrowCov = CovFromDist(std::min({shaftDist, triDist, barDist}), aa) * bgCov;

            float outR = 0.0f;
            float outG = 0.0f;
            float outB = 0.0f;
            float outA = 0.0f;
            BlendOver(outR, outG, outB, outA, kBrandR, kBrandG, kBrandB, bgCov);
            BlendOver(outR, outG, outB, outA, 1.0f, 1.0f, 1.0f, arrowCov);

            if (activeDot) {
                const float dDot = std::hypot(px - 0.76f, py - 0.76f);
                const float ringCov = CovFromDist(dDot - 0.22f, aa);
                const float dotCov = CovFromDist(dDot - 0.16f, aa);
                BlendOver(outR, outG, outB, outA, 1.0f, 1.0f, 1.0f, ringCov);
                BlendOver(outR, outG, outB, outA, kDotR, kDotG, kDotB, dotCov);
            }

            pixels[static_cast<std::size_t>(y) * static_cast<std::size_t>(sizePx)
                   + static_cast<std::size_t>(x)] = PackPremulBgra(outR, outG, outB, outA);
        }
    }

    ICONINFO ii{};
    ii.fIcon = TRUE;
    ii.hbmMask = maskBmp.Get();
    ii.hbmColor = colorBmp.Get();
    return ::CreateIconIndirect(&ii);
}

void EnsureTrayIconsCreated(Win32WindowState& state) noexcept {
    const UINT dpi = state.hwnd ? ::GetDpiForWindow(static_cast<HWND>(state.hwnd)) : 96u;
    const int trayPx = std::clamp(::GetSystemMetricsForDpi(SM_CXSMICON, dpi), 16, 64);
    if (!state.trayIdleIcon) {
        state.trayIdleIcon = CreateMotrixHicon(trayPx, false);
    }
    if (!state.trayActiveIcon) {
        state.trayActiveIcon = CreateMotrixHicon(trayPx, true);
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
    const int bigPx = std::clamp(::GetSystemMetricsForDpi(SM_CXICON, dpi), 32, 64);
    const int smallPx = std::clamp(::GetSystemMetricsForDpi(SM_CXSMICON, dpi), 16, 64);

    if (HICON bigIcon = CreateMotrixHicon(bigPx, false)) {
        const LRESULT prev =
            ::SendMessageW(hwnd, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(bigIcon));
        if (prev != 0) {
            ::DestroyIcon(reinterpret_cast<HICON>(prev));
        }
    }
    if (HICON smallIcon = CreateMotrixHicon(smallPx, false)) {
        const LRESULT prev =
            ::SendMessageW(hwnd, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(smallIcon));
        if (prev != 0) {
            ::DestroyIcon(reinterpret_cast<HICON>(prev));
        }
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
        if (const LRESULT prevBig = ::SendMessageW(hwnd, WM_SETICON, ICON_BIG, 0); prevBig != 0) {
            ::DestroyIcon(reinterpret_cast<HICON>(prevBig));
        }
        if (const LRESULT prevSmall = ::SendMessageW(hwnd, WM_SETICON, ICON_SMALL, 0); prevSmall != 0) {
            ::DestroyIcon(reinterpret_cast<HICON>(prevSmall));
        }
    }
    if (state.trayIdleIcon) {
        ::DestroyIcon(static_cast<HICON>(state.trayIdleIcon));
        state.trayIdleIcon = nullptr;
    }
    if (state.trayActiveIcon) {
        ::DestroyIcon(static_cast<HICON>(state.trayActiveIcon));
        state.trayActiveIcon = nullptr;
    }
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
    ::wcsncpy_s(nid.szTip, std::size(nid.szTip), L"Motrix", _TRUNCATE);

    if (!state.trayInstalled) {
        if (::Shell_NotifyIconW(NIM_ADD, &nid) != FALSE) {
            state.trayInstalled = true;
        }
    } else {
        ::Shell_NotifyIconW(NIM_MODIFY, &nid);
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

    const std::wstring wideTitle = motrix::core::Utf8ToWide(title);
    const std::wstring wideBody = motrix::core::Utf8ToWide(body);

    NOTIFYICONDATAW nid{};
    nid.cbSize = sizeof(NOTIFYICONDATAW);
    nid.hWnd = static_cast<HWND>(state.hwnd);
    nid.uID = 1;
    nid.uFlags = NIF_INFO;
    nid.dwInfoFlags = NIIF_INFO;
    ::wcsncpy_s(nid.szInfoTitle, std::size(nid.szInfoTitle), wideTitle.c_str(), _TRUNCATE);
    ::wcsncpy_s(nid.szInfo, std::size(nid.szInfo), wideBody.c_str(), _TRUNCATE);
    ::Shell_NotifyIconW(NIM_MODIFY, &nid);
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
                Win32ShowWindow(hwnd, true, true);
                state.hiddenToTray = false;
                state.activeFramesRemaining = std::max(state.activeFramesRemaining, 3);
            } else {
                Win32ShowWindow(hwnd, false, false);
                state.hiddenToTray = true;
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
                Win32ShowWindow(hwnd, true, true);
                state.hiddenToTray = false;
                state.activeFramesRemaining = std::max(state.activeFramesRemaining, 3);
            }
            break;
        }
        default: break;
    }
}

}  // namespace motrix::platform

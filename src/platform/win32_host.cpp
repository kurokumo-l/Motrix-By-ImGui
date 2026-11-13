module;

#include <d3d11.h>
#include <windows.h>

#include "imgui.h"
#include "imgui_impl_dx11.h"
#include "imgui_impl_win32.h"

module motrix.platform;

import std;
import motrix.core;
import :window_host;
import :dx11_internal;
import :win32_internal;

namespace motrix::platform {

struct Win32WindowHost::Impl {
    Win32WindowState state;
    Win32WindowHost* owner = nullptr;
    UniqueHandle wakeEvent;
    Dx11Context dx11;

    bool occluded = false;
    bool animationActive = false;
    std::uint64_t framesRendered = 0;
    std::uint64_t sleepWaits = 0;

    void PumpMessages(bool& done);
    [[nodiscard]] bool ShouldSleep(bool screenshotMode);
    void SleepUntilEvent(bool wantTextInput);
    [[nodiscard]] bool RenderSingleFrame(const FrameCallback& onFrame, bool screenshotMode);
};

void Win32WindowHost::Impl::PumpMessages(bool& done) {
    MSG msg{};
    while (::PeekMessageW(&msg, nullptr, 0U, 0U, PM_REMOVE)) {
        ::TranslateMessage(&msg);
        ::DispatchMessageW(&msg);
        if (msg.message == WM_QUIT) {
            done = true;
        }
    }
}

bool Win32WindowHost::Impl::ShouldSleep(bool screenshotMode) {
    if (screenshotMode) {
        return false;
    }
    if (state.hiddenToTray || Win32IsWindowIconic(state.hwnd)) {
        return true;
    }
    if (occluded && dx11.IsOccludedStillActive()) {
        return true;
    }
    occluded = false;
    return state.activeFramesRemaining <= 0;
}

void Win32WindowHost::Impl::SleepUntilEvent(bool wantTextInput) {
    const bool pausedSurface = state.hiddenToTray || Win32IsWindowIconic(state.hwnd);
    DWORD timeoutMs = INFINITE;
    if (!pausedSurface && occluded) {
        timeoutMs = 150u;
    } else if (!pausedSurface && wantTextInput) {
        timeoutMs = 200u;
    }

    HANDLE h = wakeEvent.Get();
    const DWORD waitRes =
        ::MsgWaitForMultipleObjectsEx(1, &h, timeoutMs, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
    ++sleepWaits;

    if (waitRes == WAIT_OBJECT_0 || waitRes == WAIT_TIMEOUT) {
        state.activeFramesRemaining = std::max(state.activeFramesRemaining, 2);
    }
}

bool Win32WindowHost::Impl::RenderSingleFrame(const FrameCallback& onFrame, bool screenshotMode) {
    if (state.pendingResizeW != 0 && state.pendingResizeH != 0) {
        dx11.Resize(state.pendingResizeW, state.pendingResizeH);
        state.pendingResizeW = 0;
        state.pendingResizeH = 0;
    }

    state.dpiScale = Win32GetWindowDpiScale(state.hwnd);

    ImGui_ImplDX11_NewFrame();
    ImGui_ImplWin32_NewFrame();

    ImGuiIO& io = ImGui::GetIO();
    if (screenshotMode) {
        io.DeltaTime = 1.0f / 60.0f;
    } else if (io.DeltaTime > 0.05f) {
        io.DeltaTime = 1.0f / 30.0f;
    }

    ImGui::NewFrame();
    animationActive = false;
    state.titlebarRects.clear();
    if (onFrame) {
        onFrame(*owner);
    }
    ImGui::Render();

    const ImVec4 clear = ImGui::GetStyle().Colors[ImGuiCol_WindowBg];
    dx11.BeginRenderPass(clear.x, clear.y, clear.z, 1.0f);
    ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
    ++framesRendered;

    if (screenshotMode
        && static_cast<int>(framesRendered) >= std::max(1, state.config.screenshotFrames)) {
        if (!dx11.CaptureSwapChainPng(
                std::filesystem::path(motrix::core::Utf8ToWide(state.config.screenshotPath)))) {
            std::cerr << "[dx11] Failed to write screenshot PNG\n";
        }
        return true;
    }

    occluded = dx11.PresentFrame(!screenshotMode);
    if (state.activeFramesRemaining > 0) {
        --state.activeFramesRemaining;
    }
    if (animationActive) {
        state.activeFramesRemaining = std::max(state.activeFramesRemaining, 2);
    }
    return false;
}

Win32WindowHost::Win32WindowHost() : impl_(std::make_unique<Impl>()) {
    impl_->owner = this;
}

Win32WindowHost::~Win32WindowHost() = default;

motrix::core::Result<int> Win32WindowHost::Run(const WindowHostConfig& config,
                                               const InitCallback& onImGuiInit,
                                               const FrameCallback& onFrame) {
    impl_->state.config = config;
    impl_->state.darkFrame = config.initialDark;
    impl_->wakeEvent.Reset(::CreateEventW(nullptr, FALSE, FALSE, nullptr));
    if (!impl_->wakeEvent.IsValid()) {
        return std::unexpected(motrix::core::MakeError(motrix::core::ErrorDomain::Platform,
                                                       motrix::core::ErrorCode::IoFailure,
                                                       "Failed to create Win32 wake event"));
    }

    Win32EnableDpiAwareness();
    impl_->state.dpiScale = Win32GetPrimaryMonitorDpiScale();
    if (auto winRes = Win32CreateWindow(impl_->state); !winRes) {
        return std::unexpected(winRes.error());
    }

    if (auto dxRes = impl_->dx11.Initialize(impl_->state.hwnd); !dxRes) {
        Win32DestroyWindow(impl_->state);
        return std::unexpected(dxRes.error());
    }

    const bool screenshotMode = !config.screenshotPath.empty();
    Win32ShowWindow(impl_->state.hwnd, true, !screenshotMode);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigDpiScaleFonts = true;

    if (onImGuiInit) {
        onImGuiInit(*this);
    }

    ImGui_ImplWin32_Init(impl_->state.hwnd);
    ImGui_ImplDX11_Init(static_cast<ID3D11Device*>(impl_->dx11.DeviceRaw()),
                        static_cast<ID3D11DeviceContext*>(impl_->dx11.ContextRaw()));

    bool done = false;
    while (!done) {
        impl_->PumpMessages(done);
        if (done) {
            break;
        }
        if (impl_->ShouldSleep(screenshotMode)) {
            impl_->SleepUntilEvent(io.WantTextInput);
            continue;
        }
        done = impl_->RenderSingleFrame(onFrame, screenshotMode);
    }

    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
    impl_->dx11.Shutdown();
    Win32DestroyWindow(impl_->state);
    return 0;
}

float Win32WindowHost::DpiScale() const noexcept {
    return impl_->state.dpiScale;
}

bool Win32WindowHost::IsMaximized() const noexcept {
    return Win32IsWindowZoomed(impl_->state.hwnd);
}

bool Win32WindowHost::IsHiddenToTray() const noexcept {
    return impl_->state.hiddenToTray;
}

HostMetricsSnapshot Win32WindowHost::Metrics() const noexcept {
    return HostMetricsSnapshot{
        .framesRendered = impl_->framesRendered,
        .sleepWaits = impl_->sleepWaits,
        .occluded = impl_->occluded,
        .minimized = Win32IsWindowIconic(impl_->state.hwnd),
        .hiddenToTray = impl_->state.hiddenToTray,
    };
}

void Win32WindowHost::Minimize() {
    Win32MinimizeWindow(impl_->state.hwnd);
}

void Win32WindowHost::ToggleMaximize() {
    Win32ToggleMaximizeWindow(impl_->state.hwnd);
    RequestFrame();
}

void Win32WindowHost::Close() {
    Win32PostCloseWindow(impl_->state.hwnd);
}

void Win32WindowHost::ShowFromTray() {
    if (!impl_->state.hwnd) {
        return;
    }
    impl_->state.hiddenToTray = false;
    Win32ShowWindow(impl_->state.hwnd, true, true);
    RequestFrame();
}

void Win32WindowHost::HideToTray() {
    if (!impl_->state.hwnd) {
        return;
    }
    impl_->state.hiddenToTray = true;
    Win32ShowWindow(impl_->state.hwnd, false, false);
}

void Win32WindowHost::SetDarkFrame(bool dark) {
    if (impl_->state.darkFrame == dark) {
        return;
    }
    impl_->state.darkFrame = dark;
    Win32ApplyDwmChrome(impl_->state.hwnd, dark);
}

void Win32WindowHost::SetWindowTitle(std::string_view utf8Title) {
    Win32SetWindowTitle(impl_->state.hwnd, utf8Title);
}

void Win32WindowHost::RequestFrame() {
    impl_->state.activeFramesRemaining = std::max(impl_->state.activeFramesRemaining, 3);
    if (impl_->wakeEvent.IsValid()) {
        ::SetEvent(impl_->wakeEvent.Get());
    }
}

void Win32WindowHost::NotifyAnimationActive() {
    impl_->animationActive = true;
}

void Win32WindowHost::SetTitlebarHeight(float heightPx) {
    impl_->state.titlebarHeightPx = heightPx;
}

void Win32WindowHost::RegisterTitlebarInteractiveRect(TitlebarRect rect) {
    impl_->state.titlebarRects.push_back(rect);
}

void Win32WindowHost::CopyTextToClipboard(std::string_view utf8) {
    Win32CopyTextToClipboard(impl_->state.hwnd, utf8);
}

void Win32WindowHost::OpenPathInExplorer(std::string_view utf8Path) {
    Win32OpenPathInExplorer(utf8Path);
}

std::optional<std::string> Win32WindowHost::PickTorrentFile() {
    auto chosen = Win32PickTorrentFile(impl_->state.hwnd);
    RequestFrame();
    return chosen;
}

std::optional<std::string> Win32WindowHost::PickFolder(std::string_view initialDir) {
    auto chosen = Win32PickFolder(impl_->state.hwnd, initialDir);
    RequestFrame();
    return chosen;
}

}  // namespace motrix::platform

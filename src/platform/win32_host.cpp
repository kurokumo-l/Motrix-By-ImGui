module;

// clang-format off
#include <windows.h>
#include <objbase.h>
#include <d3d11.h>
// clang-format on

#include "imgui.h"
#include "imgui_impl_dx11.h"
#include "imgui_impl_win32.h"

module motrix.platform;

import std;
import motrix.core;
import :window_host;
import :dx11_internal;
import :win32_internal;
import :win32;

namespace motrix::platform {

namespace {
inline constexpr UINT kWmAppWakeup = WM_APP + 101;
}

struct Win32WindowHost::Impl {
    Win32WindowState state;
    Win32WindowHost* owner = nullptr;
    UniqueHandle wakeEvent{::CreateEventW(nullptr, FALSE, FALSE, nullptr)};
    Dx11Context dx11;
    InitCallback savedImGuiInit;
    std::atomic<DWORD> uiThreadId{0};
    std::atomic<void*> wakeupHwnd{nullptr};
    std::atomic<std::uint64_t> textureGeneration{1};
    std::mutex uiActionMutex;
    std::vector<std::function<void()>> pendingUiActions;
    bool occluded = false;
    bool animationActive = false;
    bool desiredSleepBlocker = false;
    bool firstShowMaximized = false;
    bool imguiInitialized = false;
    std::uint64_t framesRendered = 0;
    std::uint64_t sleepWaits = 0;

    void RunOnUiThread(std::function<void()> fn) {
        const DWORD tid = uiThreadId.load(std::memory_order_acquire);
        if (tid != 0 && ::GetCurrentThreadId() == tid) {
            fn();
            return;
        }
        {
            std::lock_guard lock(uiActionMutex);
            pendingUiActions.push_back(std::move(fn));
        }
        if (wakeEvent.IsValid()) {
            ::SetEvent(wakeEvent.Get());
        }
        if (void* rawHwnd = wakeupHwnd.load(std::memory_order_acquire)) {
            ::PostMessageW(static_cast<HWND>(rawHwnd), kWmAppWakeup, 0, 0);
        }
    }

    void TryRunOnUiThread(std::function<void()> fn) noexcept {
        try {
            RunOnUiThread(std::move(fn));
        } catch (...) {}
    }

    void FlushPendingUiActions() {
        std::vector<std::function<void()>> actions;
        if (std::lock_guard lock(uiActionMutex); !pendingUiActions.empty()) {
            actions.swap(pendingUiActions);
        }
        for (auto& fn : actions) {
            if (fn) {
                fn();
            }
        }
    }

    void PumpMessages(bool& done) {
        MSG msg{};
        while (::PeekMessageW(&msg, nullptr, 0U, 0U, PM_REMOVE)) {
            ::TranslateMessage(&msg);
            ::DispatchMessageW(&msg);
            if (msg.message == WM_QUIT) {
                done = true;
            }
        }
    }

    [[nodiscard]] bool ShouldSleep(bool screenshotMode) {
        if (screenshotMode) {
            return false;
        }
        if (state.hiddenToTray.load(std::memory_order_acquire) || Win32IsWindowIconic(state.hwnd)) {
            return true;
        }
        if (!imguiInitialized || !dx11.IsInitialized()) {
            return false;
        }
        if (occluded && dx11.IsOccludedStillActive()) {
            return true;
        }
        occluded = false;
        return state.activeFramesRemaining.load(std::memory_order_acquire) <= 0;
    }

    void TrimIdleMemory() noexcept {
        dx11.Trim();
        Win32TrimWorkingSet();
    }

    void SleepUntilEvent(bool wantTextInput) {
        const bool paused =
            state.hiddenToTray.load(std::memory_order_acquire) || Win32IsWindowIconic(state.hwnd);
        DWORD timeoutMs = INFINITE;
        if (!paused && occluded) {
            timeoutMs = 150u;
        } else if (!paused && wantTextInput) {
            timeoutMs = 200u;
        }
        HANDLE h = wakeEvent.Get();
        const DWORD waitRes =
            ::MsgWaitForMultipleObjectsEx(1, &h, timeoutMs, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
        ++sleepWaits;
        if (waitRes == WAIT_OBJECT_0 || waitRes == WAIT_TIMEOUT) {
            state.BumpActiveFrames(2);
        }
    }

    [[nodiscard]] bool RecoverFromDeviceLost() {
        if (imguiInitialized) {
            ImGui_ImplDX11_Shutdown();
        }
        dx11.Shutdown();
        textureGeneration.fetch_add(1, std::memory_order_acq_rel);
        if (auto dxRes = dx11.Initialize(state.hwnd); !dxRes) {
            std::cerr << "[dx11] Device recovery failed: " << dxRes.error().message << "\n";
            if (imguiInitialized) {
                ImGui_ImplWin32_Shutdown();
                ImGui::DestroyContext();
                imguiInitialized = false;
            }
            return false;
        }
        if (state.pendingResizeW != 0 && state.pendingResizeH != 0) {
            (void)dx11.Resize(state.pendingResizeW, state.pendingResizeH);
            state.pendingResizeW = 0;
            state.pendingResizeH = 0;
        }
        if (imguiInitialized) {
            ImGui_ImplDX11_Init(static_cast<ID3D11Device*>(dx11.DeviceRaw()),
                                static_cast<ID3D11DeviceContext*>(dx11.ContextRaw()));
        }
        occluded = false;
        state.BumpActiveFrames(3);
        return true;
    }

    [[nodiscard]] bool RenderSingleFrame(const FrameCallback& onFrame, bool screenshotMode) {
        if (state.pendingResizeW != 0 && state.pendingResizeH != 0) {
            const bool resized = dx11.Resize(state.pendingResizeW, state.pendingResizeH);
            state.pendingResizeW = 0;
            state.pendingResizeH = 0;
            if (!resized && dx11.IsDeviceLost() && !RecoverFromDeviceLost()) {
                return true;
            }
        }
        state.dpiScale = Win32GetWindowDpiScale(state.hwnd);
        if (state.config.onBeforeNewFrame) {
            state.config.onBeforeNewFrame();
        }
        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();

        ImGuiIO& io = ImGui::GetIO();
        if (screenshotMode && state.config.screenshotHover.has_value()) {
            const ImGuiViewport* mvp = ImGui::GetMainViewport();
            io.AddFocusEvent(true);
            io.AddMousePosEvent(mvp->Pos.x + state.config.screenshotHover->first,
                                mvp->Pos.y + state.config.screenshotHover->second);
        }
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
        if (!dx11.BeginRenderPass(clear.x, clear.y, clear.z, 1.0f)) {
            if (dx11.IsDeviceLost() && !RecoverFromDeviceLost()) {
                return true;
            }
            return false;
        }
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
        ++framesRendered;

        if (screenshotMode
            && static_cast<int>(framesRendered) >= std::max(1, state.config.screenshotFrames)) {
            if (!dx11.CaptureSwapChainPng(motrix::core::PathFromUtf8(state.config.screenshotPath))) {
                std::cerr << "[dx11] Failed to write screenshot PNG\n";
            }
            return true;
        }
        const PresentResult pres = dx11.PresentFrame(!screenshotMode);
        occluded = (pres == PresentResult::Occluded);
        if (pres == PresentResult::DeviceLost) {
            if (!RecoverFromDeviceLost()) {
                return true;
            }
            return false;
        }
        int curFrames = state.activeFramesRemaining.load(std::memory_order_acquire);
        while (curFrames > 0
               && !state.activeFramesRemaining.compare_exchange_weak(
                   curFrames, curFrames - 1, std::memory_order_acq_rel, std::memory_order_relaxed)) {}
        if (animationActive) {
            state.BumpActiveFrames(2);
        }
        return false;
    }

    [[nodiscard]] motrix::core::Result<void> SetupWindow(const WindowHostConfig& config,
                                                         bool screenshotMode) {
        Win32ReleaseTaskbarList(state);
        Win32DestroyTrayIcons(state);
        wakeupHwnd.store(nullptr, std::memory_order_release);
        textureGeneration.fetch_add(1, std::memory_order_acq_rel);
        state.ResetForSession(config, QuerySystemDarkMode(), QueryTaskbarDarkMode());

        if (!wakeEvent.IsValid()) {
            return std::unexpected(motrix::core::MakeError(motrix::core::ErrorDomain::Platform,
                                                           motrix::core::ErrorCode::IoFailure,
                                                           "Failed to create Win32 wake event"));
        }

        Win32EnableDpiAwareness();
        state.dpiScale = Win32GetPrimaryMonitorDpiScale();
        if (auto winRes = Win32CreateWindow(state); !winRes) {
            return std::unexpected(winRes.error());
        }
        wakeupHwnd.store(state.hwnd, std::memory_order_release);
        if (!screenshotMode && state.enableTrayIcon) {
            Win32InstallOrUpdateTray(state);
        }
        const bool wantMax =
            (!screenshotMode && config.initialBounds && config.initialBounds->maximized);
        firstShowMaximized = (config.startHidden && wantMax);
        if (config.startHidden && !screenshotMode) {
            state.hiddenToTray.store(true, std::memory_order_release);
            Win32TrimWorkingSet();
            return {};
        }
        if (!EnsureUiInitialized()) {
            Win32DestroyWindow(state);
            return std::unexpected(motrix::core::MakeError(motrix::core::ErrorDomain::Platform,
                                                           motrix::core::ErrorCode::IoFailure,
                                                           "Failed to initialize DX11 and ImGui"));
        }
        Win32ShowWindowMode(state.hwnd, wantMax, !screenshotMode);
        return {};
    }

    [[nodiscard]] bool EnsureUiInitialized() {
        if (imguiInitialized) {
            return true;
        }
        if (auto dxRes = dx11.Initialize(state.hwnd); !dxRes) {
            std::cerr << "[dx11] " << dxRes.error().message << "\n";
            return false;
        }
        if (state.pendingResizeW != 0 && state.pendingResizeH != 0) {
            if (!dx11.Resize(state.pendingResizeW, state.pendingResizeH)) {
                dx11.Shutdown();
                return false;
            }
            state.pendingResizeW = 0;
            state.pendingResizeH = 0;
        }
        state.dpiScale = Win32GetWindowDpiScale(state.hwnd);
        InitImGuiContext(savedImGuiInit);
        imguiInitialized = true;
        return true;
    }

    void InitImGuiContext(const InitCallback& onImGuiInit) {
        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO();
        io.IniFilename = nullptr;
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
        io.ConfigDpiScaleFonts = true;
        if (onImGuiInit) {
            onImGuiInit(*owner);
        }
        ImGui_ImplWin32_Init(state.hwnd);
        ImGui_ImplDX11_Init(static_cast<ID3D11Device*>(dx11.DeviceRaw()),
                            static_cast<ID3D11DeviceContext*>(dx11.ContextRaw()));
        if (desiredSleepBlocker) {
            Win32SetSleepBlocker(true);
        }
    }

    void ShutdownSession() {
        wakeupHwnd.store(nullptr, std::memory_order_release);
        Win32SetSleepBlocker(false);
        Win32SetCachedTaskbarProgress(state, TaskbarProgressMode::None, 0, 0);
        Win32ReleaseTaskbarList(state);
        Win32DestroyTrayIcons(state);
        if (imguiInitialized) {
            ImGui_ImplDX11_Shutdown();
            ImGui_ImplWin32_Shutdown();
            ImGui::DestroyContext();
            imguiInitialized = false;
        }
        dx11.Shutdown();
        Win32DestroyWindow(state);
    }
};

Win32WindowHost::Win32WindowHost() : impl_(std::make_unique<Impl>()) {
    impl_->owner = this;
}

Win32WindowHost::~Win32WindowHost() {
    if (impl_) {
        Win32SetSleepBlocker(false);
        Win32ReleaseTaskbarList(impl_->state);
        Win32DestroyTrayIcons(impl_->state);
    }
}

motrix::core::Result<int> Win32WindowHost::Run(const WindowHostConfig& config,
                                               const InitCallback& onImGuiInit,
                                               const FrameCallback& onFrame) {
    ScopedComInit comScope;
    impl_->uiThreadId.store(::GetCurrentThreadId(), std::memory_order_release);
    struct RunSessionGuard {
        Impl& impl;
        ~RunSessionGuard() {
            std::lock_guard lock(impl.uiActionMutex);
            impl.pendingUiActions.clear();
            impl.wakeupHwnd.store(nullptr, std::memory_order_release);
            impl.uiThreadId.store(0, std::memory_order_release);
        }
    } runGuard{*impl_};
    const bool screenshotMode = !config.screenshotPath.empty();
    impl_->savedImGuiInit = onImGuiInit;
    impl_->imguiInitialized = false;
    if (auto setupRes = impl_->SetupWindow(config, screenshotMode); !setupRes) {
        return std::unexpected(setupRes.error());
    }
    bool done = false;
    while (!done) {
        impl_->PumpMessages(done);
        impl_->FlushPendingUiActions();
        if (done) {
            break;
        }
        if (impl_->ShouldSleep(screenshotMode)) {
            if (std::exchange(impl_->state.trimRequested, false)) {
                impl_->TrimIdleMemory();
            }
            const bool wantText = impl_->imguiInitialized && ImGui::GetIO().WantTextInput;
            impl_->SleepUntilEvent(wantText);
            impl_->FlushPendingUiActions();
            continue;
        }
        if (!impl_->EnsureUiInitialized()) {
            done = true;
            break;
        }
        done = impl_->RenderSingleFrame(onFrame, screenshotMode);
    }
    impl_->ShutdownSession();
    return 0;
}

float Win32WindowHost::DpiScale() const noexcept {
    return impl_->state.dpiScale;
}
bool Win32WindowHost::IsMaximized() const noexcept {
    return Win32IsWindowZoomed(impl_->state.hwnd);
}
bool Win32WindowHost::IsHiddenToTray() const noexcept {
    return impl_->state.hiddenToTray.load(std::memory_order_acquire);
}
bool Win32WindowHost::IsSystemDarkMode() const noexcept {
    return impl_->state.systemDark;
}
WindowBounds Win32WindowHost::CurrentWindowBounds() const {
    return Win32ReadWindowBounds(impl_->state.hwnd);
}
HostMetricsSnapshot Win32WindowHost::Metrics() const noexcept {
    return {.framesRendered = impl_->framesRendered,
            .sleepWaits = impl_->sleepWaits,
            .occluded = impl_->occluded,
            .minimized = Win32IsWindowIconic(impl_->state.hwnd),
            .hiddenToTray = impl_->state.hiddenToTray.load(std::memory_order_acquire)};
}
void Win32WindowHost::Minimize() {
    impl_->RunOnUiThread([this] { Win32MinimizeWindow(impl_->state.hwnd); });
}
void Win32WindowHost::Close() {
    impl_->RunOnUiThread([this] { Win32PostCloseWindow(impl_->state.hwnd); });
}
void Win32WindowHost::ToggleMaximize() {
    impl_->RunOnUiThread([this] {
        Win32ToggleMaximizeWindow(impl_->state.hwnd);
        RequestFrame();
    });
}
void Win32WindowHost::RequestQuit() {
    impl_->RunOnUiThread([this] {
        impl_->state.explicitQuitRequested = true;
        Win32PostCloseWindow(impl_->state.hwnd);
    });
}
void Win32WindowHost::ShowFromTray() {
    impl_->RunOnUiThread([this] {
        if (!impl_->state.hwnd) {
            return;
        }
        impl_->state.trimRequested = false;
        impl_->occluded = false;
        if (!impl_->EnsureUiInitialized()) {
            return;
        }
        impl_->state.hiddenToTray.store(false, std::memory_order_release);
        Win32ShowWindowMode(impl_->state.hwnd, std::exchange(impl_->firstShowMaximized, false), true);
        RequestFrame();
    });
}
void Win32WindowHost::HideToTray() {
    impl_->RunOnUiThread([this] {
        if (!impl_->state.hwnd) {
            return;
        }
        impl_->state.hiddenToTray.store(true, std::memory_order_release);
        impl_->state.trimRequested = true;
        Win32ShowWindow(impl_->state.hwnd, false, false);
    });
}
void Win32WindowHost::SetDarkFrame(bool dark) {
    impl_->RunOnUiThread([this, dark] {
        if (impl_->state.darkFrame == dark) {
            return;
        }
        impl_->state.darkFrame = dark;
        Win32ApplyDwmChrome(impl_->state.hwnd, dark);
    });
}
void Win32WindowHost::SetWindowTitle(std::string_view utf8Title) {
    impl_->RunOnUiThread(
        [this, title = std::string(utf8Title)] { Win32SetWindowTitle(impl_->state.hwnd, title); });
}
void Win32WindowHost::SetBindCloseToHide(bool enabled) noexcept {
    impl_->TryRunOnUiThread([this, enabled] { impl_->state.bindCloseToHide = enabled; });
}
void Win32WindowHost::SetAutoHideOnBlur(bool enabled) noexcept {
    impl_->TryRunOnUiThread([this, enabled] { impl_->state.autoHideOnBlur = enabled; });
}
void Win32WindowHost::SetTrayEnabled(bool enabled) {
    impl_->RunOnUiThread([this, enabled] {
        if (impl_->state.enableTrayIcon == enabled) {
            return;
        }
        impl_->state.enableTrayIcon = enabled;
        if (impl_->state.config.screenshotPath.empty()) {
            Win32InstallOrUpdateTray(impl_->state);
        }
    });
}
void Win32WindowHost::UpdateTrayStatus(bool hasActiveDownloads,
                                       bool isZhCn,
                                       std::string_view tooltipText) {
    impl_->RunOnUiThread([this, hasActiveDownloads, isZhCn, tip = std::string(tooltipText)] {
        const bool statusChanged = (impl_->state.trayActiveDownloads != hasActiveDownloads)
                                   || (impl_->state.trayIsZhCn != isZhCn)
                                   || (impl_->state.trayTooltipUtf8 != tip);
        impl_->state.trayActiveDownloads = hasActiveDownloads;
        impl_->state.trayIsZhCn = isZhCn;
        impl_->state.trayTooltipUtf8 = tip;
        if (impl_->state.config.screenshotPath.empty() && impl_->state.enableTrayIcon
            && (statusChanged || !impl_->state.trayInstalled)) {
            Win32InstallOrUpdateTray(impl_->state);
        }
    });
}
void Win32WindowHost::UpdateTaskbarProgress(TaskbarProgressMode mode,
                                            std::uint64_t completed,
                                            std::uint64_t total) {
    impl_->RunOnUiThread([this, mode, completed, total] {
        Win32SetCachedTaskbarProgress(impl_->state, mode, completed, total);
    });
}
void Win32WindowHost::ShowToastNotification(std::string_view title,
                                            std::string_view body,
                                            std::string_view clickTargetDir) {
    impl_->RunOnUiThread(
        [this, t = std::string(title), b = std::string(body), dir = std::string(clickTargetDir)] {
            if (impl_->state.config.screenshotPath.empty()) {
                Win32ShowTrayBalloon(impl_->state, t, b, dir);
            }
        });
}
void Win32WindowHost::SetSleepBlockerEnabled(bool active) noexcept {
    impl_->TryRunOnUiThread([this, active] {
        if (impl_->desiredSleepBlocker != active) {
            impl_->desiredSleepBlocker = active;
            Win32SetSleepBlocker(active);
        }
    });
}
void Win32WindowHost::RequestFrame() {
    impl_->state.BumpActiveFrames(3);
    if (impl_->wakeEvent.IsValid()) {
        ::SetEvent(impl_->wakeEvent.Get());
    }
    if (void* rawHwnd = impl_->wakeupHwnd.load(std::memory_order_acquire)) {
        const DWORD tid = impl_->uiThreadId.load(std::memory_order_acquire);
        if (tid != 0 && ::GetCurrentThreadId() != tid) {
            ::PostMessageW(static_cast<HWND>(rawHwnd), kWmAppWakeup, 0, 0);
        }
    }
}
void Win32WindowHost::NotifyAnimationActive() {
    impl_->animationActive = true;
}
void Win32WindowHost::SetTitlebarHeight(float heightPx) {
    impl_->state.titlebarHeightPx = heightPx;
}
void Win32WindowHost::SetTitlebarBlocked(bool blocked) {
    impl_->state.titlebarBlocked = blocked;
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
    if (impl_->state.inPickerDialog) {
        return std::nullopt;
    }
    impl_->state.inPickerDialog = true;
    std::optional<std::string> chosen;
    {
        ScopedModalLoop modalGuard(impl_->state);
        chosen = Win32PickTorrentFile(impl_->state.hwnd);
    }
    impl_->state.inPickerDialog = false;
    RequestFrame();
    return chosen;
}
std::optional<std::string> Win32WindowHost::PickFolder(std::string_view initialDir) {
    if (impl_->state.inPickerDialog) {
        return std::nullopt;
    }
    impl_->state.inPickerDialog = true;
    std::optional<std::string> chosen;
    {
        ScopedModalLoop modalGuard(impl_->state);
        chosen = Win32PickFolder(impl_->state.hwnd, initialDir);
    }
    impl_->state.inPickerDialog = false;
    RequestFrame();
    return chosen;
}
std::uint64_t Win32WindowHost::UploadRgbaTexture(std::uint64_t existingId,
                                                 int width,
                                                 int height,
                                                 std::span<const std::uint8_t> rgba) {
    return impl_->dx11.UploadRgbaTexture(existingId, width, height, rgba);
}
std::uint64_t Win32WindowHost::TextureGeneration() const noexcept {
    return impl_->textureGeneration.load(std::memory_order_acquire);
}

}  // namespace motrix::platform

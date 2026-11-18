export module motrix.platform:window_host;

import std;

namespace motrix::platform {

export enum class TrayMenuAction {
    ShowWindow = 1001,
    NewTask = 1002,
    NewBtTask = 1003,
    OpenTorrentFile = 1004,
    ResumeAll = 1005,
    PauseAll = 1006,
    Tasks = 1007,
    Preferences = 1008,
    Quit = 1009,
};

export enum class TaskbarProgressMode {
    None = 0,
    Indeterminate,
    Normal,
    Error,
    Paused,
};

export struct WindowBounds {
    int x = 0;
    int y = 0;
    int width = 1024;
    int height = 768;
    bool hasPosition = false;
    bool maximized = false;
};

export struct WindowHostConfig {
    std::string title = "Motrix";
    int width = 1024;
    int height = 768;
    int minWidth = 840;
    int minHeight = 420;
    bool initialDark = false;
    bool initialZhCn = false;
    bool startHidden = false;
    bool bindCloseToHide = false;
    bool autoHideOnBlur = false;
    bool enableTrayIcon = true;
    std::optional<WindowBounds> initialBounds;
    std::string screenshotPath;
    int screenshotFrames = 8;
    std::optional<std::pair<float, float>> screenshotHover;
    std::function<void(std::vector<std::string>)> onFilesDropped;
    std::function<void(TrayMenuAction)> onTrayAction;
    std::function<void(bool)> onSessionEnding;
    std::function<void()> onQuitRequested;
    std::function<void(const WindowBounds&)> onBoundsChanged;
    std::function<void()> onBeforeNewFrame;
};

export struct TitlebarRect {
    float x0 = 0.0f;
    float y0 = 0.0f;
    float x1 = 0.0f;
    float y1 = 0.0f;
};

export struct HostMetricsSnapshot {
    std::uint64_t framesRendered = 0;
    std::uint64_t sleepWaits = 0;
    bool occluded = false;
    bool minimized = false;
    bool hiddenToTray = false;
};

export class IWindowHost {
public:
    virtual ~IWindowHost() = default;

    [[nodiscard]] virtual float DpiScale() const noexcept = 0;
    [[nodiscard]] virtual bool IsMaximized() const noexcept = 0;
    [[nodiscard]] virtual bool IsHiddenToTray() const noexcept = 0;
    [[nodiscard]] virtual bool IsSystemDarkMode() const noexcept = 0;
    [[nodiscard]] virtual HostMetricsSnapshot Metrics() const noexcept = 0;
    [[nodiscard]] virtual WindowBounds CurrentWindowBounds() const = 0;

    virtual void Minimize() = 0;
    virtual void ToggleMaximize() = 0;
    virtual void Close() = 0;
    virtual void RequestQuit() = 0;
    virtual void ShowFromTray() = 0;
    virtual void HideToTray() = 0;
    virtual void SetDarkFrame(bool dark) = 0;
    virtual void SetWindowTitle(std::string_view utf8Title) = 0;

    virtual void SetBindCloseToHide(bool enabled) noexcept = 0;
    virtual void SetAutoHideOnBlur(bool enabled) noexcept = 0;
    virtual void SetTrayEnabled(bool enabled) = 0;
    virtual void
    UpdateTrayStatus(bool hasActiveDownloads, bool isZhCn, std::string_view tooltipText = {}) = 0;
    virtual void
    UpdateTaskbarProgress(TaskbarProgressMode mode, std::uint64_t completed, std::uint64_t total) = 0;
    virtual void ShowToastNotification(std::string_view title,
                                       std::string_view body,
                                       std::string_view clickTargetDir = {}) = 0;
    virtual void SetSleepBlockerEnabled(bool active) noexcept = 0;

    virtual void RequestFrame() = 0;
    virtual void NotifyAnimationActive() = 0;
    virtual void SetTitlebarHeight(float heightPx) = 0;
    virtual void SetTitlebarBlocked(bool blocked) = 0;
    virtual void RegisterTitlebarInteractiveRect(TitlebarRect rect) = 0;
    virtual void CopyTextToClipboard(std::string_view utf8) = 0;
    virtual void OpenPathInExplorer(std::string_view utf8Path) = 0;
    [[nodiscard]] virtual std::optional<std::string> PickTorrentFile() = 0;
    [[nodiscard]] virtual std::optional<std::string> PickFolder(std::string_view initialDir = {}) = 0;
    [[nodiscard]] virtual std::uint64_t UploadRgbaTexture(std::uint64_t /*existingId*/,
                                                          int /*width*/,
                                                          int /*height*/,
                                                          std::span<const std::uint8_t> /*rgba*/) {
        return 0;
    }
};

}  // namespace motrix::platform

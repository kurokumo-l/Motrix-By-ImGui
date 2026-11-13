export module motrix.platform:window_host;

import std;

namespace motrix::platform {

export struct WindowHostConfig {
    std::string title = "Motrix";
    int width = 1024;
    int height = 768;
    int minWidth = 840;
    int minHeight = 420;
    bool initialDark = false;
    std::string screenshotPath;
    int screenshotFrames = 8;
    std::function<void(std::vector<std::string>)> onFilesDropped;
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
    [[nodiscard]] virtual HostMetricsSnapshot Metrics() const noexcept = 0;

    virtual void Minimize() = 0;
    virtual void ToggleMaximize() = 0;
    virtual void Close() = 0;
    virtual void ShowFromTray() = 0;
    virtual void HideToTray() = 0;
    virtual void SetDarkFrame(bool dark) = 0;
    virtual void SetWindowTitle(std::string_view utf8Title) = 0;

    virtual void RequestFrame() = 0;
    virtual void NotifyAnimationActive() = 0;
    virtual void SetTitlebarHeight(float heightPx) = 0;
    virtual void RegisterTitlebarInteractiveRect(TitlebarRect rect) = 0;
    virtual void CopyTextToClipboard(std::string_view utf8) = 0;
    virtual void OpenPathInExplorer(std::string_view utf8Path) = 0;
    [[nodiscard]] virtual std::optional<std::string> PickTorrentFile() = 0;
    [[nodiscard]] virtual std::optional<std::string> PickFolder(std::string_view initialDir = {}) = 0;
};

}  // namespace motrix::platform

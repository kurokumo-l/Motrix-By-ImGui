export module motrix.test_server;

import std;
import motrix.core;
import motrix.domain;
import motrix.platform;
export import motrix.test_server_util;

namespace motrix::test {

export struct ServerStats {
    int totalRequests = 0;
    int rangeRequests = 0;
    int maxConcurrent = 0;
    std::string lastUserAgent;
    std::string lastReferer;
    std::string lastCookie;
    std::string lastAuthorization;
};

export struct TorrentBuildResult {
    std::string base64;
    std::string infoHashHex;
    std::vector<std::vector<std::uint8_t>> filePayloads;
};

export struct TorrentFileSpec {
    std::vector<std::string> pathParts;
    std::vector<std::uint8_t> data;
};

export class LocalTestServer final {
public:
    LocalTestServer();
    ~LocalTestServer();

    LocalTestServer(const LocalTestServer&) = delete;
    LocalTestServer& operator=(const LocalTestServer&) = delete;

    [[nodiscard]] std::uint16_t Port() const noexcept;
    [[nodiscard]] std::string BaseUrl() const;
    [[nodiscard]] ServerStats Stats() const;
    void ResetStats();
    void SetToggleOk(bool ok);
    void SetCustomRoute(std::string path,
                        std::vector<std::uint8_t> body,
                        std::string contentType = "application/octet-stream");

    [[nodiscard]] static std::vector<std::uint8_t> MakePatternBytes(std::size_t size, std::uint8_t seed);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

export class RecordingWindowHost final : public motrix::platform::IWindowHost {
public:
    float DpiScale() const noexcept override { return 1.0f; }
    bool IsMaximized() const noexcept override { return false; }
    bool IsHiddenToTray() const noexcept override { return false; }
    bool IsSystemDarkMode() const noexcept override { return false; }
    motrix::platform::HostMetricsSnapshot Metrics() const noexcept override { return {}; }
    motrix::platform::WindowBounds CurrentWindowBounds() const override { return {}; }

    void Minimize() override {}
    void ToggleMaximize() override {}
    void Close() override {}
    void RequestQuit() override { ++quitRequests; }
    void ShowFromTray() override {}
    void HideToTray() override {}
    void SetDarkFrame(bool) override {}
    void SetWindowTitle(std::string_view) override {}

    void SetBindCloseToHide(bool) noexcept override {}
    void SetAutoHideOnBlur(bool) noexcept override {}
    void SetTrayEnabled(bool) override {}
    void UpdateTrayStatus(bool hasActiveDownloads, bool, std::string_view tooltipText = {}) override {
        std::scoped_lock lock(mu_);
        lastTrayActive_ = hasActiveDownloads;
        lastTrayTooltip_ = std::string(tooltipText);
        if (hasActiveDownloads) {
            everTrayActive_ = true;
        }
    }
    void UpdateTaskbarProgress(motrix::platform::TaskbarProgressMode mode,
                               std::uint64_t completed,
                               std::uint64_t total) override {
        std::scoped_lock lock(mu_);
        lastProgressMode_ = mode;
        lastProgressCompleted_ = completed;
        lastProgressTotal_ = total;
        if (mode == motrix::platform::TaskbarProgressMode::Normal && total > 0) {
            everNormalProgress_ = true;
        }
    }
    void
    ShowToastNotification(std::string_view title, std::string_view body, std::string_view) override {
        std::scoped_lock lock(mu_);
        toasts_.emplace_back(std::string(title) + "|" + std::string(body));
    }
    void SetSleepBlockerEnabled(bool active) noexcept override {
        std::scoped_lock lock(mu_);
        lastSleepBlock_ = active;
        if (active) {
            everSleepBlocked_ = true;
        }
    }

    void RequestFrame() override {}
    void NotifyAnimationActive() override {}
    void SetTitlebarHeight(float) override {}
    void SetTitlebarBlocked(bool) noexcept override {}
    void RegisterTitlebarInteractiveRect(motrix::platform::TitlebarRect) override {}
    void CopyTextToClipboard(std::string_view utf8) override {
        std::scoped_lock lock(mu_);
        lastClipboard_ = std::string(utf8);
    }
    void OpenPathInExplorer(std::string_view) override {}
    std::optional<std::string> PickTorrentFile() override { return std::nullopt; }
    std::optional<std::string> PickFolder(std::string_view) override { return std::nullopt; }

    [[nodiscard]] std::vector<std::string> Toasts() const {
        std::scoped_lock lock(mu_);
        return toasts_;
    }
    [[nodiscard]] std::string LastClipboard() const {
        std::scoped_lock lock(mu_);
        return lastClipboard_;
    }
    [[nodiscard]] bool EverTrayActive() const {
        std::scoped_lock lock(mu_);
        return everTrayActive_;
    }
    [[nodiscard]] bool EverNormalProgress() const {
        std::scoped_lock lock(mu_);
        return everNormalProgress_;
    }
    [[nodiscard]] bool EverSleepBlocked() const {
        std::scoped_lock lock(mu_);
        return everSleepBlocked_;
    }
    [[nodiscard]] bool LastSleepBlock() const {
        std::scoped_lock lock(mu_);
        return lastSleepBlock_;
    }
    [[nodiscard]] std::string LastTrayTooltip() const {
        std::scoped_lock lock(mu_);
        return lastTrayTooltip_;
    }

    std::atomic<int> quitRequests{0};

private:
    mutable std::mutex mu_;
    bool lastTrayActive_ = false;
    bool everTrayActive_ = false;
    std::string lastTrayTooltip_;
    motrix::platform::TaskbarProgressMode lastProgressMode_ =
        motrix::platform::TaskbarProgressMode::None;
    std::uint64_t lastProgressCompleted_ = 0;
    std::uint64_t lastProgressTotal_ = 0;
    bool everNormalProgress_ = false;
    bool lastSleepBlock_ = false;
    bool everSleepBlocked_ = false;
    std::vector<std::string> toasts_;
    std::string lastClipboard_;
};

export [[nodiscard]] TorrentBuildResult BuildTestTorrent(std::string_view torrentName,
                                                         std::string_view trackerUrl,
                                                         std::span<const TorrentFileSpec> files,
                                                         bool singleFileLayout = false);

export [[nodiscard]] bool VerifyFileMatches(const std::filesystem::path& path,
                                            std::span<const std::uint8_t> expected);

export [[nodiscard]] std::uint16_t PickFreeEphemeralPort();

export template <typename Pred>
[[nodiscard]] bool WaitUntil(Pred&& pred, std::chrono::milliseconds timeout = std::chrono::seconds(15)) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
        if (pred()) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(40));
    }
    return pred();
}

}  // namespace motrix::test

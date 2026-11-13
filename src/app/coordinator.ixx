export module motrix.app:coordinator;

import std;
import motrix.core;
import motrix.domain;
import motrix.engine;
import motrix.platform;
import :command;
import :snapshot;

namespace motrix::app {

export struct CoordinatorOptions {
    motrix::domain::ThemePreference initialTheme = motrix::domain::ThemePreference::Light;
    motrix::core::Locale initialLocale = motrix::core::Locale::EnUS;
    motrix::domain::TaskCategory initialCategory = motrix::domain::TaskCategory::Downloading;
    bool populatePreviewTasks = false;
    bool openAddModal = false;
    bool openAboutModal = false;
};

export class AppCoordinator final : public ICommandSink {
public:
    AppCoordinator(CoordinatorOptions options,
                   motrix::core::Logger& logger,
                   motrix::platform::IWindowHost* windowHost = nullptr);

    void AttachWindowHost(motrix::platform::IWindowHost* windowHost) noexcept;
    [[nodiscard]] std::shared_ptr<const AppStateSnapshot> Snapshot() const noexcept;

    void Dispatch(Command cmd) override;

private:
    void PublishMutated(const std::function<void(AppStateSnapshot&)>& mutator);
    void RecomputeStats(AppStateSnapshot& state);
    void HandleTaskAction(AppStateSnapshot& state, const Command& cmd);
    void HandleBulkAndSubmit(AppStateSnapshot& state, const Command& cmd);
    [[nodiscard]] const motrix::domain::DownloadTaskView*
    FindTaskByGid(const AppStateSnapshot& state, std::string_view gid) const noexcept;

    motrix::core::Logger& logger_;
    motrix::platform::IWindowHost* windowHost_ = nullptr;
    std::mutex writeMutex_;
    AppStateSnapshot mutableState_;
    std::atomic<std::shared_ptr<const AppStateSnapshot>> published_;
    std::uint64_t nextLocalGid_ = 1000;
};

}  // namespace motrix::app

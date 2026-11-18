export module motrix.app:command;

import std;
import motrix.core;
import motrix.domain;
import motrix.platform;

namespace motrix::app {

export enum class ActiveModal {
    None = 0,
    AddTask,
    About,
};

export struct SelectCategoryCmd {
    motrix::domain::TaskCategory category = motrix::domain::TaskCategory::Downloading;
};
export struct SelectSectionCmd {
    int sectionIndex = 0;  // 0 = Tasks, 1 = Preferences
};
export struct OpenModalCmd {
    ActiveModal modal = ActiveModal::None;
};
export struct CloseModalCmd {};
export struct SelectTaskCmd {
    std::string gid;
    bool openDrawer = false;
    bool toggle = false;
    bool range = false;
};
export struct SelectAllTasksCmd {};
export struct SetDetailTabCmd {
    int tabIndex = 0;
};
export struct PauseTaskCmd {
    std::string gid;
};
export struct ResumeTaskCmd {
    std::string gid;
};
export struct RemoveTaskCmd {
    std::string gid;
    bool deleteFiles = false;
};
export struct RemoveTasksCmd {
    std::vector<std::string> gids;
    bool deleteFiles = false;
};
export struct RestartTaskCmd {
    std::string gid;
};
export struct RefreshTasksCmd {};
export struct ResumeAllCmd {};
export struct PauseAllCmd {};
export struct PurgeStoppedCmd {};
export struct ToggleSpeedLimitCmd {};
export struct SetThemeCmd {
    motrix::domain::ThemePreference theme = motrix::domain::ThemePreference::Auto;
};
export struct ToggleThemeCmd {};
export struct SetLocaleCmd {
    motrix::core::Locale locale = motrix::core::Locale::EnUS;
};

export struct SubmitAddUriCmd {
    std::vector<std::string> uris;
    std::string outName;
    std::string dir;
    int split = 16;
    std::string userAgent;
    std::string authorization;
    std::string referer;
    std::string cookie;
    std::string allProxy;
    std::optional<bool> newTaskShowDownloading;
};

export struct CopyTaskLinkCmd {
    std::string gid;
};

export struct ShowInFolderCmd {
    std::string gid;
};

export struct SubmitAddTorrentCmd {
    std::string torrentBase64;
    std::vector<int> selectedIndices;
    std::size_t totalFiles = 0;
    std::string outName;
    std::string dir;
    int split = 16;
    std::string userAgent;
    std::string authorization;
    std::string referer;
    std::string cookie;
    std::string allProxy;
    std::optional<bool> newTaskShowDownloading;
};

export struct ApplyTaskFileSelectionCmd {
    std::string gid;
    std::vector<int> selectedIndices;
};

export struct SavePreferencesCmd {
    motrix::domain::UserPreferences prefs;
    motrix::domain::SystemConfig sysConfig;
    bool restartEngineIfNeeded = false;
};

export struct QueueDroppedFilesCmd {
    std::vector<std::string> paths;
};

export struct SyncTrackersCmd {
    std::vector<std::string> sources;
};

export struct ResetSessionCmd {};
export struct FactoryResetCmd {};

export struct HandleStartupPayloadCmd {
    std::vector<std::string> uris;
    std::vector<std::string> torrentFiles;
    std::vector<std::string> deepLinks;
    bool activateWindow = true;
};

export struct HideToTrayCmd {};
export struct ShowFromTrayCmd {};
export struct QuitAppCmd {};

export struct UpdateWindowBoundsCmd {
    motrix::platform::WindowBounds bounds;
};

export struct HandleTrayActionCmd {
    motrix::platform::TrayMenuAction action = motrix::platform::TrayMenuAction::ShowWindow;
};
export struct StartAria2NextUpdateCmd {
    bool force = false;
};

export using Command = std::variant<SelectCategoryCmd,
                                    SelectSectionCmd,
                                    OpenModalCmd,
                                    CloseModalCmd,
                                    SelectTaskCmd,
                                    SelectAllTasksCmd,
                                    SetDetailTabCmd,
                                    PauseTaskCmd,
                                    ResumeTaskCmd,
                                    RemoveTaskCmd,
                                    RemoveTasksCmd,
                                    RestartTaskCmd,
                                    RefreshTasksCmd,
                                    ResumeAllCmd,
                                    PauseAllCmd,
                                    PurgeStoppedCmd,
                                    ToggleSpeedLimitCmd,
                                    SetThemeCmd,
                                    ToggleThemeCmd,
                                    SetLocaleCmd,
                                    SubmitAddUriCmd,
                                    SubmitAddTorrentCmd,
                                    ApplyTaskFileSelectionCmd,
                                    SavePreferencesCmd,
                                    QueueDroppedFilesCmd,
                                    CopyTaskLinkCmd,
                                    ShowInFolderCmd,
                                    SyncTrackersCmd,
                                    ResetSessionCmd,
                                    FactoryResetCmd,
                                    HandleStartupPayloadCmd,
                                    HideToTrayCmd,
                                    ShowFromTrayCmd,
                                    QuitAppCmd,
                                    UpdateWindowBoundsCmd,
                                    HandleTrayActionCmd,
                                    StartAria2NextUpdateCmd>;

export class ICommandSink {
public:
    virtual ~ICommandSink() = default;
    virtual void Dispatch(Command cmd) = 0;
};

}  // namespace motrix::app

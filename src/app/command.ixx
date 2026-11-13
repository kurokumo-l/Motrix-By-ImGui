export module motrix.app:command;

import std;
import motrix.core;
import motrix.domain;

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
};

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

export struct RestartTaskCmd {
    std::string gid;
};

export struct RefreshTasksCmd {};
export struct ResumeAllCmd {};
export struct PauseAllCmd {};
export struct PurgeStoppedCmd {};

export struct SetThemeCmd {
    motrix::domain::ThemePreference theme = motrix::domain::ThemePreference::Light;
};

export struct ToggleThemeCmd {};

export struct SetLocaleCmd {
    motrix::core::Locale locale = motrix::core::Locale::EnUS;
};

export struct SubmitAddUriCmd {
    std::vector<std::string> uris;
    std::string outName;
    std::string dir;
    int split = 64;
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
    int split = 64;
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

export using Command = std::variant<SelectCategoryCmd,
                                    SelectSectionCmd,
                                    OpenModalCmd,
                                    CloseModalCmd,
                                    SelectTaskCmd,
                                    SetDetailTabCmd,
                                    PauseTaskCmd,
                                    ResumeTaskCmd,
                                    RemoveTaskCmd,
                                    RestartTaskCmd,
                                    RefreshTasksCmd,
                                    ResumeAllCmd,
                                    PauseAllCmd,
                                    PurgeStoppedCmd,
                                    SetThemeCmd,
                                    ToggleThemeCmd,
                                    SetLocaleCmd,
                                    SubmitAddUriCmd,
                                    SubmitAddTorrentCmd,
                                    ApplyTaskFileSelectionCmd,
                                    SavePreferencesCmd,
                                    QueueDroppedFilesCmd,
                                    CopyTaskLinkCmd,
                                    ShowInFolderCmd>;

export class ICommandSink {
public:
    virtual ~ICommandSink() = default;
    virtual void Dispatch(Command cmd) = 0;
};

}  // namespace motrix::app

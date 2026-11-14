export module motrix.ui:icons;

import std;
import :theme;

namespace motrix::ui {

export enum class MotrixIcon {
    LogoMini = 0,
    LogoFull,
    MenuTask,
    MenuAdd,
    MenuPreference,
    MenuAbout,
    TaskStart,
    TaskStartLine,
    TaskPause,
    TaskPauseLine,
    TaskStop,
    TaskStopLine,
    TaskRestart,
    Delete,
    Trash,
    Refresh,
    Purge,
    Folder,
    Link,
    InfoCircle,
    More,
    Speedometer,
    Magnet,
    ArrowUp,
    ArrowDown,
    Node,
    WinMinimize,
    WinMaximize,
    WinRestore,
    WinClose,
    Inbox,
    Video,
    Audio,
    Image,
    PreferenceBasic,
    PreferenceAdvanced,
    PreferenceLab,
    TaskHistory,
    Sync,
    Dice,
    ChevronDown,
    ChevronUp,
    TagClose,
};

export struct IconSpec {
    MotrixIcon icon = MotrixIcon::LogoMini;
    float x = 0.0f;
    float y = 0.0f;
    float width = 24.0f;
    float height = 24.0f;
    RgbaColor color{};
    float dpiScale = 1.0f;
    bool foreground = false;
};

export void DrawMotrixIcon(const IconSpec& spec);

export struct UfoIllustrationSpec {
    float centerX = 0.0f;
    float topY = 0.0f;
    float width = 380.0f;
    float height = 233.0f;
    bool dark = false;
};

export void DrawEmptyUfoIllustration(const UfoIllustrationSpec& spec);

}  // namespace motrix::ui

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
};

export void DrawMotrixIcon(MotrixIcon icon,
                           float x,
                           float y,
                           float width,
                           float height,
                           RgbaColor color,
                           float dpiScale = 1.0f,
                           bool foreground = false);

export void DrawEmptyUfoIllustration(float centerX, float topY, float width, float height, bool dark);

}  // namespace motrix::ui

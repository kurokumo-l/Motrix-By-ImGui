export module motrix.ui:scope_guard;

import std;
import :theme;
import :fonts;

namespace motrix::ui {

export class FontGuard {
public:
    FontGuard(const FontRegistry& fonts, FontRole role, float unscaledSize);
    ~FontGuard();

    FontGuard(const FontGuard&) = delete;
    FontGuard& operator=(const FontGuard&) = delete;
};

export class StyleColorGuard {
public:
    StyleColorGuard(int imguiCol, RgbaColor color);
    ~StyleColorGuard();

    StyleColorGuard(const StyleColorGuard&) = delete;
    StyleColorGuard& operator=(const StyleColorGuard&) = delete;
};

export class StyleVarGuard {
public:
    StyleVarGuard(int imguiVar, float value);
    StyleVarGuard(int imguiVar, float x, float y);
    ~StyleVarGuard();

    StyleVarGuard(const StyleVarGuard&) = delete;
    StyleVarGuard& operator=(const StyleVarGuard&) = delete;
};

export class IdGuard {
public:
    explicit IdGuard(int intId);
    explicit IdGuard(std::string_view strId);
    ~IdGuard();

    IdGuard(const IdGuard&) = delete;
    IdGuard& operator=(const IdGuard&) = delete;
};

export class WindowGuard {
public:
    WindowGuard(const char* name, bool* open, int windowFlags);
    ~WindowGuard();

    [[nodiscard]] bool IsOpen() const noexcept { return open_; }

    WindowGuard(const WindowGuard&) = delete;
    WindowGuard& operator=(const WindowGuard&) = delete;

private:
    bool open_ = false;
};

export class ChildGuard {
public:
    ChildGuard(const char* id, float width, float height, int childFlags, int windowFlags);
    ~ChildGuard();

    [[nodiscard]] bool IsVisible() const noexcept { return visible_; }

    ChildGuard(const ChildGuard&) = delete;
    ChildGuard& operator=(const ChildGuard&) = delete;

private:
    bool visible_ = false;
};

export class PopupModalGuard {
public:
    PopupModalGuard(const char* title, bool* open, int windowFlags);
    ~PopupModalGuard();

    [[nodiscard]] bool IsOpen() const noexcept { return open_; }

    PopupModalGuard(const PopupModalGuard&) = delete;
    PopupModalGuard& operator=(const PopupModalGuard&) = delete;

private:
    bool open_ = false;
};

}  // namespace motrix::ui

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

export struct ChildWindowSpec {
    float width = 0.0f;
    float height = 0.0f;
    int childFlags = 0;
    int windowFlags = 0;
};

export class ChildGuard {
public:
    ChildGuard(const char* id, const ChildWindowSpec& spec = {});
    ~ChildGuard();

    [[nodiscard]] bool IsVisible() const noexcept { return visible_; }

    ChildGuard(const ChildGuard&) = delete;
    ChildGuard& operator=(const ChildGuard&) = delete;

private:
    bool visible_ = false;
};

export class PopupGuard {
public:
    explicit PopupGuard(const char* id, int windowFlags = 0);
    ~PopupGuard();

    [[nodiscard]] bool IsOpen() const noexcept { return open_; }

    PopupGuard(const PopupGuard&) = delete;
    PopupGuard& operator=(const PopupGuard&) = delete;

private:
    bool open_ = false;
};

export struct TableSpec {
    int columns = 1;
    int flags = 0;
    float width = 0.0f;
    float height = 0.0f;
};

export class TableGuard {
public:
    TableGuard(const char* id, const TableSpec& spec);
    ~TableGuard();

    [[nodiscard]] bool IsOpen() const noexcept { return open_; }

    TableGuard(const TableGuard&) = delete;
    TableGuard& operator=(const TableGuard&) = delete;

private:
    bool open_ = false;
};

export class GroupGuard {
public:
    GroupGuard();
    ~GroupGuard();

    GroupGuard(const GroupGuard&) = delete;
    GroupGuard& operator=(const GroupGuard&) = delete;
};

export class TextWrapGuard {
public:
    explicit TextWrapGuard(float wrapLocalPosX = 0.0f);
    ~TextWrapGuard();

    TextWrapGuard(const TextWrapGuard&) = delete;
    TextWrapGuard& operator=(const TextWrapGuard&) = delete;
};

export struct ClipRectSpec {
    float minX = 0.0f;
    float minY = 0.0f;
    float maxX = 0.0f;
    float maxY = 0.0f;
    bool intersect = true;
};

export class ClipRectGuard {
public:
    ClipRectGuard(void* drawList, const ClipRectSpec& spec);
    ~ClipRectGuard();

    ClipRectGuard(const ClipRectGuard&) = delete;
    ClipRectGuard& operator=(const ClipRectGuard&) = delete;

private:
    void* drawList_ = nullptr;
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

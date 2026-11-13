export module motrix.ui:fonts;

import std;

namespace motrix::ui {

export enum class FontRole {
    Regular = 0,
    Medium,
    Semibold,
    Bold,
    Mono,
};

export class FontRegistry {
public:
    bool LoadAll(const std::filesystem::path& assetsDir);
    [[nodiscard]] bool IsLoaded() const noexcept { return loaded_; }
    [[nodiscard]] void* FontPtr(FontRole role) const noexcept;

private:
    void* regular_ = nullptr;
    void* medium_ = nullptr;
    void* semibold_ = nullptr;
    void* bold_ = nullptr;
    void* mono_ = nullptr;
    bool loaded_ = false;
};

export [[nodiscard]] std::filesystem::path ResolveAssetsFontsDir();

}  // namespace motrix::ui

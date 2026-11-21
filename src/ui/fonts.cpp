module;

// clang-format off
#include <windows.h>
#include <imgui.h>
// clang-format on

module motrix.ui;

import std;
import motrix.core;
import :fonts;
import :fonts_dwrite;

namespace motrix::ui {

namespace {

constexpr float kRefFontSize = 15.0f;
constexpr std::string_view kDefaultUiFamily = "Microsoft YaHei UI";
constexpr std::array<std::string_view, 3> kFontExtensions{".otf", ".ttf", ".ttc"};

struct GenericUiFontFiles {
    std::optional<std::filesystem::path> regular;
    std::optional<std::filesystem::path> medium;
    std::optional<std::filesystem::path> semibold;
    std::optional<std::filesystem::path> bold;
    std::optional<std::filesystem::path> mono;
};

struct ResolvedFontSource {
    std::optional<std::filesystem::path> filePath;
    std::string sysFamily;
};

[[nodiscard]] std::optional<std::filesystem::path> FindGenericFontFile(const std::filesystem::path& dir,
                                                                       std::string_view stem) {
    if (dir.empty()) {
        return std::nullopt;
    }
    std::error_code ec;
    for (const std::string_view ext : kFontExtensions) {
        std::string filename;
        filename.reserve(stem.size() + ext.size());
        filename.append(stem);
        filename.append(ext);
        const auto candidate = dir / motrix::core::PathFromUtf8(filename);
        if (std::filesystem::is_regular_file(candidate, ec)
            && std::filesystem::file_size(candidate, ec) > 0u) {
            return candidate;
        }
    }
    return std::nullopt;
}

[[nodiscard]] GenericUiFontFiles ProbeGenericAssetFonts(const std::filesystem::path& assetsDir) {
    GenericUiFontFiles out;
    out.regular = FindGenericFontFile(assetsDir, "ui-regular");
    if (!out.regular) {
        out.regular = FindGenericFontFile(assetsDir, "ui");
    }
    out.medium = FindGenericFontFile(assetsDir, "ui-medium");
    if (!out.medium) {
        out.medium = out.regular;
    }
    out.bold = FindGenericFontFile(assetsDir, "ui-bold");
    out.semibold = FindGenericFontFile(assetsDir, "ui-semibold");
    if (!out.semibold) {
        out.semibold = out.bold ? out.bold : out.medium;
    }
    if (!out.bold) {
        out.bold = out.semibold ? out.semibold : out.regular;
    }
    out.mono = FindGenericFontFile(assetsDir, "ui-mono");
    return out;
}

[[nodiscard]] ImFontConfig
MakeBaseFontConfig(bool mergeMode, ImFont* dstFont, std::string_view debugName) noexcept {
    ImFontConfig cfg;
    cfg.MergeMode = mergeMode;
    cfg.DstFont = dstFont;
    cfg.PixelSnapH = true;
    cfg.OversampleH = 1;
    cfg.OversampleV = 1;
    cfg.SizePixels = kRefFontSize;
    cfg.FontDataOwnedByAtlas = true;
    const std::size_t copyLen = std::min(debugName.size(), sizeof(cfg.Name) - 1u);
    if (copyLen > 0) {
        std::memcpy(cfg.Name, debugName.data(), copyLen);
        cfg.Name[copyLen] = '\0';
    }
    return cfg;
}

[[nodiscard]] ImFont*
AddFontFile(ImFontAtlas& atlas, const std::filesystem::path& path, ImFontConfig cfg) {
    std::error_code ec;
    const auto fileSize = std::filesystem::file_size(path, ec);
    if (ec || fileSize == 0u || fileSize > static_cast<std::uintmax_t>(64u * 1024u * 1024u)) {
        return nullptr;
    }
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return nullptr;
    }
    const std::size_t byteCount = static_cast<std::size_t>(fileSize);
    void* buffer = IM_ALLOC(byteCount);
    if (!buffer) {
        return nullptr;
    }
    if (!file.read(static_cast<char*>(buffer), static_cast<std::streamsize>(byteCount))) {
        IM_FREE(buffer);
        return nullptr;
    }
    cfg.FontData = buffer;
    cfg.FontDataSize = static_cast<int>(byteCount);
    cfg.FontDataOwnedByAtlas = true;
    return atlas.AddFont(&cfg);
}

[[nodiscard]] ImFont*
AddSystemFont(ImFontAtlas& atlas, int weight, std::string_view familyUtf8, ImFontConfig cfg) {
    if (familyUtf8.empty()) {
        return nullptr;
    }
    const std::string spec = std::format("MOTRIX_SYS:{}:{}", weight, familyUtf8);
    void* buffer = IM_ALLOC(spec.size());
    if (!buffer) {
        return nullptr;
    }
    std::memcpy(buffer, spec.data(), spec.size());
    cfg.FontData = buffer;
    cfg.FontDataSize = static_cast<int>(spec.size());
    cfg.FontDataOwnedByAtlas = true;
    return atlas.AddFont(&cfg);
}

[[nodiscard]] ResolvedFontSource ResolveRequestedSource(std::string_view requestedUtf8,
                                                        std::string_view fallbackSys) {
    const std::string_view trimmed = motrix::core::TrimAscii(requestedUtf8);
    if (!trimmed.empty()) {
        std::error_code ec;
        const auto asPath = motrix::core::PathFromUtf8(trimmed);
        if (std::filesystem::is_regular_file(asPath, ec)
            && std::filesystem::file_size(asPath, ec) > 0u) {
            return {asPath, {}};
        }
        if (HasDWriteSystemFontFamily(trimmed)) {
            return {std::nullopt, std::string(trimmed)};
        }
    }
    if (!fallbackSys.empty() && HasDWriteSystemFontFamily(fallbackSys)) {
        return {std::nullopt, std::string(fallbackSys)};
    }
    if (HasDWriteSystemFontFamily("Segoe UI")) {
        return {std::nullopt, "Segoe UI"};
    }
    return {};
}

[[nodiscard]] ImFont* LoadFontWithFallback(ImFontAtlas& atlas,
                                           const std::optional<std::filesystem::path>& filePath,
                                           std::string_view sysFamily,
                                           int weight) {
    ImFont* font = nullptr;
    if (filePath) {
        const std::string label = motrix::core::PathToUtf8(filePath->filename());
        font = AddFontFile(atlas, *filePath, MakeBaseFontConfig(false, nullptr, label));
    }
    if (!font && !sysFamily.empty()) {
        font = AddSystemFont(atlas, weight, sysFamily, MakeBaseFontConfig(false, nullptr, sysFamily));
    }
    if (!font) {
        return nullptr;
    }
    const bool needsCjkFallback =
        filePath.has_value() || !motrix::core::EqualsAsciiInsensitive(sysFamily, kDefaultUiFamily);
    if (needsCjkFallback && HasDWriteSystemFontFamily(kDefaultUiFamily)) {
        (void)AddSystemFont(
            atlas, weight, kDefaultUiFamily, MakeBaseFontConfig(true, font, "YaHeiUI-CJK"));
    }
    return font;
}

[[nodiscard]] ImFont* LoadMonoFont(ImFontAtlas& atlas,
                                   const ResolvedFontSource& monoSrc,
                                   const ResolvedFontSource& uiFallback) {
    ImFont* mono = nullptr;
    if (monoSrc.filePath) {
        const std::string label = motrix::core::PathToUtf8(monoSrc.filePath->filename());
        mono = AddFontFile(atlas, *monoSrc.filePath, MakeBaseFontConfig(false, nullptr, label));
    } else if (!monoSrc.sysFamily.empty()) {
        mono = AddSystemFont(
            atlas, 400, monoSrc.sysFamily, MakeBaseFontConfig(false, nullptr, monoSrc.sysFamily));
    }
    if (!mono) {
        return LoadFontWithFallback(atlas, uiFallback.filePath, uiFallback.sysFamily, 400);
    }
    if (uiFallback.filePath) {
        const std::string label = motrix::core::PathToUtf8(uiFallback.filePath->filename());
        (void)AddFontFile(atlas, *uiFallback.filePath, MakeBaseFontConfig(true, mono, label));
    } else if (!uiFallback.sysFamily.empty()) {
        (void)AddSystemFont(
            atlas, 400, uiFallback.sysFamily, MakeBaseFontConfig(true, mono, uiFallback.sysFamily));
    }
    if ((uiFallback.filePath.has_value()
         || !motrix::core::EqualsAsciiInsensitive(uiFallback.sysFamily, kDefaultUiFamily))
        && HasDWriteSystemFontFamily(kDefaultUiFamily)) {
        (void)AddSystemFont(atlas, 400, kDefaultUiFamily, MakeBaseFontConfig(true, mono, "YaHeiUI-CJK"));
    }
    return mono;
}

[[nodiscard]] bool HasFontAssets(const std::filesystem::path& dir) {
    std::error_code ec;
    return FindGenericFontFile(dir, "ui-mono").has_value()
           || FindGenericFontFile(dir, "ui-regular").has_value()
           || FindGenericFontFile(dir, "ui").has_value()
           || std::filesystem::is_regular_file(dir / "lucide.ttf", ec);
}

}  // namespace

std::filesystem::path ResolveAssetsFontsDir() {
    const auto exeDir = motrix::core::CurrentExecutablePath().parent_path();

    const std::array<std::filesystem::path, 4> candidates{
        exeDir / "assets" / "fonts",
        exeDir.parent_path() / "assets" / "fonts",
        exeDir.parent_path().parent_path() / "assets" / "fonts",
        std::filesystem::current_path() / "assets" / "fonts",
    };

    for (const auto& dir : candidates) {
        if (HasFontAssets(dir)) {
            return dir;
        }
    }
    return {};
}

bool IsFontFamilyInstalled(std::string_view familyUtf8) {
    return HasDWriteSystemFontFamily(familyUtf8);
}

bool FontRegistry::LoadAll(const std::filesystem::path& assetsDir,
                           std::string_view fontFamily,
                           std::string_view monoFontFamily) {
    ImGuiIO& io = ImGui::GetIO();
    ImFontAtlas* atlas = io.Fonts;
    if (!atlas) {
        return false;
    }

    if (const ImFontLoader* dwriteLoader = GetDWriteFontLoader()) {
        atlas->SetFontLoader(dwriteLoader);
    }

    const GenericUiFontFiles assetFonts = ProbeGenericAssetFonts(assetsDir);
    const ResolvedFontSource primarySrc = assetFonts.regular
                                              ? ResolvedFontSource{assetFonts.regular, {}}
                                              : ResolveRequestedSource(fontFamily, kDefaultUiFamily);

    if (assetFonts.regular) {
        regular_ = LoadFontWithFallback(*atlas, assetFonts.regular, {}, 400);
        medium_ = LoadFontWithFallback(*atlas, assetFonts.medium, {}, 500);
        semibold_ = LoadFontWithFallback(*atlas, assetFonts.semibold, {}, 600);
        bold_ = LoadFontWithFallback(*atlas, assetFonts.bold, {}, 700);
    } else {
        regular_ = LoadFontWithFallback(*atlas, primarySrc.filePath, primarySrc.sysFamily, 400);
        medium_ = LoadFontWithFallback(*atlas, primarySrc.filePath, primarySrc.sysFamily, 500);
        semibold_ = LoadFontWithFallback(*atlas, primarySrc.filePath, primarySrc.sysFamily, 600);
        bold_ = LoadFontWithFallback(*atlas, primarySrc.filePath, primarySrc.sysFamily, 700);
    }

    ResolvedFontSource monoSrc;
    const std::string_view trimmedMono = motrix::core::TrimAscii(monoFontFamily);
    if (!trimmedMono.empty()) {
        monoSrc = ResolveRequestedSource(trimmedMono, {});
    }
    if (!monoSrc.filePath && monoSrc.sysFamily.empty()) {
        if (assetFonts.mono) {
            monoSrc.filePath = assetFonts.mono;
        } else if (HasDWriteSystemFontFamily("Consolas")) {
            monoSrc.sysFamily = "Consolas";
        }
    }
    mono_ = LoadMonoFont(*atlas, monoSrc, primarySrc);

    if (!regular_) {
        regular_ = atlas->AddFontDefault();
    }
    if (!medium_) {
        medium_ = regular_;
    }
    if (!semibold_) {
        semibold_ = medium_;
    }
    if (!bold_) {
        bold_ = semibold_;
    }
    if (!mono_) {
        mono_ = regular_;
    }
    io.FontDefault = static_cast<ImFont*>(regular_);
    loaded_ = (regular_ != nullptr && medium_ != nullptr && semibold_ != nullptr && bold_ != nullptr
               && mono_ != nullptr);
    return loaded_;
}

void* FontRegistry::FontPtr(FontRole role) const noexcept {
    switch (role) {
        case FontRole::Regular: return regular_;
        case FontRole::Medium: return medium_;
        case FontRole::Semibold: return semibold_;
        case FontRole::Bold: return bold_;
        case FontRole::Mono: return mono_;
    }
    return regular_;
}

}  // namespace motrix::ui

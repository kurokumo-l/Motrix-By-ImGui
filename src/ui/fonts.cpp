module;

#include "imgui.h"
#include <windows.h>

module motrix.ui;

import std;
import motrix.core;

namespace motrix::ui {

namespace {

[[nodiscard]] std::string FindWindowsCjkFont(bool bold) {
    const char* candidates[] = {
        bold ? "C:\\Windows\\Fonts\\msyhbd.ttc" : "C:\\Windows\\Fonts\\msyh.ttc",
        "C:\\Windows\\Fonts\\msyh.ttc",
        "C:\\Windows\\Fonts\\simhei.ttf",
    };
    std::error_code ec;
    for (const char* p : candidates) {
        if (std::filesystem::exists(p, ec)) {
            return p;
        }
    }
    return {};
}

[[nodiscard]] ImFont* LoadFontWithCjkFallback(ImFontAtlas* atlas,
                                              const std::filesystem::path& primaryFile,
                                              const std::string& cjkFile,
                                              float refSize) {
    ImFontConfig primaryCfg;
    primaryCfg.OversampleH = 2;
    primaryCfg.OversampleV = 2;
    primaryCfg.ExtraSizeScale = 1.06f;

    ImFont* font = nullptr;
    std::error_code ec;
    if (!primaryFile.empty() && std::filesystem::exists(primaryFile, ec)) {
        const std::string utf8Path = motrix::core::PathToUtf8(primaryFile);
        font = atlas->AddFontFromFileTTF(utf8Path.c_str(), refSize, &primaryCfg);
    }
    if (!font) {
        font = atlas->AddFontDefault(&primaryCfg);
    }

    if (!cjkFile.empty()) {
        ImFontConfig cjkCfg;
        cjkCfg.MergeMode = true;
        cjkCfg.OversampleH = 2;
        cjkCfg.OversampleV = 2;
        cjkCfg.ExtraSizeScale = 1.24f;
        atlas->AddFontFromFileTTF(cjkFile.c_str(), refSize, &cjkCfg);
    }
    return font;
}

}  // namespace

std::filesystem::path ResolveAssetsFontsDir() {
    wchar_t exeBuf[MAX_PATH] = {};
    ::GetModuleFileNameW(nullptr, exeBuf, MAX_PATH);
    const std::filesystem::path exeDir = std::filesystem::path(exeBuf).parent_path();

    const std::filesystem::path candidates[] = {
        exeDir / "assets" / "fonts",
        exeDir.parent_path() / "assets" / "fonts",
        exeDir.parent_path().parent_path() / "assets" / "fonts",
        std::filesystem::current_path() / "assets" / "fonts",
    };
    std::error_code ec;
    for (const auto& dir : candidates) {
        if (std::filesystem::exists(dir / "Inter-Regular.ttf", ec)) {
            return dir;
        }
    }
    return {};
}

bool FontRegistry::LoadAll(const std::filesystem::path& assetsDir) {
    ImFontAtlas* atlas = ImGui::GetIO().Fonts;
    atlas->TexMinWidth = 1024;
    atlas->TexMinHeight = 1024;

    const std::string cjkRegular = FindWindowsCjkFont(false);
    const std::string cjkBold = FindWindowsCjkFont(true);
    constexpr float kRefSize = 15.0f;

    const auto at = [&](std::string_view file) -> std::filesystem::path {
        return assetsDir.empty() ? std::filesystem::path{} : (assetsDir / file);
    };

    regular_ = LoadFontWithCjkFallback(atlas, at("Inter-Regular.ttf"), cjkRegular, kRefSize);
    medium_ = LoadFontWithCjkFallback(atlas, at("Inter-Medium.ttf"), cjkRegular, kRefSize);
    semibold_ = LoadFontWithCjkFallback(atlas, at("Inter-SemiBold.ttf"), cjkBold, kRefSize);
    bold_ = LoadFontWithCjkFallback(atlas, at("Inter-Bold.ttf"), cjkBold, kRefSize);
    mono_ = LoadFontWithCjkFallback(atlas, at("JetBrainsMono-Regular.ttf"), cjkRegular, kRefSize);

    loaded_ = (regular_ != nullptr);
    return loaded_;
}

void* FontRegistry::FontPtr(FontRole role) const noexcept {
    switch (role) {
        case FontRole::Regular: return regular_;
        case FontRole::Medium: return medium_ ? medium_ : regular_;
        case FontRole::Semibold: return semibold_ ? semibold_ : regular_;
        case FontRole::Bold: return bold_ ? bold_ : regular_;
        case FontRole::Mono: return mono_ ? mono_ : regular_;
    }
    return regular_;
}

}  // namespace motrix::ui

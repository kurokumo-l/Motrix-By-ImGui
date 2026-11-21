module;

#include "imgui.h"

module motrix.ui;

import std;
import motrix.core;
import motrix.domain;
import motrix.app;
import :views_internal;
import :el_widgets;
import :theme;
import :anim;
import :icons;
import :scope_guard;

namespace motrix::ui {

namespace {

struct AboutCardMetrics {
    ImVec2 winPos{};
    float modalW = 632.0f;
    float x0 = 0.0f;
    float bodyW = 0.0f;
    float s = 1.0f;
};

void RenderAboutTopSection(UiFrameContext& ctx, const AboutCardMetrics& m, float& curY) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float closeSize = 24.0f * m.s;
    const float closeX = m.winPos.x + m.modalW - 20.0f * m.s - closeSize;
    const float closeY = m.winPos.y + 18.0f * m.s;
    ImGui::SetCursorScreenPos(ImVec2(closeX, closeY));
    ImGui::InvisibleButton("##about_close", ImVec2(closeSize, closeSize));
    if (ImGui::IsItemClicked()) {
        ctx.sink.Dispatch(motrix::app::CloseModalCmd{});
        ImGui::CloseCurrentPopup();
    }
    const bool closeHov = ImGui::IsItemHovered();
    if (closeHov) {
        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    }
    DrawMotrixIcon(IconSpec{MotrixIcon::WinClose,
                            closeX + (closeSize - 11.0f * m.s) * 0.5f,
                            closeY + (closeSize - 11.0f * m.s) * 0.5f,
                            11.0f * m.s,
                            11.0f * m.s,
                            closeHov ? ctx.palette.primary : ctx.palette.taskMetaText,
                            m.s});

    curY += 20.0f * m.s;
    const float iconSize = 128.0f * m.s;
    const float iconX = m.x0 + m.bodyW - iconSize;
    DrawMotrixAppIcon(
        AppIconSpec{iconX, curY, iconSize, ctx.palette.isDark, &ctx.host, &ctx.anim.AppIconCache()});

    const float logoW = 93.0f * m.s;
    const float logoH = 21.0f * m.s;
    const RgbaColor logoCol = ctx.palette.isDark ? HexColor(0xEEEEEE) : HexColor(0x4D515A);
    DrawMotrixIcon(IconSpec{MotrixIcon::LogoFull, m.x0, curY, logoW, logoH, logoCol, m.s});

    const RgbaColor verCol = ctx.palette.isDark ? HexColor(0xA2A3A4) : HexColor(0x606266);
    {
        FontGuard verF(ctx.fonts, FontRole::Regular, 18.0f);
        const ImVec2 vsz = ImGui::CalcTextSize("By ImGui  v0.1.2");
        dl->AddText(ImVec2(m.x0 + logoW + 20.0f * m.s, curY + logoH - vsz.y + 2.0f * m.s),
                    ToAbgrU32(verCol),
                    "By ImGui  v0.1.2");
    }
    curY += logoH + 50.0f * m.s;
}

void RenderAboutEngineSection(UiFrameContext& ctx, const AboutCardMetrics& m, float& curY) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const auto& ei = ctx.state.engineInfo;
    std::string verPart = "--";
    if (!ei.version.empty()) {
        verPart = (ei.product.empty() || ei.product == "aria2")
                      ? ei.version
                      : std::format("{} {}", ei.product, ei.version);
    }
    const std::string heading = std::format("{} {}", ctx.tr.Tr("about.engine_version"), verPart);
    const float listX = m.x0 + 8.0f * m.s;
    const RgbaColor titleCol = ctx.palette.isDark ? HexColor(0xA2A3A4) : HexColor(0x606266);
    {
        FontGuard ef(ctx.fonts, FontRole::Regular, 14.0f);
        dl->AddText(ImVec2(listX, curY), ToAbgrU32(titleCol), heading.c_str());
    }
    curY += 32.0f * m.s;

    constexpr std::array<std::string_view, 9> kFallbackFeats{"Async DNS",
                                                             "BitTorrent",
                                                             "Firefox3 Cookie",
                                                             "GZip",
                                                             "HTTPS",
                                                             "Message Digest",
                                                             "Metalink",
                                                             "XML-RPC",
                                                             "SFTP"};
    const std::size_t featCount =
        ei.enabledFeatures.empty() ? kFallbackFeats.size() : ei.enabledFeatures.size();
    const float listW = (m.bodyW - 8.0f * m.s) * 0.65f;
    const float colW = listW * 0.5f;
    const RgbaColor featCol = ctx.palette.isDark ? HexColor(0x777777) : HexColor(0x909399);
    {
        FontGuard ff(ctx.fonts, FontRole::Regular, 12.0f);
        for (std::size_t i = 0; i < featCount; ++i) {
            const std::string_view fName =
                ei.enabledFeatures.empty() ? kFallbackFeats[i] : std::string_view(ei.enabledFeatures[i]);
            const float fx = listX + static_cast<float>(i % 2) * colW;
            const float fy = curY + static_cast<float>(i / 2) * 20.0f * m.s;
            dl->AddText(ImVec2(fx, fy), ToAbgrU32(featCol), fName.data(), fName.data() + fName.size());
        }
    }
    const std::size_t rows = (featCount + 1) / 2;
    curY += static_cast<float>(rows) * 20.0f * m.s + 38.0f * m.s;
}

void RenderAboutFooter(UiFrameContext& ctx, const AboutCardMetrics& m, float& curY) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const RgbaColor footCol = ctx.palette.isDark ? HexColor(0xA2A3A4) : HexColor(0x606266);
    {
        FontGuard cf(ctx.fonts, FontRole::Regular, 12.0f);
        dl->AddText(ImVec2(m.x0, curY), ToAbgrU32(footCol), "©2026 Motrix By ImGui");
    }

    struct FooterLink {
        const char* id;
        std::string_view key;
        const char* url;
    };
    constexpr std::array<FooterLink, 4> kLinks{{
        {"##lnk_lic", "about.license", "https://motrix.app/license"},
        {"##lnk_abt", "about.about", "https://motrix.app/about"},
        {"##lnk_sup", "about.support", "https://motrix.app/support"},
        {"##lnk_rel", "about.release", "https://motrix.app/release"},
    }};

    const ElStyle st{ctx.palette, ctx.fonts, m.s, &ctx.anim};
    float widths[4]{};
    float totalW = 0.0f;
    {
        FontGuard lf(ctx.fonts, FontRole::Regular, 12.0f);
        for (std::size_t i = 0; i < kLinks.size(); ++i) {
            const std::string_view lbl = ctx.tr.Tr(kLinks[i].key);
            widths[i] = ImGui::CalcTextSize(lbl.data(), lbl.data() + lbl.size()).x;
            totalW += widths[i];
        }
    }
    const float gap = 30.0f * m.s;
    float linkX = m.x0 + m.bodyW - totalW - gap * 3.0f;
    for (std::size_t i = 0; i < kLinks.size(); ++i) {
        IdGuard gid(kLinks[i].id);
        ImGui::SetCursorScreenPos(ImVec2(linkX, curY));
        const char* url = kLinks[i].url;
        ElDrawLinkText(st,
                       ElLinkSpec{ctx.tr.Tr(kLinks[i].key),
                                  [&ctx, url]() { ctx.host.OpenPathInExplorer(url); },
                                  12.0f,
                                  false});
        linkX += widths[i] + gap;
    }
    curY += 36.0f * m.s;
}

}  // namespace

void RenderAboutModalDialog(UiFrameContext& ctx, bool justOpened) {
    const float s = ctx.dpiScale;
    const float openT = SmoothOpenProgress(ctx.anim, HashId("modal.about.open"), justOpened);
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    const float modalW = std::clamp(vp->Size.x * 0.618f, 380.0f * s, 632.0f * s);
    const float openOffsetY = 6.0f * s * (1.0f - openT);
    ImGui::SetNextWindowPos(
        ImVec2(vp->Pos.x + vp->Size.x * 0.5f, vp->Pos.y + vp->Size.y * 0.5f + openOffsetY),
        ImGuiCond_Always,
        ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(modalW, 0.0f), ImGuiCond_Always);

    StyleVarGuard winPad(ImGuiStyleVar_WindowPadding, 0.0f, 0.0f);
    StyleVarGuard winRound(ImGuiStyleVar_WindowRounding, 4.0f * s);
    StyleVarGuard winBorder(ImGuiStyleVar_WindowBorderSize, 0.0f);
    StyleColorGuard popBg(ImGuiCol_PopupBg, ctx.palette.modalBg);
    StyleColorGuard dimBg(ImGuiCol_ModalWindowDimBg, RgbaColor{0.0f, 0.0f, 0.0f, 0.50f * openT});

    constexpr ImGuiWindowFlags kFlags = ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoTitleBar
                                        | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings;
    PopupModalGuard modal("##AboutModal", nullptr, kFlags);
    if (!modal.IsOpen()) {
        return;
    }

    const ImVec2 winPos = ImGui::GetWindowPos();
    const float padX = 28.0f * s;
    const AboutCardMetrics metrics{winPos, modalW, winPos.x + padX, modalW - padX * 2.0f, s};
    float curY = winPos.y + 24.0f * s;

    RenderAboutTopSection(ctx, metrics, curY);
    RenderAboutEngineSection(ctx, metrics, curY);
    RenderAboutFooter(ctx, metrics, curY);

    ImGui::SetCursorScreenPos(ImVec2(winPos.x, curY));
    ImGui::Dummy(ImVec2(modalW, 0.0f));
    if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
        ctx.sink.Dispatch(motrix::app::CloseModalCmd{});
        ImGui::CloseCurrentPopup();
    }
}

}  // namespace motrix::ui

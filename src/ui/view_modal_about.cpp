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
import :icons;
import :scope_guard;

namespace motrix::ui {

namespace {

struct CaptionPos {
    float x = 0.0f;
    float y = 0.0f;
    float h = 0.0f;
};

struct AboutLayout {
    ImVec2 winPos{};
    float modalW = 480.0f;
    float padX = 28.0f;
    float x0 = 0.0f;
    float bodyW = 424.0f;
};

void DrawAlignedFieldCaption(const UiFrameContext& ctx, const CaptionPos& pos, std::string_view text) {
    FontGuard fnt(ctx.fonts, FontRole::Regular, 13.5f);
    const ImVec2 sz = ImGui::CalcTextSize(text.data(), text.data() + text.size());
    ImGui::GetWindowDrawList()->AddText(ImVec2(pos.x, pos.y + (pos.h - sz.y) * 0.5f),
                                        ToAbgrU32(ctx.palette.subLabelText),
                                        text.data(),
                                        text.data() + text.size());
}

void RenderAboutHeader(UiFrameContext& ctx, const AboutLayout& lay, float& curY) {
    const float s = ctx.dpiScale;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float closeBox = 24.0f * s;
    const float closeX = lay.winPos.x + lay.modalW - 14.0f * s - closeBox;
    const float closeY = lay.winPos.y + 14.0f * s;
    ImGui::SetCursorScreenPos(ImVec2(closeX, closeY));
    if (ImGui::InvisibleButton("##about_close", ImVec2(closeBox, closeBox))) {
        ctx.sink.Dispatch(motrix::app::CloseModalCmd{});
        ImGui::CloseCurrentPopup();
    }
    const bool closeHov = ImGui::IsItemHovered();
    DrawMotrixIcon(IconSpec{
        MotrixIcon::WinClose,
        closeX + 6.5f * s,
        closeY + 6.5f * s,
        11.0f * s,
        11.0f * s,
        closeHov ? ctx.palette.primary : ctx.palette.taskMetaText,
        s,
    });

    curY += 20.0f * s;
    {
        FontGuard titleF(ctx.fonts, FontRole::Medium, 24.0f);
        dl->AddText(ImVec2(lay.x0, curY), ToAbgrU32(ctx.palette.panelTitle), "Motrix");
    }
    {
        FontGuard verF(ctx.fonts, FontRole::Regular, 13.0f);
        dl->AddText(ImVec2(lay.x0, curY + 32.0f * s),
                    ToAbgrU32(ctx.palette.taskMetaText),
                    "Version 1.8.19  (Native C++23)");
    }
    DrawMotrixIcon(IconSpec{
        MotrixIcon::LogoFull,
        lay.x0 + lay.bodyW - 56.0f * s,
        curY + 4.0f * s,
        48.0f * s,
        48.0f * s,
        ctx.palette.primary,
        s,
    });
    curY += 68.0f * s;
}

void RenderAboutEngineCard(UiFrameContext& ctx, const AboutLayout& lay, float& curY) {
    const float s = ctx.dpiScale;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const auto& ei = ctx.state.engineInfo;
    const std::string verStr =
        ei.version.empty() ? "--"
                           : std::format("{} {}", ei.product.empty() ? "aria2" : ei.product, ei.version);
    constexpr std::array<std::string_view, 8> kFallbackFeats{
        "BitTorrent",
        "Firefox3 Cookie",
        "GZip",
        "HTTPS",
        "Message Digest",
        "Metalink",
        "XML-RPC",
        "SFTP",
    };
    const std::size_t featCount =
        ei.enabledFeatures.empty() ? kFallbackFeats.size() : ei.enabledFeatures.size();
    const std::size_t rows = (featCount + 1) / 2;
    const float cardH = 44.0f * s + static_cast<float>(rows) * 20.0f * s + 12.0f * s;

    dl->AddRectFilled(ImVec2(lay.x0, curY),
                      ImVec2(lay.x0 + lay.bodyW, curY + cardH),
                      ToAbgrU32(ctx.palette.subnavBg),
                      6.0f * s);
    {
        FontGuard ef(ctx.fonts, FontRole::Medium, 13.5f);
        const std::string_view evLbl = ctx.tr.Tr("about.engine_version");
        dl->AddText(ImVec2(lay.x0 + 16.0f * s, curY + 14.0f * s),
                    ToAbgrU32(ctx.palette.panelTitle),
                    evLbl.data(),
                    evLbl.data() + evLbl.size());
        const ImVec2 vsz = ImGui::CalcTextSize(verStr.c_str());
        dl->AddText(ImVec2(lay.x0 + lay.bodyW - 16.0f * s - vsz.x, curY + 14.0f * s),
                    ToAbgrU32(ctx.palette.taskMetaText),
                    verStr.c_str());
    }
    dl->AddLine(ImVec2(lay.x0 + 16.0f * s, curY + 40.0f * s),
                ImVec2(lay.x0 + lay.bodyW - 16.0f * s, curY + 40.0f * s),
                ToAbgrU32(ctx.palette.panelBorder),
                1.0f);

    {
        FontGuard ff(ctx.fonts, FontRole::Regular, 12.5f);
        const float col2X = lay.x0 + lay.bodyW * 0.5f + 8.0f * s;
        for (std::size_t i = 0; i < featCount; ++i) {
            const std::string_view fName =
                ei.enabledFeatures.empty() ? kFallbackFeats[i] : std::string_view(ei.enabledFeatures[i]);
            const float fx = (i % 2 == 0) ? (lay.x0 + 16.0f * s) : col2X;
            const float fy = curY + 48.0f * s + static_cast<float>(i / 2) * 20.0f * s;
            dl->AddText(ImVec2(fx, fy),
                        ToAbgrU32(ctx.palette.subLabelText),
                        fName.data(),
                        fName.data() + fName.size());
        }
    }
    curY += cardH + 22.0f * s;
}

void RenderAboutFooter(UiFrameContext& ctx, const AboutLayout& lay, float curY) {
    const float s = ctx.dpiScale;
    const float footerH = 52.0f * s;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(lay.winPos.x > -1e9f ? ImVec2(lay.winPos.x, curY) : ImVec2(0.0f, curY),
                      ImVec2(lay.winPos.x + lay.modalW, curY + footerH),
                      ToAbgrU32(ctx.palette.modalFooterBg),
                      6.0f * s,
                      ImDrawFlags_RoundCornersBottom);
    {
        FontGuard cf(ctx.fonts, FontRole::Regular, 12.0f);
        dl->AddText(
            ImVec2(lay.x0, curY + 18.0f * s), ToAbgrU32(ctx.palette.taskMetaText), "© 2018-2026 Motrix");
    }

    float linkX = lay.x0 + lay.bodyW - 176.0f * s;
    const ElStyle elSt{ctx.palette, ctx.fonts, s};
    const auto putLink = [&](const char* id, std::string_view key, const char* url) {
        IdGuard gid(id);
        ImGui::SetCursorScreenPos(ImVec2(linkX, curY + 18.0f * s));
        ElDrawLinkText(elSt,
                       {ctx.tr.Tr(key), [&ctx, url]() { ctx.host.OpenPathInExplorer(url); }, 12.0f});
        linkX += 60.0f * s;
    };
    putLink("##lnk_lic", "about.license", "https://motrix.app/license");
    putLink("##lnk_sup", "about.support", "https://motrix.app/support");
    putLink("##lnk_rel", "about.release", "https://motrix.app/release");

    ImGui::SetCursorScreenPos(ImVec2(lay.winPos.x, curY + footerH));
    ImGui::Dummy(ImVec2(lay.modalW, 0.0f));
}

void RenderAddTaskExtraAdvancedRows(UiFrameContext& ctx, AddTaskFormState& form, ModalFlowPos& flow) {
    const float s = ctx.dpiScale;
    const float rowH = 30.0f * s;
    const float gapY = 12.0f * s;
    const float lblW = 92.0f * s;
    const float fieldW = flow.bodyW - lblW;
    const ElStyle elSt{ctx.palette, ctx.fonts, s};

    DrawAlignedFieldCaption(ctx, {flow.x0, flow.curY, rowH}, ctx.tr.Tr("task.cookie_label"));
    ImGui::SetCursorScreenPos(ImVec2(flow.x0 + lblW, flow.curY));
    ElInputText(elSt,
                {"##add_cookie",
                 form.cookieBuffer.data(),
                 form.cookieBuffer.size(),
                 fieldW,
                 ctx.tr.Tr("task.cookie_placeholder")});
    flow.curY += rowH + gapY;

    DrawAlignedFieldCaption(ctx, {flow.x0, flow.curY, rowH}, ctx.tr.Tr("task.proxy_label"));
    ImGui::SetCursorScreenPos(ImVec2(flow.x0 + lblW, flow.curY));
    ElInputText(elSt,
                {"##add_proxy",
                 form.proxyBuffer.data(),
                 form.proxyBuffer.size(),
                 fieldW,
                 "[http://][USER:PASSWORD@]HOST[:PORT]"});
    flow.curY += rowH + 4.0f * s;

    ImGui::SetCursorScreenPos(ImVec2(flow.x0 + lblW, flow.curY));
    ElDrawLinkText(elSt, {ctx.tr.Tr("task.proxy_tips"), [&ctx]() {
                              ctx.host.OpenPathInExplorer("https://motrix.app/release");
                          }});
    flow.curY += 20.0f * s;

    ImGui::SetCursorScreenPos(ImVec2(flow.x0 + lblW, flow.curY));
    ElCheckbox(
        elSt, "##add_nav_dl", ctx.tr.Tr("task.navigate_to_downloading"), &form.navigateToDownloading);
    flow.curY += 22.0f * s;
}

}  // namespace

void RenderAddTaskAdvancedFields(UiFrameContext& ctx, AddTaskFormState& form, ModalFlowPos& flow) {
    const float s = ctx.dpiScale;
    const float rowH = 30.0f * s;
    const float gapY = 12.0f * s;
    const float lblW = 92.0f * s;
    const float fieldW = flow.bodyW - lblW;
    const ElStyle elSt{ctx.palette, ctx.fonts, s};

    DrawAlignedFieldCaption(ctx, {flow.x0, flow.curY, rowH}, ctx.tr.Tr("task.ua_label"));
    ImGui::SetCursorScreenPos(ImVec2(flow.x0 + lblW, flow.curY));
    ElInputText(elSt,
                {"##add_ua",
                 form.userAgentBuffer.data(),
                 form.userAgentBuffer.size(),
                 fieldW,
                 ctx.tr.Tr("task.ua_placeholder")});
    flow.curY += rowH + gapY;

    const float halfGap = 14.0f * s;
    const float halfColW = (flow.bodyW - halfGap) * 0.5f;
    DrawAlignedFieldCaption(ctx, {flow.x0, flow.curY, rowH}, ctx.tr.Tr("task.auth_label"));
    ImGui::SetCursorScreenPos(ImVec2(flow.x0 + lblW, flow.curY));
    ElInputText(elSt,
                {"##add_auth",
                 form.authBuffer.data(),
                 form.authBuffer.size(),
                 halfColW - lblW,
                 ctx.tr.Tr("task.auth_placeholder")});

    const float rightX = flow.x0 + halfColW + halfGap;
    const float refLblW = 66.0f * s;
    DrawAlignedFieldCaption(ctx, {rightX, flow.curY, rowH}, ctx.tr.Tr("task.referer_label"));
    ImGui::SetCursorScreenPos(ImVec2(rightX + refLblW, flow.curY));
    ElInputText(elSt,
                {"##add_referer",
                 form.refererBuffer.data(),
                 form.refererBuffer.size(),
                 halfColW - refLblW,
                 ctx.tr.Tr("task.referer_placeholder")});
    flow.curY += rowH + gapY;

    RenderAddTaskExtraAdvancedRows(ctx, form, flow);
}

void RenderAboutModalDialog(UiFrameContext& ctx) {
    const float s = ctx.dpiScale;
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    const float modalW = 480.0f * s;
    ImGui::SetNextWindowPos(ImVec2(vp->Pos.x + vp->Size.x * 0.5f, vp->Pos.y + vp->Size.y * 0.5f),
                            ImGuiCond_Always,
                            ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(modalW, 0.0f), ImGuiCond_Always);

    StyleVarGuard winPad(ImGuiStyleVar_WindowPadding, 0.0f, 0.0f);
    StyleVarGuard winRound(ImGuiStyleVar_WindowRounding, 6.0f * s);
    StyleVarGuard winBorder(ImGuiStyleVar_WindowBorderSize, 0.0f);
    StyleColorGuard popBg(ImGuiCol_PopupBg, ctx.palette.modalBg);
    StyleColorGuard dimBg(ImGuiCol_ModalWindowDimBg,
                          RgbaColor{0.0f, 0.0f, 0.0f, ctx.palette.isDark ? 0.56f : 0.45f});

    constexpr ImGuiWindowFlags kFlags = ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoTitleBar
                                        | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings;
    PopupModalGuard modal("##AboutModal", nullptr, kFlags);
    if (!modal.IsOpen()) {
        return;
    }

    const ImVec2 winPos = ImGui::GetWindowPos();
    const float padX = 28.0f * s;
    const AboutLayout lay{winPos, modalW, padX, winPos.x + padX, modalW - padX * 2.0f};
    float curY = winPos.y + 18.0f * s;

    RenderAboutHeader(ctx, lay, curY);
    RenderAboutEngineCard(ctx, lay, curY);
    RenderAboutFooter(ctx, lay, curY);

    if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
        ctx.sink.Dispatch(motrix::app::CloseModalCmd{});
        ImGui::CloseCurrentPopup();
    }
}

}  // namespace motrix::ui

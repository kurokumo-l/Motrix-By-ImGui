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

void DrawAlignedFieldCaption(
    const UiFrameContext& ctx, float x, float y, float h, std::string_view text) {
    FontGuard fnt(ctx.fonts, FontRole::Regular, 13.5f);
    const ImVec2 sz = ImGui::CalcTextSize(text.data(), text.data() + text.size());
    ImGui::GetWindowDrawList()->AddText(ImVec2(x, y + (h - sz.y) * 0.5f),
                                        ToAbgrU32(ctx.palette.subLabelText),
                                        text.data(),
                                        text.data() + text.size());
}

}  // namespace

void RenderAddTaskAdvancedFields(
    UiFrameContext& ctx, AddTaskFormState& form, float x0, float bodyW, float& curY) {
    const float s = ctx.dpiScale;
    const float rowH = 30.0f * s;
    const float gapY = 12.0f * s;
    const float lblW = 92.0f * s;
    const float fieldW = bodyW - lblW;

    DrawAlignedFieldCaption(ctx, x0, curY, rowH, ctx.tr.Tr("task.ua_label"));
    ImGui::SetCursorScreenPos(ImVec2(x0 + lblW, curY));
    ElInputText("##add_ua",
                form.userAgentBuffer.data(),
                form.userAgentBuffer.size(),
                fieldW,
                ctx.palette,
                ctx.fonts,
                s,
                ctx.tr.Tr("task.ua_placeholder"));
    curY += rowH + gapY;

    const float halfGap = 14.0f * s;
    const float halfColW = (bodyW - halfGap) * 0.5f;
    DrawAlignedFieldCaption(ctx, x0, curY, rowH, ctx.tr.Tr("task.auth_label"));
    ImGui::SetCursorScreenPos(ImVec2(x0 + lblW, curY));
    ElInputText("##add_auth",
                form.authBuffer.data(),
                form.authBuffer.size(),
                halfColW - lblW,
                ctx.palette,
                ctx.fonts,
                s,
                ctx.tr.Tr("task.auth_placeholder"));

    const float rightX = x0 + halfColW + halfGap;
    const float refLblW = 66.0f * s;
    DrawAlignedFieldCaption(ctx, rightX, curY, rowH, ctx.tr.Tr("task.referer_label"));
    ImGui::SetCursorScreenPos(ImVec2(rightX + refLblW, curY));
    ElInputText("##add_referer",
                form.refererBuffer.data(),
                form.refererBuffer.size(),
                halfColW - refLblW,
                ctx.palette,
                ctx.fonts,
                s,
                ctx.tr.Tr("task.referer_placeholder"));
    curY += rowH + gapY;

    DrawAlignedFieldCaption(ctx, x0, curY, rowH, ctx.tr.Tr("task.cookie_label"));
    ImGui::SetCursorScreenPos(ImVec2(x0 + lblW, curY));
    ElInputText("##add_cookie",
                form.cookieBuffer.data(),
                form.cookieBuffer.size(),
                fieldW,
                ctx.palette,
                ctx.fonts,
                s,
                ctx.tr.Tr("task.cookie_placeholder"));
    curY += rowH + gapY;

    DrawAlignedFieldCaption(ctx, x0, curY, rowH, ctx.tr.Tr("task.proxy_label"));
    ImGui::SetCursorScreenPos(ImVec2(x0 + lblW, curY));
    ElInputText("##add_proxy",
                form.proxyBuffer.data(),
                form.proxyBuffer.size(),
                fieldW,
                ctx.palette,
                ctx.fonts,
                s,
                "[http://][USER:PASSWORD@]HOST[:PORT]");
    curY += rowH + 4.0f * s;

    ImGui::SetCursorScreenPos(ImVec2(x0 + lblW, curY));
    ElDrawLinkText(ctx.palette, ctx.fonts, s, ctx.tr.Tr("task.proxy_tips"), [&ctx]() {
        ctx.host.OpenPathInExplorer("https://motrix.app/release");
    });
    curY += 20.0f * s;

    ImGui::SetCursorScreenPos(ImVec2(x0 + lblW, curY));
    ElCheckbox("##add_nav_dl",
               ctx.tr.Tr("task.navigate_to_downloading"),
               &form.navigateToDownloading,
               ctx.palette,
               ctx.fonts,
               s);
    curY += 22.0f * s;
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
    if (!ImGui::BeginPopupModal("##AboutModal", nullptr, kFlags)) {
        return;
    }

    const ImVec2 winPos = ImGui::GetWindowPos();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float padX = 28.0f * s;
    const float x0 = winPos.x + padX;
    const float bodyW = modalW - padX * 2.0f;
    float curY = winPos.y + 18.0f * s;

    const float closeBox = 24.0f * s;
    const float closeX = winPos.x + modalW - 14.0f * s - closeBox;
    const float closeY = winPos.y + 14.0f * s;
    ImGui::SetCursorScreenPos(ImVec2(closeX, closeY));
    if (ImGui::InvisibleButton("##about_close", ImVec2(closeBox, closeBox))) {
        ctx.sink.Dispatch(motrix::app::CloseModalCmd{});
        ImGui::CloseCurrentPopup();
    }
    const bool closeHov = ImGui::IsItemHovered();
    DrawMotrixIcon(MotrixIcon::WinClose,
                   closeX + 6.5f * s,
                   closeY + 6.5f * s,
                   11.0f * s,
                   11.0f * s,
                   closeHov ? ctx.palette.primary : ctx.palette.taskMetaText,
                   s);

    curY += 20.0f * s;
    {
        FontGuard titleF(ctx.fonts, FontRole::Medium, 24.0f);
        dl->AddText(ImVec2(x0, curY), ToAbgrU32(ctx.palette.panelTitle), "Motrix");
    }
    {
        FontGuard verF(ctx.fonts, FontRole::Regular, 13.0f);
        dl->AddText(ImVec2(x0, curY + 32.0f * s),
                    ToAbgrU32(ctx.palette.taskMetaText),
                    "Version 1.8.19  (Native C++23)");
    }
    DrawMotrixIcon(MotrixIcon::LogoFull,
                   x0 + bodyW - 56.0f * s,
                   curY + 4.0f * s,
                   48.0f * s,
                   48.0f * s,
                   ctx.palette.primary,
                   s);
    curY += 68.0f * s;

    const auto& ei = ctx.state.engineInfo;
    const std::string verStr =
        ei.version.empty() ? "--"
                           : std::format("{} {}", ei.product.empty() ? "aria2" : ei.product, ei.version);
    const std::vector<std::string_view> fallbackFeats{"BitTorrent",
                                                      "Firefox3 Cookie",
                                                      "GZip",
                                                      "HTTPS",
                                                      "Message Digest",
                                                      "Metalink",
                                                      "XML-RPC",
                                                      "SFTP"};
    const std::size_t featCount =
        ei.enabledFeatures.empty() ? fallbackFeats.size() : ei.enabledFeatures.size();
    const std::size_t rows = (featCount + 1) / 2;
    const float cardH = 44.0f * s + static_cast<float>(rows) * 20.0f * s + 12.0f * s;

    dl->AddRectFilled(
        ImVec2(x0, curY), ImVec2(x0 + bodyW, curY + cardH), ToAbgrU32(ctx.palette.subnavBg), 6.0f * s);
    {
        FontGuard ef(ctx.fonts, FontRole::Medium, 13.5f);
        const std::string_view evLbl = ctx.tr.Tr("about.engine_version");
        dl->AddText(ImVec2(x0 + 16.0f * s, curY + 14.0f * s),
                    ToAbgrU32(ctx.palette.panelTitle),
                    evLbl.data(),
                    evLbl.data() + evLbl.size());
        const ImVec2 vsz = ImGui::CalcTextSize(verStr.c_str());
        dl->AddText(ImVec2(x0 + bodyW - 16.0f * s - vsz.x, curY + 14.0f * s),
                    ToAbgrU32(ctx.palette.taskMetaText),
                    verStr.c_str());
    }
    dl->AddLine(ImVec2(x0 + 16.0f * s, curY + 40.0f * s),
                ImVec2(x0 + bodyW - 16.0f * s, curY + 40.0f * s),
                ToAbgrU32(ctx.palette.panelBorder),
                1.0f);

    {
        FontGuard ff(ctx.fonts, FontRole::Regular, 12.5f);
        const float col2X = x0 + bodyW * 0.5f + 8.0f * s;
        for (std::size_t i = 0; i < featCount; ++i) {
            const std::string_view fName =
                ei.enabledFeatures.empty() ? fallbackFeats[i] : std::string_view(ei.enabledFeatures[i]);
            const float fx = (i % 2 == 0) ? (x0 + 16.0f * s) : col2X;
            const float fy = curY + 48.0f * s + static_cast<float>(i / 2) * 20.0f * s;
            dl->AddText(ImVec2(fx, fy),
                        ToAbgrU32(ctx.palette.subLabelText),
                        fName.data(),
                        fName.data() + fName.size());
        }
    }
    curY += cardH + 22.0f * s;

    const float footerH = 52.0f * s;
    dl->AddRectFilled(ImVec2(winPos.x, curY),
                      ImVec2(winPos.x + modalW, curY + footerH),
                      ToAbgrU32(ctx.palette.modalFooterBg),
                      6.0f * s,
                      ImDrawFlags_RoundCornersBottom);
    {
        FontGuard cf(ctx.fonts, FontRole::Regular, 12.0f);
        dl->AddText(
            ImVec2(x0, curY + 18.0f * s), ToAbgrU32(ctx.palette.taskMetaText), "© 2018-2026 Motrix");
    }

    float linkX = x0 + bodyW - 176.0f * s;
    const auto putLink = [&](const char* id, std::string_view key, const char* url) {
        IdGuard gid(id);
        ImGui::SetCursorScreenPos(ImVec2(linkX, curY + 18.0f * s));
        ElDrawLinkText(
            ctx.palette,
            ctx.fonts,
            s,
            ctx.tr.Tr(key),
            [&ctx, url]() { ctx.host.OpenPathInExplorer(url); },
            12.0f);
        linkX += 60.0f * s;
    };
    putLink("##lnk_lic", "about.license", "https://motrix.app/license");
    putLink("##lnk_sup", "about.support", "https://motrix.app/support");
    putLink("##lnk_rel", "about.release", "https://motrix.app/release");

    ImGui::SetCursorScreenPos(ImVec2(winPos.x, curY + footerH));
    ImGui::Dummy(ImVec2(modalW, 0.0f));
    if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
        ctx.sink.Dispatch(motrix::app::CloseModalCmd{});
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

}  // namespace motrix::ui

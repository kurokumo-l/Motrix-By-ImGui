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

void DrawLabCaption(const UiFrameContext& ctx, float x, float y, std::string_view txt) {
    FontGuard fg(ctx.fonts, FontRole::Regular, 13.0f);
    ImGui::GetWindowDrawList()->AddText(
        ImVec2(x, y), ToAbgrU32(ctx.palette.subLabelText), txt.data(), txt.data() + txt.size());
}

void RenderUserAgentButtons(const UiFrameContext& ctx, PrefsFormState& ps, float x, float y) {
    struct Preset {
        std::string_view label;
        std::string_view ua;
    };
    static constexpr std::array<Preset, 4> kPresets{{
        {"Aria2", "aria2/1.36.0"},
        {"Transmission", "Transmission/3.00"},
        {"Chrome",
         "Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) AppleWebKit/537.36 "
         "(KHTML, like Gecko) Chrome/111.0.0.0 Safari/537.36"},
        {"du", "netdisk;6.0.0.12;PC;PC-Windows;10.0.16299;WindowsBaiduYunGuanJia"},
    }};

    const float s = ctx.dpiScale;
    const float h = 30.0f * s;
    FontGuard fg(ctx.fonts, FontRole::Medium, 12.5f);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    float curX = x;

    for (std::size_t i = 0; i < kPresets.size(); ++i) {
        const ImVec2 tSz = ImGui::CalcTextSize(kPresets[i].label.data());
        const float w = tSz.x + 26.0f * s;
        const ImVec2 p0(curX, y);
        const ImVec2 p1(curX + w, y + h);
        ImGui::SetCursorScreenPos(p0);
        if (ImGui::InvisibleButton(std::format("##ua_preset_{}", i).c_str(), ImVec2(w, h))) {
            CopyUtf8ToBuffer(kPresets[i].ua, ps.userAgentBuffer.data(), ps.userAgentBuffer.size());
        }
        const bool hov = ImGui::IsItemHovered();
        if (hov) {
            ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
        }
        ImDrawFlags roundFlags = ImDrawFlags_RoundCornersNone;
        if (i == 0) {
            roundFlags = ImDrawFlags_RoundCornersLeft;
        } else if (i + 1 == kPresets.size()) {
            roundFlags = ImDrawFlags_RoundCornersRight;
        }
        dl->AddRectFilled(
            p0,
            p1,
            ToAbgrU32(hov ? WithAlpha(ctx.palette.primary, ctx.palette.isDark ? 0.14f : 0.08f)
                          : ctx.palette.inputBg),
            4.0f * s,
            roundFlags);
        dl->AddRect(p0,
                    p1,
                    ToAbgrU32(hov ? ctx.palette.primary : ctx.palette.inputBorder),
                    4.0f * s,
                    roundFlags,
                    1.0f);
        dl->AddText(ImVec2(p0.x + (w - tSz.x) * 0.5f, p0.y + (h - tSz.y) * 0.5f),
                    ToAbgrU32(hov ? ctx.palette.primary : ctx.palette.panelTitle),
                    kPresets[i].label.data());
        curX += w - 1.0f;
    }
}

void DrawReadonlyPathRow(const UiFrameContext& ctx,
                         const char* id,
                         std::string_view caption,
                         std::string_view path,
                         float x,
                         float& y,
                         float width) {
    const float s = ctx.dpiScale;
    DrawLabCaption(ctx, x, y, caption);
    y += 22.0f * s;
    std::array<char, 512> buf{};
    CopyUtf8ToBuffer(path, buf.data(), buf.size());
    ImGui::SetCursorScreenPos(ImVec2(x, y));
    const ElCompositeResult res = ElCompositeInput(id,
                                                   buf.data(),
                                                   buf.size(),
                                                   width,
                                                   ctx.palette,
                                                   ctx.fonts,
                                                   s,
                                                   std::nullopt,
                                                   MotrixIcon::Folder,
                                                   {},
                                                   true);
    if (res.rightClicked && !path.empty()) {
        ctx.host.OpenPathInExplorer(path);
    }
    y += 40.0f * s;
}

}  // namespace

void RenderPrefsAdvancedTail(UiFrameContext& ctx, PrefsFormState& ps) {
    const float s = ctx.dpiScale;

    DrawPrefsFormRow(ctx, ctx.tr.Tr("prefs.download_protocol"), 20.0f * s, [&](float bx, float) {
        float cy = ImGui::GetCursorScreenPos().y;
        DrawLabCaption(ctx, bx, cy, ctx.tr.Tr("prefs.protocols_default_client"));
        cy += 26.0f * s;
        ImGui::SetCursorScreenPos(ImVec2(bx, cy));
        ElSwitch("##proto_mag",
                 ctx.tr.Tr("prefs.proto_magnet"),
                 &ps.protocolMagnet,
                 ctx.palette,
                 ctx.fonts,
                 s);
        cy += 32.0f * s;
        ImGui::SetCursorScreenPos(ImVec2(bx, cy));
        ElSwitch("##proto_th",
                 ctx.tr.Tr("prefs.proto_thunder"),
                 &ps.protocolThunder,
                 ctx.palette,
                 ctx.fonts,
                 s);
        cy += 32.0f * s;
        ImGui::SetCursorScreenPos(ImVec2(bx, cy));
        ElSwitch("##proto_mx",
                 ctx.tr.Tr("prefs.proto_motrix"),
                 &ps.protocolMotrix,
                 ctx.palette,
                 ctx.fonts,
                 s);
        ImGui::SetCursorScreenPos(ImVec2(bx, cy + 26.0f * s));
        ImGui::Dummy(ImVec2(0.0f, 0.0f));
    });

    DrawPrefsFormRow(ctx, ctx.tr.Tr("prefs.user_agent"), 20.0f * s, [&](float bx, float bw) {
        float cy = ImGui::GetCursorScreenPos().y;
        const float fieldW = std::min(bw, 480.0f * s);
        DrawLabCaption(ctx, bx, cy, ctx.tr.Tr("prefs.mock_user_agent"));
        cy += 24.0f * s;
        ImGui::SetCursorScreenPos(ImVec2(bx, cy));
        ElInputTextMultiline("##adv_ua",
                             ps.userAgentBuffer.data(),
                             ps.userAgentBuffer.size(),
                             fieldW,
                             56.0f * s,
                             ctx.palette,
                             ctx.fonts,
                             s);
        cy += 64.0f * s;
        RenderUserAgentButtons(ctx, ps, bx, cy);
        ImGui::SetCursorScreenPos(ImVec2(bx, cy + 32.0f * s));
        ImGui::Dummy(ImVec2(0.0f, 0.0f));
    });

    DrawPrefsFormRow(ctx, ctx.tr.Tr("prefs.developer"), 20.0f * s, [&](float bx, float bw) {
        float cy = ImGui::GetCursorScreenPos().y;
        const float fieldW = std::min(bw, 450.0f * s);
        DrawReadonlyPathRow(ctx,
                            "##dev_conf",
                            ctx.tr.Tr("prefs.aria2_conf_path"),
                            ctx.state.engineConfPath,
                            bx,
                            cy,
                            fieldW);
        DrawReadonlyPathRow(ctx,
                            "##dev_sess",
                            ctx.tr.Tr("prefs.session_path"),
                            ctx.state.sessionFilePath,
                            bx,
                            cy,
                            fieldW);
        DrawReadonlyPathRow(
            ctx, "##dev_log", ctx.tr.Tr("prefs.log_path"), ctx.state.appLogPath, bx, cy, fieldW);

        cy += 4.0f * s;
        float btn1W = 138.0f * s;
        float btn2W = 118.0f * s;
        {
            FontGuard bf(ctx.fonts, FontRole::Regular, 13.5f);
            const std::string_view l1 = ctx.tr.Tr("prefs.session_reset");
            const std::string_view l2 = ctx.tr.Tr("prefs.factory_reset");
            btn1W = ImGui::CalcTextSize(l1.data(), l1.data() + l1.size()).x + 28.0f * s;
            btn2W = ImGui::CalcTextSize(l2.data(), l2.data() + l2.size()).x + 28.0f * s;
        }
        ImGui::SetCursorScreenPos(ImVec2(bx, cy));
        if (ElSecondaryButton("##dev_rst_sess",
                              ctx.tr.Tr("prefs.session_reset"),
                              btn1W,
                              30.0f * s,
                              ctx.palette,
                              ctx.fonts,
                              s)) {
            ctx.sink.Dispatch(motrix::app::ResetSessionCmd{});
        }
        ImGui::SetCursorScreenPos(ImVec2(bx + btn1W + 12.0f * s, cy));
        if (ElSecondaryButton("##dev_fact_rst",
                              ctx.tr.Tr("prefs.factory_reset"),
                              btn2W,
                              30.0f * s,
                              ctx.palette,
                              ctx.fonts,
                              s)) {
            ctx.sink.Dispatch(motrix::app::FactoryResetCmd{});
            ps.initialized = false;
        }
        ImGui::SetCursorScreenPos(ImVec2(bx, cy + 32.0f * s));
        ImGui::Dummy(ImVec2(0.0f, 0.0f));
    });
}

void RenderPrefsLabTab(UiFrameContext& ctx, PrefsFormState& ps) {
    const float s = ctx.dpiScale;
    const float availW = std::min(ImGui::GetContentRegionAvail().x, 680.0f * s);
    const ImVec2 wPos = ImGui::GetCursorScreenPos();
    const float warnH = 54.0f * s;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(wPos,
                      ImVec2(wPos.x + availW, wPos.y + warnH),
                      ToAbgrU32(WithAlpha(HexColor(0xF59E0B), 0.12f)),
                      4.0f * s);
    dl->AddRect(wPos,
                ImVec2(wPos.x + availW, wPos.y + warnH),
                ToAbgrU32(WithAlpha(HexColor(0xF59E0B), 0.45f)),
                4.0f * s);
    {
        const float triCx = wPos.x + 22.0f * s;
        const float triTy = wPos.y + 19.0f * s;
        const ImU32 warnCol = ToAbgrU32(HexColor(0xE6A23C));
        dl->AddTriangleFilled(ImVec2(triCx, triTy),
                              ImVec2(triCx - 7.5f * s, triTy + 13.5f * s),
                              ImVec2(triCx + 7.5f * s, triTy + 13.5f * s),
                              warnCol);
        dl->AddRectFilled(ImVec2(triCx - 1.0f * s, triTy + 4.5f * s),
                          ImVec2(triCx + 1.0f * s, triTy + 9.0f * s),
                          ToAbgrU32(HexColor(0xFFFFFF)));
        dl->AddRectFilled(ImVec2(triCx - 1.0f * s, triTy + 10.4f * s),
                          ImVec2(triCx + 1.0f * s, triTy + 12.2f * s),
                          ToAbgrU32(HexColor(0xFFFFFF)));
    }
    {
        FontGuard fg(ctx.fonts, FontRole::Regular, 13.0f);
        const std::string_view msg = ctx.tr.Tr("prefs.lab_warning");
        dl->AddText(ImGui::GetFont(),
                    ImGui::GetFontSize(),
                    ImVec2(wPos.x + 38.0f * s, wPos.y + 17.0f * s),
                    ToAbgrU32(ctx.palette.panelTitle),
                    msg.data(),
                    msg.data() + msg.size(),
                    availW - 52.0f * s);
    }
    ImGui::SetCursorScreenPos(ImVec2(wPos.x, wPos.y + warnH + 22.0f * s));
    DrawPrefsFormRow(ctx, ctx.tr.Tr("prefs.engine_path"), 30.0f * s, [&](float bx, float bw) {
        const float fieldW = std::min(bw, 450.0f * s);
        ImGui::SetCursorScreenPos(ImVec2(bx, ImGui::GetCursorScreenPos().y));
        const ElCompositeResult r = ElCompositeInput("##lab_engine_bin",
                                                     ps.enginePathBuffer.data(),
                                                     ps.enginePathBuffer.size(),
                                                     fieldW,
                                                     ctx.palette,
                                                     ctx.fonts,
                                                     s,
                                                     std::nullopt,
                                                     MotrixIcon::Folder,
                                                     ctx.state.resolvedEnginePath);
        if (r.rightClicked && !ctx.state.resolvedEnginePath.empty()) {
            ctx.host.OpenPathInExplorer(ctx.state.resolvedEnginePath);
        }
    });
}

}  // namespace motrix::ui

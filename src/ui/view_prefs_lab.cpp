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
    FontGuard fg(ctx.fonts, FontRole::Regular, 14.5f);
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
    const float h = 32.0f * s;
    FontGuard fg(ctx.fonts, FontRole::Medium, 13.5f);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    float curX = x;

    for (std::size_t i = 0; i < kPresets.size(); ++i) {
        const ImVec2 tSz = ImGui::CalcTextSize(kPresets[i].label.data());
        const float w = tSz.x + 28.0f * s;
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

struct ReadonlyPathSpec {
    const char* id = nullptr;
    std::string_view caption{};
    std::string_view path{};
    float x = 0.0f;
    float y = 0.0f;
    float width = 0.0f;
};

float DrawReadonlyPathRow(UiFrameContext& ctx, const ReadonlyPathSpec& spec) {
    const float s = ctx.dpiScale;
    DrawLabCaption(ctx, spec.x, spec.y, spec.caption);
    const float fieldY = spec.y + 24.0f * s;
    std::array<char, 512> buf{};
    CopyUtf8ToBuffer(spec.path, buf.data(), buf.size());
    ImGui::SetCursorScreenPos(ImVec2(spec.x, fieldY));
    const ElStyle elSt{ctx.palette, ctx.fonts, s, &ctx.anim};
    const ElCompositeResult res = ElCompositeInput(
        elSt,
        ElCompositeSpec{
            spec.id, buf.data(), buf.size(), spec.width, std::nullopt, MotrixIcon::Folder, {}, true});
    if (res.rightClicked && !spec.path.empty()) {
        ctx.host.OpenPathInExplorer(spec.path);
    }
    return fieldY + 42.0f * s;
}

void RenderAdvProtocolRow(UiFrameContext& ctx, PrefsFormState& ps) {
    const float s = ctx.dpiScale;
    const ElStyle elSt{ctx.palette, ctx.fonts, s, &ctx.anim};

    DrawPrefsFormRow(ctx, ctx.tr.Tr("prefs.download_protocol"), 24.0f * s, [&](float bx, float) {
        float cy = ImGui::GetCursorScreenPos().y;
        DrawLabCaption(ctx, bx, cy, ctx.tr.Tr("prefs.protocols_default_client"));
        cy += 28.0f * s;
        ImGui::SetCursorScreenPos(ImVec2(bx, cy));
        ElSwitch(elSt, "##proto_mag", ctx.tr.Tr("prefs.proto_magnet"), &ps.protocolMagnet);
        cy += 34.0f * s;
        ImGui::SetCursorScreenPos(ImVec2(bx, cy));
        ElSwitch(elSt, "##proto_th", ctx.tr.Tr("prefs.proto_thunder"), &ps.protocolThunder);
        cy += 34.0f * s;
        ImGui::SetCursorScreenPos(ImVec2(bx, cy));
        ElSwitch(elSt, "##proto_mx", ctx.tr.Tr("prefs.proto_motrix"), &ps.protocolMotrix);
        ImGui::SetCursorScreenPos(ImVec2(bx, cy + 28.0f * s));
        ImGui::Dummy(ImVec2(0.0f, 0.0f));
    });
}

void RenderAdvUserAgentRow(UiFrameContext& ctx, PrefsFormState& ps) {
    const float s = ctx.dpiScale;
    const ElStyle elSt{ctx.palette, ctx.fonts, s, &ctx.anim};

    DrawPrefsFormRow(ctx, ctx.tr.Tr("prefs.user_agent"), 24.0f * s, [&](float bx, float bw) {
        float cy = ImGui::GetCursorScreenPos().y;
        const float fieldW = std::min(bw, 490.0f * s);
        DrawLabCaption(ctx, bx, cy, ctx.tr.Tr("prefs.mock_user_agent"));
        cy += 26.0f * s;
        ImGui::SetCursorScreenPos(ImVec2(bx, cy));
        ElInputTextMultiline(elSt,
                             ElTextInputSpec{"##adv_ua",
                                             ps.userAgentBuffer.data(),
                                             ps.userAgentBuffer.size(),
                                             fieldW,
                                             {},
                                             0,
                                             60.0f * s});
        cy += 68.0f * s;
        RenderUserAgentButtons(ctx, ps, bx, cy);
        ImGui::SetCursorScreenPos(ImVec2(bx, cy + 34.0f * s));
        ImGui::Dummy(ImVec2(0.0f, 0.0f));
    });
}

void RenderAdvDeveloperRow(UiFrameContext& ctx, PrefsFormState& ps) {
    const float s = ctx.dpiScale;
    const ElStyle elSt{ctx.palette, ctx.fonts, s, &ctx.anim};

    DrawPrefsFormRow(ctx, ctx.tr.Tr("prefs.developer"), 24.0f * s, [&](float bx, float bw) {
        float cy = ImGui::GetCursorScreenPos().y;
        const float fieldW = std::min(bw, 460.0f * s);

        DrawLabCaption(ctx, bx, cy, ctx.tr.Tr("prefs.log_level"));
        cy += 24.0f * s;
        const std::array<const char*, 4> logItems{
            ctx.tr.Tr("prefs.log_debug").data(),
            ctx.tr.Tr("prefs.log_info").data(),
            ctx.tr.Tr("prefs.log_warn").data(),
            ctx.tr.Tr("prefs.log_error").data(),
        };
        ImGui::SetCursorScreenPos(ImVec2(bx, cy));
        ElSelectCombo(
            elSt,
            ElSelectSpec{"##dev_log_level", &ps.logLevelIndex, logItems, std::min(bw, 220.0f * s)});
        cy += 44.0f * s;

        cy = DrawReadonlyPathRow(ctx,
                                 ReadonlyPathSpec{"##dev_conf",
                                                  ctx.tr.Tr("prefs.aria2_conf_path"),
                                                  ctx.state.engineConfPath,
                                                  bx,
                                                  cy,
                                                  fieldW});
        cy = DrawReadonlyPathRow(ctx,
                                 ReadonlyPathSpec{"##dev_sess",
                                                  ctx.tr.Tr("prefs.session_path"),
                                                  ctx.state.sessionFilePath,
                                                  bx,
                                                  cy,
                                                  fieldW});
        cy = DrawReadonlyPathRow(
            ctx,
            ReadonlyPathSpec{
                "##dev_log", ctx.tr.Tr("prefs.log_path"), ctx.state.appLogPath, bx, cy, fieldW});

        cy += 6.0f * s;
        float btn1W = 148.0f * s;
        float btn2W = 128.0f * s;
        {
            FontGuard bf(ctx.fonts, FontRole::Regular, 14.5f);
            const std::string_view l1 = ctx.tr.Tr("prefs.session_reset");
            const std::string_view l2 = ctx.tr.Tr("prefs.factory_reset");
            btn1W = ImGui::CalcTextSize(l1.data(), l1.data() + l1.size()).x + 30.0f * s;
            btn2W = ImGui::CalcTextSize(l2.data(), l2.data() + l2.size()).x + 30.0f * s;
        }
        ImGui::SetCursorScreenPos(ImVec2(bx, cy));
        if (ElSecondaryButton(
                elSt,
                ElButtonSpec{"##dev_rst_sess", ctx.tr.Tr("prefs.session_reset"), btn1W, 34.0f * s})) {
            ctx.sink.Dispatch(motrix::app::ResetSessionCmd{});
            SetPrefsFeedback(ps, 3, true);
            ctx.host.NotifyAnimationActive();
        }
        ImGui::SetCursorScreenPos(ImVec2(bx + btn1W + 12.0f * s, cy));
        if (ElSecondaryButton(
                elSt,
                ElButtonSpec{"##dev_fact_rst", ctx.tr.Tr("prefs.factory_reset"), btn2W, 34.0f * s})) {
            ctx.sink.Dispatch(motrix::app::FactoryResetCmd{});
            ps.initialized = false;
            SetPrefsFeedback(ps, 4, true);
            ctx.host.NotifyAnimationActive();
        }
        ImGui::SetCursorScreenPos(ImVec2(bx, cy + 36.0f * s));
        ImGui::Dummy(ImVec2(0.0f, 0.0f));
    });
}

void RenderLabWarningBanner(UiFrameContext& ctx) {
    const float s = ctx.dpiScale;
    const float availW = std::min(ImGui::GetContentRegionAvail().x, 680.0f * s);
    const ImVec2 wPos = ImGui::GetCursorScreenPos();
    const float warnH = 62.0f * s;
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
        const float triTy = wPos.y + 23.0f * s;
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
        FontGuard fg(ctx.fonts, FontRole::Regular, 14.0f);
        const std::string_view msg = ctx.tr.Tr("prefs.lab_warning");
        dl->AddText(ImGui::GetFont(),
                    ImGui::GetFontSize(),
                    ImVec2(wPos.x + 40.0f * s, wPos.y + 18.0f * s),
                    ToAbgrU32(ctx.palette.panelTitle),
                    msg.data(),
                    msg.data() + msg.size(),
                    availW - 56.0f * s);
    }
    ImGui::SetCursorScreenPos(ImVec2(wPos.x, wPos.y + warnH + 24.0f * s));
    ImGui::Dummy(ImVec2(0.0f, 0.0f));
}

void RenderLabMediaRow(UiFrameContext& ctx, PrefsFormState& ps) {
    const float s = ctx.dpiScale;
    const ElStyle elSt{ctx.palette, ctx.fonts, s, &ctx.anim};

    DrawPrefsFormRow(ctx, ctx.tr.Tr("prefs.lab_media_group"), 32.0f * s, [&](float bx, float bw) {
        float cy = ImGui::GetCursorScreenPos().y;
        const float comboW = std::min(bw, 260.0f * s);

        DrawLabCaption(ctx, bx, cy, ctx.tr.Tr("prefs.lab_media_mode"));
        cy += 24.0f * s;
        const std::array<const char*, 2> modeOpts{
            ctx.tr.Tr("prefs.lab_media_auto").data(),
            ctx.tr.Tr("prefs.lab_media_file").data(),
        };
        ImGui::SetCursorScreenPos(ImVec2(bx, cy));
        ElSelectCombo(elSt, ElSelectSpec{"##lab_media_mode", &ps.mediaModeIdx, modeOpts, comboW});
        cy += 42.0f * s;

        DrawLabCaption(ctx, bx, cy, ctx.tr.Tr("prefs.lab_media_format"));
        cy += 24.0f * s;
        static constexpr std::array<const char*, 2> kFormats{"MP4 (.mp4)", "Matroska (.mkv)"};
        ImGui::SetCursorScreenPos(ImVec2(bx, cy));
        ElSelectCombo(elSt, ElSelectSpec{"##lab_media_fmt", &ps.mediaFormatIdx, kFormats, 180.0f * s});
        ImGui::SetCursorScreenPos(ImVec2(bx, cy + 32.0f * s));
        ImGui::Dummy(ImVec2(0.0f, 0.0f));
    });
}

void RenderLabBtTweaksRow(UiFrameContext& ctx, PrefsFormState& ps) {
    const float s = ctx.dpiScale;
    const ElStyle elSt{ctx.palette, ctx.fonts, s, &ctx.anim};

    DrawPrefsFormRow(ctx, ctx.tr.Tr("prefs.lab_bt_stream_group"), 24.0f * s, [&](float bx, float) {
        float cy = ImGui::GetCursorScreenPos().y;
        ImGui::SetCursorScreenPos(ImVec2(bx, cy));
        ElCheckbox(
            elSt, "##lab_first_last", ctx.tr.Tr("prefs.lab_first_last"), &ps.btFirstLastPieceFirst);
        cy += 30.0f * s;
        ImGui::SetCursorScreenPos(ImVec2(bx, cy));
        ElCheckbox(elSt, "##lab_detach_share", ctx.tr.Tr("prefs.lab_detach_share"), &ps.detachShareOnly);
        cy += 34.0f * s;

        DrawLabCaption(ctx, bx, cy, ctx.tr.Tr("prefs.lab_max_peers"));
        cy += 24.0f * s;
        ImGui::SetCursorScreenPos(ImVec2(bx, cy));
        ElInputNumberInt(elSt, "##lab_max_peers", ElIntSpec{&ps.btMaxPeers, 0, 1000, 5, 140.0f * s});
        ImGui::SetCursorScreenPos(ImVec2(bx, cy + 32.0f * s));
        ImGui::Dummy(ImVec2(0.0f, 0.0f));
    });
}

void RenderLabEngineRow(UiFrameContext& ctx, PrefsFormState& ps) {
    const float s = ctx.dpiScale;
    const ElStyle elSt{ctx.palette, ctx.fonts, s, &ctx.anim};

    DrawPrefsFormRow(ctx, ctx.tr.Tr("prefs.engine_path"), 32.0f * s, [&](float bx, float bw) {
        float cy = ImGui::GetCursorScreenPos().y;
        const float fieldW = std::min(bw, 460.0f * s);
        ImGui::SetCursorScreenPos(ImVec2(bx, cy));
        const ElCompositeResult r = ElCompositeInput(elSt,
                                                     ElCompositeSpec{"##lab_engine_bin",
                                                                     ps.enginePathBuffer.data(),
                                                                     ps.enginePathBuffer.size(),
                                                                     fieldW,
                                                                     std::nullopt,
                                                                     MotrixIcon::Folder,
                                                                     ctx.state.resolvedEnginePath});
        if (r.rightClicked && !ctx.state.resolvedEnginePath.empty()) {
            ctx.host.OpenPathInExplorer(ctx.state.resolvedEnginePath);
        }
        cy += 38.0f * s;
        {
            FontGuard nf(ctx.fonts, FontRole::Regular, 13.5f);
            const std::string_view note = ctx.tr.Tr("prefs.engine_path_note");
            ImGui::GetWindowDrawList()->AddText(ImVec2(bx, cy),
                                                ToAbgrU32(ctx.palette.taskMetaText),
                                                note.data(),
                                                note.data() + note.size());
        }
        ImGui::SetCursorScreenPos(ImVec2(bx, cy + 20.0f * s));
        ImGui::Dummy(ImVec2(0.0f, 0.0f));
    });
}

}  // namespace

void RenderPrefsAdvancedTail(UiFrameContext& ctx, PrefsFormState& ps) {
    RenderAdvProtocolRow(ctx, ps);
    RenderAdvUserAgentRow(ctx, ps);
    RenderAdvDeveloperRow(ctx, ps);
}

void RenderPrefsLabTab(UiFrameContext& ctx, PrefsFormState& ps) {
    RenderLabWarningBanner(ctx);
    RenderLabMediaRow(ctx, ps);
    RenderLabBtTweaksRow(ctx, ps);
    RenderLabEngineRow(ctx, ps);
    RenderLabAria2NextUpdateRow(ctx, ps);
}

}  // namespace motrix::ui

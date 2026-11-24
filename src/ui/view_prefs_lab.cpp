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

[[nodiscard]] bool IsAria2UpdateBusy(motrix::app::EngineUpdateStatus st) noexcept {
    using S = motrix::app::EngineUpdateStatus;
    return st == S::Checking || st == S::Downloading || st == S::Verifying || st == S::Restarting;
}

float DrawAria2UpdateStatus(const UiFrameContext& ctx, float x, float y, float wrapW) {
    using S = motrix::app::EngineUpdateStatus;
    const S st = ctx.state.engineUpdateStatus;
    if (st == S::Idle) {
        return y;
    }
    std::string msg;
    RgbaColor col = ctx.palette.primary;
    if (st == S::Checking) {
        msg = std::string(ctx.tr.Tr("prefs.aria2_next_status_checking"));
    } else if (st == S::Downloading) {
        msg = std::string(ctx.tr.Tr("prefs.aria2_next_status_downloading"));
    } else if (st == S::Verifying || st == S::Restarting) {
        msg = std::string(ctx.tr.Tr("prefs.aria2_next_status_verifying"));
    } else if (st == S::Succeeded) {
        col = ctx.palette.progressComplete;
        msg = ctx.state.engineUpdateMessage.empty()
                  ? std::string(ctx.tr.Tr("prefs.aria2_next_status_uptodate"))
                  : std::format("{} ({})",
                                ctx.tr.Tr("prefs.aria2_next_status_updated"),
                                ctx.state.engineUpdateMessage);
    } else if (st == S::Failed) {
        col = ctx.palette.progressError;
        msg = ctx.state.engineUpdateMessage;
    }
    FontGuard sf(ctx.fonts, FontRole::Regular, 13.5f);
    ImGui::GetWindowDrawList()->AddText(ImGui::GetFont(),
                                        ImGui::GetFontSize(),
                                        ImVec2(x, y),
                                        ToAbgrU32(col),
                                        msg.c_str(),
                                        nullptr,
                                        wrapW);
    const ImVec2 sz = ImGui::CalcTextSize(msg.c_str(), nullptr, false, wrapW);
    return y + std::max(22.0f * ctx.dpiScale, sz.y + 8.0f * ctx.dpiScale);
}

void TriggerAria2NextUpdate(UiFrameContext& ctx, const PrefsFormState& ps) {
    const std::string_view url = ps.aria2NextUrlBuffer.data();
    const std::string_view mir = ps.aria2NextMirrorBuffer.data();
    if (url != ctx.state.prefs.aria2NextUpdateUrl || mir != ctx.state.prefs.aria2NextMirrorPrefix) {
        auto pr = ctx.state.prefs;
        pr.aria2NextUpdateUrl = std::string(url);
        pr.aria2NextMirrorPrefix = std::string(mir);
        ctx.sink.Dispatch(motrix::app::SavePreferencesCmd{std::move(pr), ctx.state.sysConfig, false});
    }
    ctx.sink.Dispatch(motrix::app::StartAria2NextUpdateCmd{false});
}

void RenderUpdaterSourceFields(UiFrameContext& ctx, PrefsFormState& ps, ImVec2& pos, float fieldW) {
    const float s = ctx.dpiScale;
    const ElStyle elSt{ctx.palette, ctx.fonts, s, &ctx.anim};
    DrawSubCaption(ctx, pos.x, pos.y, ctx.tr.Tr("prefs.aria2_next_source"));
    pos.y += 24.0f * s;
    const std::string_view rstLbl = ctx.tr.Tr("prefs.aria2_next_reset_source");
    float rstW = 96.0f * s;
    {
        FontGuard bf(ctx.fonts, FontRole::Regular, 14.5f);
        rstW = ImGui::CalcTextSize(rstLbl.data(), rstLbl.data() + rstLbl.size()).x + 24.0f * s;
    }
    const float urlW = std::max(140.0f * s, fieldW - rstW - 8.0f * s);
    ImGui::SetCursorScreenPos(pos);
    ElInputText(elSt,
                ElTextInputSpec{"##lab_a2_url",
                                ps.aria2NextUrlBuffer.data(),
                                ps.aria2NextUrlBuffer.size(),
                                urlW,
                                motrix::domain::kDefaultAria2NextUpdateUrl});
    ImGui::SetCursorScreenPos(ImVec2(pos.x + urlW + 8.0f * s, pos.y - 1.0f * s));
    if (ElSecondaryButton(elSt, ElButtonSpec{"##lab_a2_rst", rstLbl, rstW, 34.0f * s})) {
        CopyUtf8ToBuffer(motrix::domain::kDefaultAria2NextUpdateUrl,
                         ps.aria2NextUrlBuffer.data(),
                         ps.aria2NextUrlBuffer.size());
    }
    pos.y += 42.0f * s;
    DrawSubCaption(ctx, pos.x, pos.y, ctx.tr.Tr("prefs.aria2_next_mirror"));
    pos.y += 24.0f * s;
    ImGui::SetCursorScreenPos(pos);
    ElInputText(elSt,
                ElTextInputSpec{"##lab_a2_mir",
                                ps.aria2NextMirrorBuffer.data(),
                                ps.aria2NextMirrorBuffer.size(),
                                fieldW,
                                "https://ghproxy.net/"});
    pos.y += 42.0f * s;
}

void RenderLabAria2NextUpdateRow(UiFrameContext& ctx, PrefsFormState& ps) {
    DrawPrefsFormRow(
        ctx, ctx.tr.Tr("prefs.aria2_next_group"), 24.0f * ctx.dpiScale, [&](float bx, float bw) {
            const float s = ctx.dpiScale;
            const ElStyle elSt{ctx.palette, ctx.fonts, s, &ctx.anim};
            const float fieldW = std::min(bw, 460.0f * s);
            ImVec2 pos(bx, ImGui::GetCursorScreenPos().y);

            const std::string curVer =
                ctx.state.engineInfo.version.empty()
                    ? std::string(ctx.tr.Tr("prefs.aria2_next_not_connected"))
                    : std::format("{} v{}", ctx.state.engineInfo.product, ctx.state.engineInfo.version);
            DrawSubCaption(
                ctx, pos.x, pos.y, std::format("{}: {}", ctx.tr.Tr("prefs.aria2_next_current"), curVer));
            pos.y += 26.0f * s;

            RenderUpdaterSourceFields(ctx, ps, pos, fieldW);

            const bool busy = IsAria2UpdateBusy(ctx.state.engineUpdateStatus);
            const std::string_view updLbl =
                ctx.tr.Tr(busy ? "prefs.aria2_next_updating" : "prefs.aria2_next_check_update");
            float updW = 160.0f * s;
            {
                FontGuard bf(ctx.fonts, FontRole::Medium, 14.5f);
                updW = ImGui::CalcTextSize(updLbl.data(), updLbl.data() + updLbl.size()).x + 28.0f * s;
            }
            ImGui::SetCursorScreenPos(pos);
            if (ElPrimaryButton(elSt, ElButtonSpec{"##lab_a2_upd", updLbl, updW, 34.0f * s, busy})
                && !busy) {
                TriggerAria2NextUpdate(ctx, ps);
            }
            pos.y += 42.0f * s;
            pos.y = DrawAria2UpdateStatus(ctx, pos.x, pos.y, fieldW);
            float noteH = 26.0f * s;
            {
                FontGuard nf(ctx.fonts, FontRole::Regular, 13.0f);
                const std::string_view note = ctx.tr.Tr("prefs.aria2_next_managed_note");
                ImGui::GetWindowDrawList()->AddText(ImGui::GetFont(),
                                                    ImGui::GetFontSize(),
                                                    pos,
                                                    ToAbgrU32(ctx.palette.taskMetaText),
                                                    note.data(),
                                                    note.data() + note.size(),
                                                    fieldW);
                const ImVec2 noteSz =
                    ImGui::CalcTextSize(note.data(), note.data() + note.size(), false, fieldW);
                noteH = std::max(22.0f * s, noteSz.y + 8.0f * s);
            }
            ImGui::SetCursorScreenPos(ImVec2(pos.x, pos.y + noteH));
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

        DrawSubCaption(ctx, bx, cy, ctx.tr.Tr("prefs.lab_media_mode"));
        cy += 24.0f * s;
        const std::array<const char*, 2> modeOpts{
            ctx.tr.Tr("prefs.lab_media_auto").data(),
            ctx.tr.Tr("prefs.lab_media_file").data(),
        };
        ImGui::SetCursorScreenPos(ImVec2(bx, cy));
        ElSelectCombo(elSt, ElSelectSpec{"##lab_media_mode", &ps.mediaModeIdx, modeOpts, comboW});
        cy += 42.0f * s;

        DrawSubCaption(ctx, bx, cy, ctx.tr.Tr("prefs.lab_media_format"));
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

        DrawSubCaption(ctx, bx, cy, ctx.tr.Tr("prefs.lab_max_peers"));
        cy += 24.0f * s;
        ImGui::SetCursorScreenPos(ImVec2(bx, cy));
        ElInputNumberInt(elSt, "##lab_max_peers", ElIntSpec{&ps.btMaxPeers, 0, 2000, 5, 140.0f * s});
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

void RenderPrefsLabTab(UiFrameContext& ctx, PrefsFormState& ps) {
    RenderLabWarningBanner(ctx);
    RenderLabMediaRow(ctx, ps);
    RenderLabBtTweaksRow(ctx, ps);
    RenderLabEngineRow(ctx, ps);
    RenderLabAria2NextUpdateRow(ctx, ps);
}

}  // namespace motrix::ui

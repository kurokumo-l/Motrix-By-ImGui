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
import :scope_guard;

namespace motrix::ui {

namespace {

struct ThemeSketchTarget {
    ImDrawList* dl = nullptr;
    ImVec2 rMin{};
    ImVec2 rMax{};
};

void DrawThemeWindowSketch(const ThemeSketchTarget& t, bool dark, float s) {
    ImDrawList* dl = t.dl;
    const ImVec2 rMin = t.rMin;
    const ImVec2 rMax = t.rMax;
    const ImU32 asideCol = ToAbgrU32(HexColor(0x2E2E2E));
    const ImU32 subCol = ToAbgrU32(dark ? HexColor(0x303030) : HexColor(0xF4F5F7));
    const ImU32 bodyCol = ToAbgrU32(dark ? HexColor(0x3B3B3B) : HexColor(0xFFFFFF));
    const ImU32 lineCol = ToAbgrU32(dark ? HexColor(0x525252) : HexColor(0xDCDFE6));
    const ImU32 accentCol = ToAbgrU32(HexColor(0x5B5BFA));

    const float w = rMax.x - rMin.x;
    const float asideW = w * 0.20f;
    const float subW = w * 0.26f;

    dl->AddRectFilled(rMin, rMax, bodyCol, 4.0f * s);
    dl->AddRectFilled(
        rMin, ImVec2(rMin.x + asideW, rMax.y), asideCol, 4.0f * s, ImDrawFlags_RoundCornersLeft);
    dl->AddRectFilled(ImVec2(rMin.x + asideW, rMin.y), ImVec2(rMin.x + asideW + subW, rMax.y), subCol);
    dl->AddLine(ImVec2(rMin.x + asideW + subW, rMin.y + 6.0f * s),
                ImVec2(rMin.x + asideW + subW, rMax.y - 6.0f * s),
                lineCol,
                1.0f);

    const float dotX = rMin.x + asideW + 6.0f * s;
    dl->AddRectFilled(
        ImVec2(dotX, rMin.y + 10.0f * s), ImVec2(dotX + 10.0f * s, rMin.y + 13.0f * s), accentCol, 1.5f);
    dl->AddRectFilled(
        ImVec2(dotX, rMin.y + 17.0f * s), ImVec2(dotX + 10.0f * s, rMin.y + 20.0f * s), lineCol, 1.5f);

    const float cardX = rMin.x + asideW + subW + 6.0f * s;
    const float cardEnd = rMax.x - 6.0f * s;
    if (cardEnd > cardX + 8.0f * s) {
        dl->AddRectFilled(
            ImVec2(cardX, rMin.y + 10.0f * s), ImVec2(cardEnd, rMin.y + 18.0f * s), lineCol, 2.0f);
        dl->AddRectFilled(ImVec2(cardX, rMin.y + 23.0f * s),
                          ImVec2(cardX + (cardEnd - cardX) * 0.65f, rMin.y + 27.0f * s),
                          accentCol,
                          1.5f);
    }
}

struct ThemeCardSpec {
    const char* id = nullptr;
    std::string_view label{};
    int modeIdx = 0;
    bool selected = false;
    ImVec2 pos{};
    ImVec2 size{};
};

bool RenderThemeCard(UiFrameContext& ctx, const ThemeCardSpec& card) {
    const float s = ctx.dpiScale;
    ImGui::SetCursorScreenPos(card.pos);
    const bool clicked = ImGui::InvisibleButton(card.id, ImVec2(card.size.x, card.size.y + 24.0f * s));
    const bool hov = ImGui::IsItemHovered();
    if (hov) {
        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    }

    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 rMax(card.pos.x + card.size.x, card.pos.y + card.size.y);
    const ThemeSketchTarget target{dl, card.pos, rMax};
    if (card.modeIdx == 0) {
        const float midX = card.pos.x + card.size.x * 0.5f;
        {
            ClipRectGuard leftClip(dl, ClipRectSpec{card.pos.x, card.pos.y, midX, rMax.y, true});
            DrawThemeWindowSketch(target, false, s);
        }
        {
            ClipRectGuard rightClip(dl, ClipRectSpec{midX, card.pos.y, rMax.x, rMax.y, true});
            DrawThemeWindowSketch(target, true, s);
        }
    } else {
        DrawThemeWindowSketch(target, card.modeIdx == 2, s);
    }

    const RgbaColor bdCol = (card.selected || hov) ? ctx.palette.primary : ctx.palette.inputBorder;
    dl->AddRect(card.pos, rMax, ToAbgrU32(bdCol), 4.0f * s, 0, card.selected ? 2.0f * s : 1.0f);

    FontGuard fg(ctx.fonts, FontRole::Regular, 12.5f);
    const ImVec2 txtSz = ImGui::CalcTextSize(card.label.data(), card.label.data() + card.label.size());
    dl->AddText(ImVec2(card.pos.x + (card.size.x - txtSz.x) * 0.5f, rMax.y + 6.0f * s),
                ToAbgrU32(card.selected ? ctx.palette.primary : ctx.palette.subLabelText),
                card.label.data(),
                card.label.data() + card.label.size());
    return clicked;
}

struct SubLabelPos {
    float x = 0.0f;
    float y = 0.0f;
    float h = 0.0f;
};

void DrawInlineSubLabel(const UiFrameContext& ctx, const SubLabelPos& pos, std::string_view txt) {
    FontGuard fg(ctx.fonts, FontRole::Regular, 13.5f);
    const ImVec2 sz = ImGui::CalcTextSize(txt.data(), txt.data() + txt.size());
    ImGui::GetWindowDrawList()->AddText(ImVec2(pos.x, pos.y + (pos.h - sz.y) * 0.5f),
                                        ToAbgrU32(ctx.palette.subLabelText),
                                        txt.data(),
                                        txt.data() + txt.size());
}

struct ChkRowItem {
    const char* id = nullptr;
    std::string_view key{};
    bool* val = nullptr;
};

void DrawCheckboxStack(UiFrameContext& ctx, float x, float& y, std::span<const ChkRowItem> items) {
    const float s = ctx.dpiScale;
    const ElStyle elSt{ctx.palette, ctx.fonts, s};
    for (const auto& item : items) {
        ImGui::SetCursorScreenPos(ImVec2(x, y));
        ElCheckbox(elSt, item.id, ctx.tr.Tr(item.key), item.val);
        y += 28.0f * s;
    }
}

void RenderBasicAppearanceRow(UiFrameContext& ctx, PrefsFormState& ps) {
    const float s = ctx.dpiScale;
    const ElStyle elSt{ctx.palette, ctx.fonts, s};

    DrawPrefsFormRow(ctx, ctx.tr.Tr("prefs.appearance"), 44.0f * s, [&](float bx, float) {
        const ImVec2 cardSz(72.0f * s, 46.0f * s);
        const float gapX = 16.0f * s;
        float cy = ImGui::GetCursorScreenPos().y;
        const std::array<std::string_view, 3> names{
            ctx.tr.Tr("prefs.theme_auto"),
            ctx.tr.Tr("prefs.theme_light"),
            ctx.tr.Tr("prefs.theme_dark"),
        };
        const std::array<const char*, 3> ids{"##thm_auto", "##thm_light", "##thm_dark"};
        for (int i = 0; i < 3; ++i) {
            const ImVec2 cardPos(bx + static_cast<float>(i) * (cardSz.x + gapX), cy);
            if (RenderThemeCard(
                    ctx, ThemeCardSpec{ids[i], names[i], i, ps.themeIndex == i, cardPos, cardSz})) {
                ps.themeIndex = i;
                ctx.sink.Dispatch(
                    motrix::app::SetThemeCmd{static_cast<motrix::domain::ThemePreference>(i)});
            }
        }
        cy += cardSz.y + 32.0f * s;
        const std::array<ChkRowItem, 3> items{{
            {"##basic_hide_menu", "prefs.hide_app_menu", &ps.hideAppMenu},
            {"##basic_auto_hide", "prefs.auto_hide_window", &ps.autoHideWindow},
            {"##basic_prog_bar", "prefs.show_progress_bar", &ps.showProgressBar},
        }};
        DrawCheckboxStack(ctx, bx, cy, items);
        ImGui::SetCursorScreenPos(ImVec2(bx, cy - 6.0f * s));
        ImGui::Dummy(ImVec2(0.0f, 0.0f));
    });

    DrawPrefsFormRow(ctx, ctx.tr.Tr("prefs.language"), 30.0f * s, [&](float bx, float) {
        static constexpr std::array<const char*, 2> kLocales{"English", "简体中文"};
        ImGui::SetCursorScreenPos(ImVec2(bx, ImGui::GetCursorScreenPos().y));
        if (ElSelectCombo(elSt, ElSelectSpec{"##basic_locale", &ps.localeIndex, kLocales, 186.0f * s})) {
            ctx.sink.Dispatch(motrix::app::SetLocaleCmd{
                ps.localeIndex == 1 ? motrix::core::Locale::ZhCN : motrix::core::Locale::EnUS});
        }
    });
}

void RenderBasicStartupAndDirRows(UiFrameContext& ctx, PrefsFormState& ps) {
    const float s = ctx.dpiScale;
    const ElStyle elSt{ctx.palette, ctx.fonts, s};

    DrawPrefsFormRow(ctx, ctx.tr.Tr("prefs.startup"), 20.0f * s, [&](float bx, float) {
        float cy = ImGui::GetCursorScreenPos().y;
        const std::array<ChkRowItem, 3> items{{
            {"##basic_login", "prefs.open_at_login", &ps.openAtLogin},
            {"##basic_win_state", "prefs.keep_window_state", &ps.keepWindowState},
            {"##basic_resume_all", "prefs.auto_resume_all", &ps.resumeAllWhenAppLaunched},
        }};
        DrawCheckboxStack(ctx, bx, cy, items);
        ImGui::SetCursorScreenPos(ImVec2(bx, cy - 6.0f * s));
        ImGui::Dummy(ImVec2(0.0f, 0.0f));
    });

    DrawPrefsFormRow(ctx, ctx.tr.Tr("prefs.default_dir"), 30.0f * s, [&](float bx, float bw) {
        ImGui::SetCursorScreenPos(ImVec2(bx, ImGui::GetCursorScreenPos().y));
        ElDirectoryInput(elSt,
                         ElDirInputSpec{
                             "##basic_dir",
                             ps.dirBuffer.data(),
                             ps.dirBuffer.size(),
                             std::min(bw, 450.0f * s),
                             ctx.state.prefs.historyDirectories,
                             ctx.state.prefs.favoriteDirectories,
                             [&ctx](std::string_view cur) { return ctx.host.PickFolder(cur); },
                         });
    });
}

void RenderBasicTransferRow(UiFrameContext& ctx, PrefsFormState& ps) {
    const float s = ctx.dpiScale;
    const ElStyle elSt{ctx.palette, ctx.fonts, s};

    DrawPrefsFormRow(ctx, ctx.tr.Tr("prefs.transfer_settings"), 30.0f * s, [&](float bx, float) {
        static constexpr std::array<const char*, 2> kUnits{"KB/s", "MB/s"};
        float cy = ImGui::GetCursorScreenPos().y;
        const float subW = 74.0f * s;
        const float numW = 128.0f * s;
        const float unitW = 94.0f * s;

        DrawInlineSubLabel(ctx, SubLabelPos{bx, cy, 30.0f * s}, ctx.tr.Tr("prefs.transfer_upload"));
        ImGui::SetCursorScreenPos(ImVec2(bx + subW, cy));
        ElInputNumberInt(elSt, "##basic_ul_num", ElIntSpec{&ps.ulLimitKbps, 0, 999999, 10, numW});
        int ulUnit = ps.ulUnitMb ? 1 : 0;
        ImGui::SetCursorScreenPos(ImVec2(bx + subW + numW + 8.0f * s, cy));
        if (ElSelectCombo(elSt, ElSelectSpec{"##basic_ul_unit", &ulUnit, kUnits, unitW})) {
            ps.ulUnitMb = (ulUnit == 1);
        }

        cy += 42.0f * s;
        DrawInlineSubLabel(ctx, SubLabelPos{bx, cy, 30.0f * s}, ctx.tr.Tr("prefs.transfer_download"));
        ImGui::SetCursorScreenPos(ImVec2(bx + subW, cy));
        ElInputNumberInt(elSt, "##basic_dl_num", ElIntSpec{&ps.dlLimitKbps, 0, 999999, 10, numW});
        int dlUnit = ps.dlUnitMb ? 1 : 0;
        ImGui::SetCursorScreenPos(ImVec2(bx + subW + numW + 8.0f * s, cy));
        if (ElSelectCombo(elSt, ElSelectSpec{"##basic_dl_unit", &dlUnit, kUnits, unitW})) {
            ps.dlUnitMb = (dlUnit == 1);
        }
        ImGui::SetCursorScreenPos(ImVec2(bx, cy + 30.0f * s));
        ImGui::Dummy(ImVec2(0.0f, 0.0f));
    });
}

void RenderBasicBtAndTaskRows(UiFrameContext& ctx, PrefsFormState& ps) {
    const float s = ctx.dpiScale;
    const ElStyle elSt{ctx.palette, ctx.fonts, s};

    DrawPrefsFormRow(ctx, ctx.tr.Tr("prefs.bt_settings"), 20.0f * s, [&](float bx, float) {
        float cy = ImGui::GetCursorScreenPos().y;
        const std::array<ChkRowItem, 3> btItems{{
            {"##bt_save_meta", "prefs.bt_save_metadata", &ps.btSaveMetadata},
            {"##bt_auto_dl", "prefs.bt_auto_download", &ps.btAutoDownloadContent},
            {"##bt_force_enc", "prefs.bt_force_encryption", &ps.btForceEncryption},
        }};
        DrawCheckboxStack(ctx, bx, cy, btItems);

        cy += 6.0f * s;
        ImGui::SetCursorScreenPos(ImVec2(bx, cy));
        ElSwitch(elSt, "##bt_keep_seed", ctx.tr.Tr("prefs.keep_seeding"), &ps.keepSeeding);
        cy += 34.0f * s;

        const float subW = 132.0f * s;
        DrawInlineSubLabel(ctx, SubLabelPos{bx, cy, 30.0f * s}, ctx.tr.Tr("prefs.seed_ratio"));
        ImGui::SetCursorScreenPos(ImVec2(bx + subW, cy));
        ElInputNumberFloat(
            elSt, "##bt_seed_ratio", ElFloatSpec{&ps.seedRatio, 0.0f, 100.0f, 0.5f, 128.0f * s});
        cy += 42.0f * s;

        DrawInlineSubLabel(ctx, SubLabelPos{bx, cy, 30.0f * s}, ctx.tr.Tr("prefs.seed_time"));
        ImGui::SetCursorScreenPos(ImVec2(bx + subW, cy));
        ElInputNumberInt(elSt, "##bt_seed_time", ElIntSpec{&ps.seedTime, 0, 525600, 10, 128.0f * s});
        ImGui::SetCursorScreenPos(ImVec2(bx, cy + 30.0f * s));
        ImGui::Dummy(ImVec2(0.0f, 0.0f));
    });

    DrawPrefsFormRow(ctx, ctx.tr.Tr("prefs.task_manage"), 30.0f * s, [&](float bx, float) {
        float cy = ImGui::GetCursorScreenPos().y;
        const float subW = 166.0f * s;
        DrawInlineSubLabel(ctx, SubLabelPos{bx, cy, 30.0f * s}, ctx.tr.Tr("prefs.concurrency"));
        ImGui::SetCursorScreenPos(ImVec2(bx + subW, cy));
        ElInputNumberInt(elSt, "##tm_conc", ElIntSpec{&ps.maxConcurrent, 1, 64, 1, 128.0f * s});
        cy += 42.0f * s;

        DrawInlineSubLabel(ctx, SubLabelPos{bx, cy, 30.0f * s}, ctx.tr.Tr("prefs.split"));
        ImGui::SetCursorScreenPos(ImVec2(bx + subW, cy));
        ElInputNumberInt(elSt, "##tm_split", ElIntSpec{&ps.maxConnPerServer, 1, 128, 1, 128.0f * s});
        cy += 42.0f * s;

        const std::array<ChkRowItem, 4> tmItems{{
            {"##tm_cont", "prefs.continue", &ps.continueDownloads},
            {"##tm_nav", "prefs.new_task_show", &ps.newTaskShowDownloading},
            {"##tm_notif", "prefs.task_notification", &ps.taskNotification},
            {"##tm_nodel", "prefs.no_confirm_del", &ps.noConfirmBeforeDelete},
        }};
        DrawCheckboxStack(ctx, bx, cy, tmItems);
        ImGui::SetCursorScreenPos(ImVec2(bx, cy - 6.0f * s));
        ImGui::Dummy(ImVec2(0.0f, 0.0f));
    });
}

}  // namespace

void RenderPrefsBasicTab(UiFrameContext& ctx, PrefsFormState& ps) {
    RenderBasicAppearanceRow(ctx, ps);
    RenderBasicStartupAndDirRows(ctx, ps);
    RenderBasicTransferRow(ctx, ps);
    RenderBasicBtAndTaskRows(ctx, ps);
}

}  // namespace motrix::ui

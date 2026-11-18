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

constexpr const char* kAddProxyTipsUrl = "https://motrix.app/release";

struct DashedRectSpec {
    ImVec2 min{};
    ImVec2 max{};
    ImU32 col = 0;
    float s = 1.0f;
};

bool MatchesTorrentFilter(std::string_view path, int filter) noexcept {
    if (filter == 1) {
        return motrix::domain::IsVideoExtension(path);
    }
    if (filter == 2) {
        return motrix::domain::IsAudioExtension(path);
    }
    if (filter == 3) {
        return motrix::domain::IsImageExtension(path);
    }
    return true;
}

void DrawDashedRect(ImDrawList* dl, const DashedRectSpec& r) {
    const float dash = 4.0f * r.s;
    const float step = dash * 2.0f;
    const float rad = 4.0f * r.s;
    for (float x = r.min.x + rad; x < r.max.x - rad; x += step) {
        const float xEnd = std::min(r.max.x - rad, x + dash);
        dl->AddLine(ImVec2(x, r.min.y), ImVec2(xEnd, r.min.y), r.col, 1.0f);
        dl->AddLine(ImVec2(x, r.max.y), ImVec2(xEnd, r.max.y), r.col, 1.0f);
    }
    for (float y = r.min.y + rad; y < r.max.y - rad; y += step) {
        const float yEnd = std::min(r.max.y - rad, y + dash);
        dl->AddLine(ImVec2(r.min.x, y), ImVec2(r.min.x, yEnd), r.col, 1.0f);
        dl->AddLine(ImVec2(r.max.x, y), ImVec2(r.max.x, yEnd), r.col, 1.0f);
    }
}

void RenderTorrentDropzone(UiFrameContext& ctx, AddTaskFormState& form, float fieldW) {
    const float s = ctx.dpiScale;
    const float dropH = 114.0f * s;
    const ImVec2 dMin = ImGui::GetCursorScreenPos();
    const ImVec2 dMax(dMin.x + fieldW, dMin.y + dropH);
    if (ImGui::InvisibleButton("##torrent_dropzone", ImVec2(fieldW, dropH))) {
        if (auto chosen = ctx.host.PickTorrentFile()) {
            LoadTorrentFileIntoForm(form, *chosen);
        }
    }
    const bool hovered = ImGui::IsItemHovered();
    if (hovered) {
        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    }
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(
        dMin, dMax, ToAbgrU32(hovered ? ctx.palette.subnavBg : ctx.palette.inputBg), 4.0f * s);
    DrawDashedRect(dl,
                   {dMin, dMax, ToAbgrU32(hovered ? ctx.palette.primary : ctx.palette.inputBorder), s});

    const float iconSz = 30.0f * s;
    DrawMotrixIcon(IconSpec{
        MotrixIcon::Inbox,
        dMin.x + (fieldW - iconSz) * 0.5f,
        dMin.y + 24.0f * s,
        iconSz,
        iconSz,
        hovered ? ctx.palette.primary : ctx.palette.taskMetaText,
        s,
    });

    const std::string_view dropLbl = ctx.tr.Tr("task.drop_torrent");
    FontGuard fg(ctx.fonts, FontRole::Regular, 13.5f);
    const ImVec2 txtSz = ImGui::CalcTextSize(dropLbl.data(), dropLbl.data() + dropLbl.size());
    dl->AddText(ImVec2(dMin.x + (fieldW - txtSz.x) * 0.5f, dMin.y + 68.0f * s),
                ToAbgrU32(ctx.palette.taskMetaText),
                dropLbl.data(),
                dropLbl.data() + dropLbl.size());

    if (!form.torrentError.empty()) {
        ImGui::Dummy(ImVec2(0.0f, 6.0f * s));
        StyleColorGuard errCol(ImGuiCol_Text, ctx.palette.progressError);
        FontGuard ef(ctx.fonts, FontRole::Regular, 13.0f);
        ImGui::TextWrapped("%s: %s", ctx.tr.Tr("task.torrent_error").data(), form.torrentError.c_str());
    }
}

bool RenderTorrentHeaderBar(UiFrameContext& ctx,
                            AddTaskFormState& form,
                            const motrix::domain::TorrentMetadata& meta,
                            float fieldW) {
    const float s = ctx.dpiScale;
    const float barH = 34.0f * s;
    const ImVec2 barMin = ImGui::GetCursorScreenPos();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(
        barMin, ImVec2(barMin.x + fieldW, barMin.y + barH), ToAbgrU32(ctx.palette.subnavBg), 4.0f * s);
    const float clrSz = 26.0f * s;
    const ImVec2 clrPos(barMin.x + fieldW - clrSz - 4.0f * s, barMin.y + 4.0f * s);
    {
        FontGuard tf(ctx.fonts, FontRole::Medium, 13.5f);
        StyleColorGuard tc(ImGuiCol_Text, ctx.palette.panelTitle);
        ClipRectGuard clip(
            ImGui::GetWindowDrawList(),
            {barMin.x + 10.0f * s, barMin.y, clrPos.x - 6.0f * s, barMin.y + 36.0f * s, true});
        ImGui::SetCursorScreenPos(ImVec2(barMin.x + 10.0f * s, barMin.y + 7.0f * s));
        const std::string hdr =
            std::format("{}  ({})", meta.name, motrix::domain::FormatBytes(meta.totalLength));
        ImGui::TextUnformatted(hdr.c_str());
    }

    ImGui::SetCursorScreenPos(clrPos);
    if (ImGui::InvisibleButton("##torrent_clear", ImVec2(clrSz, clrSz))) {
        form.torrentMeta.reset();
        form.torrentBase64.clear();
        form.torrentFileSelected.clear();
        form.torrentPath.clear();
        form.torrentError.clear();
        if (!form.autoFilledTorrentName.empty()
            && std::string_view(form.nameBuffer.data()) == form.autoFilledTorrentName) {
            form.nameBuffer.fill('\0');
        }
        form.autoFilledTorrentName.clear();
        return false;
    }
    const bool clrHov = ImGui::IsItemHovered();
    DrawMotrixIcon(IconSpec{
        MotrixIcon::Trash,
        clrPos.x + (clrSz - 14.0f * s) * 0.5f,
        clrPos.y + (clrSz - 14.0f * s) * 0.5f,
        14.0f * s,
        14.0f * s,
        clrHov ? ctx.palette.progressError : ctx.palette.taskMetaText,
        s,
    });
    ImGui::SetCursorScreenPos(ImVec2(barMin.x, barMin.y + barH + 6.0f * s));
    return true;
}

void ApplyTorrentFilterChoice(AddTaskFormState& form,
                              const motrix::domain::TorrentMetadata& meta,
                              int idx) {
    if (idx == 0) {
        const auto selCount = static_cast<std::size_t>(
            std::count(form.torrentFileSelected.begin(), form.torrentFileSelected.end(), true));
        const bool allOn = (selCount < form.torrentFileSelected.size());
        std::fill(form.torrentFileSelected.begin(), form.torrentFileSelected.end(), allOn);
        form.torrentFileFilter = 0;
        return;
    }
    form.torrentFileFilter = (form.torrentFileFilter == idx) ? 0 : idx;
    if (form.torrentFileFilter == 0) {
        return;
    }
    for (std::size_t i = 0; i < meta.files.size(); ++i) {
        form.torrentFileSelected[i] = MatchesTorrentFilter(meta.files[i].path, form.torrentFileFilter);
    }
}

void RenderTorrentFilterPills(UiFrameContext& ctx,
                              AddTaskFormState& form,
                              const motrix::domain::TorrentMetadata& meta) {
    const float s = ctx.dpiScale;
    const std::array<std::pair<std::string_view, int>, 4> pills{{
        {ctx.tr.Tr("detail.all"), 0},
        {ctx.tr.Tr("detail.video"), 1},
        {ctx.tr.Tr("detail.audio"), 2},
        {ctx.tr.Tr("detail.image"), 3},
    }};
    for (std::size_t k = 0; k < pills.size(); ++k) {
        if (k > 0) {
            ImGui::SameLine();
        }
        const bool active = (form.torrentFileFilter == pills[k].second);
        StyleColorGuard bg(ImGuiCol_Button, active ? ctx.palette.primary : ctx.palette.subnavBg);
        StyleColorGuard fg(ImGuiCol_Text, active ? HexColor(0xFFFFFF) : ctx.palette.panelTitle);
        FontGuard fnt(ctx.fonts, FontRole::Regular, 12.0f);
        if (ImGui::Button(pills[k].first.data(), ImVec2(66.0f * s, 24.0f * s))) {
            ApplyTorrentFilterChoice(form, meta, pills[k].second);
        }
    }
}

void RenderTorrentFilesTable(UiFrameContext& ctx,
                             AddTaskFormState& form,
                             const motrix::domain::TorrentMetadata& meta,
                             float fieldW) {
    const float s = ctx.dpiScale;
    constexpr ImGuiTableFlags kFlags =
        ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_ScrollY;
    FontGuard tblFont(ctx.fonts, FontRole::Regular, 13.0f);
    TableGuard tbl("##add_torrent_files", {4, kFlags, fieldW, 154.0f * s});
    if (!tbl.IsOpen()) {
        return;
    }
    ImGui::TableSetupColumn("##sel", ImGuiTableColumnFlags_WidthFixed, 28.0f * s);
    ImGui::TableSetupColumn(
        ctx.tr.Tr("detail.file_name").data(), ImGuiTableColumnFlags_WidthStretch, 2.0f);
    ImGui::TableSetupColumn(
        ctx.tr.Tr("detail.file_ext").data(), ImGuiTableColumnFlags_WidthFixed, 54.0f * s);
    ImGui::TableSetupColumn(
        ctx.tr.Tr("detail.file_size").data(), ImGuiTableColumnFlags_WidthFixed, 76.0f * s);
    ImGui::TableHeadersRow();

    for (std::size_t i = 0; i < meta.files.size(); ++i) {
        const auto& f = meta.files[i];
        if (form.torrentFileFilter != 0 && !MatchesTorrentFilter(f.path, form.torrentFileFilter)) {
            continue;
        }
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        IdGuard rowId(static_cast<int>(i));
        bool chk = (i < form.torrentFileSelected.size()) ? form.torrentFileSelected[i] : true;
        if (ImGui::Checkbox("##tfchk", &chk) && i < form.torrentFileSelected.size()) {
            form.torrentFileSelected[i] = chk;
        }
        ImGui::TableNextColumn();
        ImGui::TextUnformatted(f.path.c_str());

        ImGui::TableNextColumn();
        ImGui::TextUnformatted(f.extension.empty() ? "-" : f.extension.c_str());

        ImGui::TableNextColumn();
        const std::string szStr = motrix::domain::FormatBytes(f.length);
        ImGui::TextUnformatted(szStr.c_str());
    }
}

struct CaptionPos {
    float x = 0.0f;
    float y = 0.0f;
    float h = 0.0f;
};

void DrawAlignedFieldCaption(const UiFrameContext& ctx, const CaptionPos& pos, std::string_view text) {
    FontGuard fnt(ctx.fonts, FontRole::Regular, 13.5f);
    const ImVec2 sz = ImGui::CalcTextSize(text.data(), text.data() + text.size());
    ImGui::GetWindowDrawList()->AddText(ImVec2(pos.x, pos.y + (pos.h - sz.y) * 0.5f),
                                        ToAbgrU32(ctx.palette.subLabelText),
                                        text.data(),
                                        text.data() + text.size());
}

void RenderAddTaskExtraAdvancedRows(UiFrameContext& ctx, AddTaskFormState& form, ModalFlowPos& flow) {
    const float s = ctx.dpiScale;
    const float rowH = 30.0f * s;
    const float gapY = 12.0f * s;
    const float lblW = 92.0f * s;
    const float fieldW = flow.bodyW - lblW;
    const ElStyle elSt{ctx.palette, ctx.fonts, s, &ctx.anim};

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
                              ctx.host.OpenPathInExplorer(kAddProxyTipsUrl);
                          }});
    flow.curY += 20.0f * s;

    ImGui::SetCursorScreenPos(ImVec2(flow.x0 + lblW, flow.curY));
    ElCheckbox(
        elSt, "##add_nav_dl", ctx.tr.Tr("task.navigate_to_downloading"), &form.navigateToDownloading);
    flow.curY += 22.0f * s;
}

}  // namespace

void LoadTorrentFileIntoForm(AddTaskFormState& form, std::string_view utf8Path) {
    form.torrentMeta.reset();
    form.torrentBase64.clear();
    form.torrentFileSelected.clear();
    form.torrentError.clear();
    form.torrentPath = std::string(utf8Path);
    form.torrentFileFilter = 0;

    const std::filesystem::path p = motrix::core::PathFromUtf8(utf8Path);
    auto bytes = motrix::core::ReadFileBytes(p);
    if (!bytes) {
        form.torrentError = bytes.error().message;
        return;
    }
    auto meta = motrix::domain::ParseTorrentBytes(*bytes);
    if (!meta) {
        form.torrentError = meta.error().message;
        return;
    }

    form.torrentBase64 = motrix::domain::Base64Encode(*bytes);
    form.torrentFileSelected.assign(meta->files.size(), true);
    const bool canAutoFillName =
        (form.nameBuffer[0] == '\0')
        || (!form.autoFilledTorrentName.empty()
            && std::string_view(form.nameBuffer.data()) == form.autoFilledTorrentName);
    if (canAutoFillName) {
        if (!meta->name.empty()) {
            CopyUtf8ToBuffer(meta->name, form.nameBuffer.data(), form.nameBuffer.size());
            form.autoFilledTorrentName = form.nameBuffer.data();
        } else {
            form.nameBuffer.fill('\0');
            form.autoFilledTorrentName.clear();
        }
    }
    form.torrentMeta = std::move(*meta);
}

void RenderAddTorrentTab(UiFrameContext& ctx, AddTaskFormState& form) {
    const float fieldW = ImGui::GetContentRegionAvail().x;
    if (!form.torrentMeta.has_value()) {
        RenderTorrentDropzone(ctx, form, fieldW);
        return;
    }
    const auto meta = *form.torrentMeta;
    if (!RenderTorrentHeaderBar(ctx, form, meta, fieldW)) {
        return;
    }
    RenderTorrentFilterPills(ctx, form, meta);
    ImGui::Dummy(ImVec2(0.0f, 4.0f * ctx.dpiScale));
    RenderTorrentFilesTable(ctx, form, meta, fieldW);
}

void RenderAddTaskAdvancedFields(UiFrameContext& ctx, AddTaskFormState& form, ModalFlowPos& flow) {
    const float s = ctx.dpiScale;
    const float rowH = 30.0f * s;
    const float gapY = 12.0f * s;
    const float lblW = 92.0f * s;
    const float fieldW = flow.bodyW - lblW;
    const ElStyle elSt{ctx.palette, ctx.fonts, s, &ctx.anim};

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

}  // namespace motrix::ui

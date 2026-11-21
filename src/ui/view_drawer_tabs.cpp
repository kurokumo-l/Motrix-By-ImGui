module;

#include "imgui.h"

module motrix.ui;

import std;
import motrix.core;
import motrix.domain;
import :views_internal;
import :el_widgets;
import :theme;
import :icons;
import :anim;
import :scope_guard;

namespace motrix::ui {

namespace {

enum class TriCheckState {
    Unchecked = 0,
    Indeterminate,
    Checked,
};

[[nodiscard]] std::string_view ExtractBaseName(std::string_view path) noexcept {
    const auto slash = path.find_last_of("/\\");
    if (slash == std::string_view::npos || slash + 1 >= path.size()) {
        return path;
    }
    return path.substr(slash + 1);
}

[[nodiscard]] std::string ExtractExtNoDot(std::string_view path) {
    std::string ext = motrix::domain::ExtractFileExtension(path);
    if (!ext.empty() && ext.front() == '.') {
        ext.erase(ext.begin());
    }
    return ext;
}

[[nodiscard]] std::string FormatPercentValue(double pct) {
    std::string text = std::format("{:.1f}", std::clamp(pct, 0.0, 100.0));
    if (text.size() >= 2 && text.ends_with(".0")) {
        text.resize(text.size() - 2);
    }
    return text;
}

[[nodiscard]] std::string FormatFilePercent(std::uint64_t completed, std::uint64_t total) {
    if (total == 0) {
        return "0";
    }
    return FormatPercentValue(static_cast<double>(completed) * 100.0 / static_cast<double>(total));
}

[[nodiscard]] bool
DrawTriCheckboxCell(const ElStyle& st, const char* id, TriCheckState state, float colW) {
    const float s = st.dpiScale;
    const MotrixPalette& p = st.palette;
    const float boxSz = 14.0f * s;
    const ImVec2 cellPos = ImGui::GetCursorScreenPos();
    const float hitW = std::max(boxSz, colW);
    const float hitH = std::max(boxSz, ImGui::GetTextLineHeight());
    const bool clicked = ImGui::InvisibleButton(id, ImVec2(hitW, hitH));
    const bool hovered = ImGui::IsItemHovered();
    if (hovered) {
        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    }

    const float bx = cellPos.x + (hitW - boxSz) * 0.5f;
    const float by = cellPos.y + (hitH - boxSz) * 0.5f;
    const ImVec2 bMin(bx, by);
    const ImVec2 bMax(bx + boxSz, by + boxSz);
    const bool active = (state != TriCheckState::Unchecked);

    ImDrawList* dl = ImGui::GetWindowDrawList();
    if (active) {
        dl->AddRectFilled(bMin, bMax, ToAbgrU32(p.primary), 2.0f * s);
    } else {
        dl->AddRectFilled(bMin, bMax, ToAbgrU32(p.inputBg), 2.0f * s);
        dl->AddRect(bMin, bMax, ToAbgrU32(hovered ? p.primary : p.inputBorder), 2.0f * s, 0, 1.0f);
    }

    const ImU32 markCol = ToAbgrU32(HexColor(0xFFFFFF));
    if (state == TriCheckState::Indeterminate) {
        const float yMid = by + boxSz * 0.5f;
        dl->AddLine(ImVec2(bx + 3.5f * s, yMid), ImVec2(bx + boxSz - 3.5f * s, yMid), markCol, 1.6f * s);
    } else if (state == TriCheckState::Checked) {
        dl->AddLine(ImVec2(bx + 3.0f * s, by + 7.2f * s),
                    ImVec2(bx + 5.8f * s, by + 10.2f * s),
                    markCol,
                    1.6f * s);
        dl->AddLine(ImVec2(bx + 5.8f * s, by + 10.2f * s),
                    ImVec2(bx + 11.2f * s, by + 4.2f * s),
                    markCol,
                    1.6f * s);
    }
    return clicked;
}

void DrawCellAlignedText(std::string_view text, bool rightAlign, RgbaColor col) {
    if (rightAlign) {
        const float avail = ImGui::GetContentRegionAvail().x;
        const ImVec2 sz = ImGui::CalcTextSize(text.data(), text.data() + text.size());
        if (avail > sz.x) {
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + avail - sz.x);
        }
    }
    StyleColorGuard tc(ImGuiCol_Text, col);
    ImGui::TextUnformatted(text.data(), text.data() + text.size());
}

void DrawTableOuterFrame(ImDrawList* dl, ImVec2 minPt, float width, float height, RgbaColor col) {
    dl->AddRect(minPt, ImVec2(minPt.x + width, minPt.y + height), ToAbgrU32(col), 2.0f, 0, 1.0f);
}

[[nodiscard]] bool MatchesPendingSelection(const motrix::domain::DownloadTaskView& task,
                                           const std::vector<bool>& expected) noexcept {
    if (expected.size() != task.files.size()) {
        return false;
    }
    for (std::size_t i = 0; i < task.files.size(); ++i) {
        if (task.files[i].selected != expected[i]) {
            return false;
        }
    }
    return true;
}

void RemapDirtyFilesDraft(DrawerUiState& ds, const motrix::domain::DownloadTaskView& task) {
    std::vector<bool> nextSel(task.files.size(), false);
    std::vector<int> nextIdx(task.files.size(), 0);
    for (std::size_t i = 0; i < task.files.size(); ++i) {
        const int fileIdx = task.files[i].index > 0 ? task.files[i].index : static_cast<int>(i + 1);
        nextIdx[i] = fileIdx;
        nextSel[i] = task.files[i].selected;
        for (std::size_t k = 0; k < ds.draftFileIndices.size() && k < ds.draftSelected.size(); ++k) {
            if (ds.draftFileIndices[k] == fileIdx) {
                nextSel[i] = ds.draftSelected[k];
                break;
            }
        }
    }
    ds.draftSelected = std::move(nextSel);
    ds.draftFileIndices = std::move(nextIdx);
}

void RebuildFilesDraftFromTask(DrawerUiState& ds, const motrix::domain::DownloadTaskView& task) {
    ds.loadedGid = task.gid;
    ds.draftSelected.clear();
    ds.draftFileIndices.clear();
    ds.draftSelected.reserve(task.files.size());
    ds.draftFileIndices.reserve(task.files.size());
    for (std::size_t i = 0; i < task.files.size(); ++i) {
        const auto& f = task.files[i];
        ds.draftSelected.push_back(f.selected);
        ds.draftFileIndices.push_back(f.index > 0 ? f.index : static_cast<int>(i + 1));
    }
    ds.dirty = false;
    ds.pendingApply = false;
    ds.pendingExpected.clear();
    ds.fileFilter = 0;
}

void SyncDrawerFilesDraft(DrawerUiState& ds, const motrix::domain::DownloadTaskView& task) {
    if (ds.loadedGid != task.gid) {
        RebuildFilesDraftFromTask(ds, task);
        return;
    }
    if (ds.pendingApply && !ds.dirty) {
        const bool settled = MatchesPendingSelection(task, ds.pendingExpected)
                             || std::chrono::steady_clock::now() >= ds.pendingUntil;
        if (!settled) {
            return;
        }
        ds.pendingApply = false;
        ds.pendingExpected.clear();
    }
    if (ds.draftSelected.size() != task.files.size()) {
        if (ds.dirty) {
            RemapDirtyFilesDraft(ds, task);
            return;
        }
        RebuildFilesDraftFromTask(ds, task);
        return;
    }
    if (ds.dirty) {
        return;
    }
    for (std::size_t i = 0; i < task.files.size(); ++i) {
        ds.draftSelected[i] = task.files[i].selected;
    }
}

void ApplyMediaSelection(DrawerUiState& ds,
                         const motrix::domain::DownloadTaskView& task,
                         int filterKind) {
    const std::size_t n = std::min(ds.draftSelected.size(), task.files.size());
    bool anyMatchSelected = false;
    bool allMatchSelected = true;
    for (std::size_t i = 0; i < n; ++i) {
        if (MatchesMediaFilter(task.files[i].path, filterKind)) {
            if (ds.draftSelected[i]) {
                anyMatchSelected = true;
            } else {
                allMatchSelected = false;
            }
        }
    }
    const bool selectTargets = !(ds.fileFilter == filterKind && anyMatchSelected && allMatchSelected);
    ds.fileFilter = selectTargets ? filterKind : 0;
    for (std::size_t i = 0; i < n; ++i) {
        if (selectTargets) {
            ds.draftSelected[i] = MatchesMediaFilter(task.files[i].path, filterKind);
        } else {
            ds.draftSelected[i] = true;
        }
    }
    ds.dirty = true;
}

[[nodiscard]] std::string
FormatFilesSummary(const UiFrameContext& ctx, std::size_t count, std::uint64_t bytes) {
    const std::string sizeStr = motrix::domain::FormatBytesFixed(bytes, 1, true);
    if (ctx.tr.CurrentLocale() == motrix::core::Locale::ZhCN) {
        return std::format("已选：{}个文件，共 {}", count, sizeStr);
    }
    return std::format("Selected: {} {}, total size {}", count, count == 1 ? "file" : "files", sizeStr);
}

void RenderFilesMediaGroupAndSummary(UiFrameContext& ctx,
                                     DrawerUiState& ds,
                                     const motrix::domain::DownloadTaskView& task,
                                     float availW) {
    const float s = ctx.dpiScale;
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const float btnW = 38.0f * s;
    const float btnH = 28.0f * s;
    const float totalGroupW = btnW * 3.0f;
    const std::array<std::pair<MotrixIcon, int>, 3> items{{
        {MotrixIcon::Video, 1},
        {MotrixIcon::Audio, 2},
        {MotrixIcon::Image, 3},
    }};

    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(origin,
                      ImVec2(origin.x + totalGroupW, origin.y + btnH),
                      ToAbgrU32(ctx.palette.inputBg),
                      3.0f * s);
    dl->AddRect(origin,
                ImVec2(origin.x + totalGroupW, origin.y + btnH),
                ToAbgrU32(ctx.palette.inputBorder),
                3.0f * s,
                0,
                1.0f);

    for (std::size_t k = 0; k < items.size(); ++k) {
        IdGuard bgId(items[k].second);
        const float bx = origin.x + static_cast<float>(k) * btnW;
        if (k > 0) {
            dl->AddLine(ImVec2(bx, origin.y),
                        ImVec2(bx, origin.y + btnH),
                        ToAbgrU32(ctx.palette.inputBorder),
                        1.0f);
        }
        ImGui::SetCursorScreenPos(ImVec2(bx, origin.y));
        if (ImGui::InvisibleButton("##media_flt", ImVec2(btnW, btnH))) {
            ApplyMediaSelection(ds, task, items[k].second);
        }
        const bool hov = ImGui::IsItemHovered();
        const bool active = (ds.fileFilter == items[k].second);
        if (hov) {
            ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
        }
        const float icoSz = 14.0f * s;
        const RgbaColor icoCol = (active || hov) ? ctx.palette.primary : ctx.palette.panelTitle;
        DrawMotrixIcon(IconSpec{
            items[k].first,
            bx + (btnW - icoSz) * 0.5f,
            origin.y + (btnH - icoSz) * 0.5f,
            icoSz,
            icoSz,
            icoCol,
            s,
        });
    }

    std::size_t selCount = 0;
    std::uint64_t selBytes = 0;
    const std::size_t n = std::min(ds.draftSelected.size(), task.files.size());
    for (std::size_t i = 0; i < n; ++i) {
        if (ds.draftSelected[i]) {
            ++selCount;
            const bool singleUnknown = (task.files.size() == 1 && task.files[i].length == 0);
            selBytes +=
                (singleUnknown && task.totalLength > 0) ? task.totalLength : task.files[i].length;
        }
    }
    const std::string summary = FormatFilesSummary(ctx, selCount, selBytes);
    FontGuard sf(ctx.fonts, FontRole::Regular, 13.0f);
    const ImVec2 sumSz = ImGui::CalcTextSize(summary.c_str());
    dl->AddText(ImVec2(origin.x + availW - sumSz.x, origin.y + (btnH - sumSz.y) * 0.5f),
                ToAbgrU32(ctx.palette.subLabelText),
                summary.c_str());

    ImGui::SetCursorScreenPos(ImVec2(origin.x, origin.y + btnH + 4.0f * s));
    ImGui::Dummy(ImVec2(0.0f, 0.0f));
}

void DrawFilesHeaderRow(UiFrameContext& ctx,
                        DrawerUiState& ds,
                        const motrix::domain::DownloadTaskView& task,
                        float selColW) {
    const ElStyle elSt{ctx.palette, ctx.fonts, ctx.dpiScale, &ctx.anim};
    const std::size_t n = std::min(ds.draftSelected.size(), task.files.size());
    const std::size_t selCount = static_cast<std::size_t>(std::count(
        ds.draftSelected.begin(), ds.draftSelected.begin() + static_cast<std::ptrdiff_t>(n), true));
    const TriCheckState hdrState =
        (n == 0 || selCount == 0)
            ? TriCheckState::Unchecked
            : ((selCount == n) ? TriCheckState::Checked : TriCheckState::Indeterminate);

    ImGui::TableNextRow(ImGuiTableRowFlags_Headers);
    ImGui::TableSetColumnIndex(0);
    if (DrawTriCheckboxCell(elSt, "##drw_all_chk", hdrState, selColW) && n > 0) {
        const bool next = (selCount != n);
        std::fill(
            ds.draftSelected.begin(), ds.draftSelected.begin() + static_cast<std::ptrdiff_t>(n), next);
        ds.dirty = true;
        ds.fileFilter = 0;
    }

    const std::array<std::pair<std::string_view, bool>, 5> cols{{
        {ctx.tr.Tr("detail.file_name"), false},
        {ctx.tr.Tr("detail.file_ext"), false},
        {ctx.tr.Tr("detail.progress"), true},
        {{}, true},  // Drawn as vector checkmark icon below
        {ctx.tr.Tr("detail.file_size"), true},
    }};
    for (int c = 0; c < 5; ++c) {
        ImGui::TableSetColumnIndex(c + 1);
        if (c == 3) {
            const float s = ctx.dpiScale;
            const ImVec2 pos = ImGui::GetCursorScreenPos();
            const float avail = ImGui::GetContentRegionAvail().x;
            const float lineH = ImGui::GetTextLineHeight();
            const float cx = pos.x + std::max(10.0f * s, avail) - 10.0f * s;
            const float cy = pos.y + lineH * 0.5f;
            ImDrawList* dl = ImGui::GetWindowDrawList();
            const ImU32 col = ToAbgrU32(ctx.palette.subLabelText);
            dl->AddLine(ImVec2(cx + 0.5f * s, cy - 0.5f * s),
                        ImVec2(cx + 3.5f * s, cy + 2.8f * s),
                        col,
                        1.5f * s);
            dl->AddLine(ImVec2(cx + 3.5f * s, cy + 2.8f * s),
                        ImVec2(cx + 9.5f * s, cy - 3.5f * s),
                        col,
                        1.5f * s);
            ImGui::Dummy(ImVec2(10.0f * s, lineH));
        } else {
            DrawCellAlignedText(cols[static_cast<std::size_t>(c)].first,
                                cols[static_cast<std::size_t>(c)].second,
                                ctx.palette.subLabelText);
        }
    }
}

void DrawFileDataRow(UiFrameContext& ctx,
                     DrawerUiState& ds,
                     const motrix::domain::DownloadTaskView& task,
                     std::size_t i) {
    const float s = ctx.dpiScale;
    const ElStyle elSt{ctx.palette, ctx.fonts, s, &ctx.anim};
    const auto& f = task.files[i];

    ImGui::TableNextRow();
    IdGuard rowId(static_cast<int>(i));

    ImGui::TableSetColumnIndex(0);
    const bool checked = (i < ds.draftSelected.size()) ? ds.draftSelected[i] : f.selected;
    if (DrawTriCheckboxCell(
            elSt, "##fchk", checked ? TriCheckState::Checked : TriCheckState::Unchecked, 24.0f * s)) {
        if (i < ds.draftSelected.size()) {
            ds.draftSelected[i] = !ds.draftSelected[i];
            ds.dirty = true;
            ds.fileFilter = 0;
        }
    }

    ImGui::TableSetColumnIndex(1);
    const std::string_view base = ExtractBaseName(f.path);
    DrawCellAlignedText(base, false, ctx.palette.tableText);
    if (ImGui::IsItemHovered()) {
        if (!f.path.empty()) {
            ElDrawTooltip({ctx.palette, ctx.fonts, s, &ctx.anim}, {f.path, ElTooltipPlacement::Top});
        }
        if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && i < ds.draftSelected.size()) {
            ds.draftSelected[i] = !ds.draftSelected[i];
            ds.dirty = true;
            ds.fileFilter = 0;
        }
    }

    ImGui::TableSetColumnIndex(2);
    const std::string ext = ExtractExtNoDot(f.path);
    DrawCellAlignedText(ext.empty() ? "-" : ext, false, ctx.palette.subLabelText);

    const bool singleUnknownFile = (task.files.size() == 1 && f.length == 0);
    const std::uint64_t doneBytes =
        (singleUnknownFile && f.completedLength == 0) ? task.completedLength : f.completedLength;
    const std::uint64_t totalBytes =
        (singleUnknownFile && task.totalLength > 0) ? task.totalLength : f.length;

    ImGui::TableSetColumnIndex(3);
    const std::string pctStr =
        (singleUnknownFile
         && (task.mediaProgress >= 0.0f || task.status == motrix::domain::TaskStatus::Complete))
            ? FormatPercentValue(static_cast<double>(motrix::domain::ProgressOf(task)) * 100.0)
            : FormatFilePercent(doneBytes, totalBytes);
    DrawCellAlignedText(pctStr, true, ctx.palette.subLabelText);

    ImGui::TableSetColumnIndex(4);
    const std::string doneStr = motrix::domain::FormatBytesFixed(doneBytes, 1, true);
    DrawCellAlignedText(doneStr, true, ctx.palette.subLabelText);

    ImGui::TableSetColumnIndex(5);
    const std::string totalStr = (totalBytes == 0 && doneBytes > 0)
                                     ? std::string("-")
                                     : motrix::domain::FormatBytesFixed(totalBytes, 1, true);
    DrawCellAlignedText(totalStr, true, ctx.palette.subLabelText);
}

}  // namespace

void RenderTrackersTab(UiFrameContext& ctx, std::span<const std::string> trackers) {
    const float s = ctx.dpiScale;
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    const ImVec2 boxMin = ImGui::GetCursorScreenPos();
    ImDrawList* dl = ImGui::GetWindowDrawList();

    if (trackers.empty()) {
        const float emptyH = 54.0f * s;
        dl->AddRectFilled(boxMin,
                          ImVec2(boxMin.x + avail.x, boxMin.y + emptyH),
                          ToAbgrU32(ctx.palette.inputBg),
                          4.0f * s);
        dl->AddRect(boxMin,
                    ImVec2(boxMin.x + avail.x, boxMin.y + emptyH),
                    ToAbgrU32(ctx.palette.inputBorder),
                    4.0f * s,
                    0,
                    1.0f);
        FontGuard ef(ctx.fonts, FontRole::Regular, 13.5f);
        const std::string_view emptyLbl = ctx.tr.Tr("detail.no_trackers");
        dl->AddText(ImVec2(boxMin.x + 14.0f * s, boxMin.y + (emptyH - ImGui::GetFontSize()) * 0.5f),
                    ToAbgrU32(ctx.palette.subLabelText),
                    emptyLbl.data(),
                    emptyLbl.data() + emptyLbl.size());
        return;
    }

    std::string joined;
    for (std::size_t i = 0; i < trackers.size(); ++i) {
        if (i > 0) {
            joined.push_back('\n');
        }
        joined += trackers[i];
    }
    std::vector<char> buf(joined.begin(), joined.end());
    buf.push_back('\0');

    FontGuard tf(ctx.fonts, FontRole::Regular, 13.5f);
    const float lineStep = ImGui::GetTextLineHeight() + 6.0f * s;
    const float boxH =
        std::clamp(static_cast<float>(trackers.size()) * lineStep + 18.0f * s, 54.0f * s, avail.y);

    dl->AddRectFilled(
        boxMin, ImVec2(boxMin.x + avail.x, boxMin.y + boxH), ToAbgrU32(ctx.palette.inputBg), 4.0f * s);
    dl->AddRect(boxMin,
                ImVec2(boxMin.x + avail.x, boxMin.y + boxH),
                ToAbgrU32(ctx.palette.inputBorder),
                4.0f * s,
                0,
                1.0f);

    StyleColorGuard bgCol(ImGuiCol_FrameBg, RgbaColor{0.0f, 0.0f, 0.0f, 0.0f});
    StyleColorGuard txtCol(ImGuiCol_Text, ctx.palette.panelTitle);
    StyleVarGuard noBorder(ImGuiStyleVar_FrameBorderSize, 0.0f);
    StyleVarGuard pad(ImGuiStyleVar_FramePadding, 14.0f * s, 10.0f * s);
    ImGui::InputTextMultiline(
        "##drw_trackers", buf.data(), buf.size(), ImVec2(avail.x, boxH), ImGuiInputTextFlags_ReadOnly);
}

void RenderPeersTab(UiFrameContext& ctx, std::span<const motrix::domain::TaskPeerView> peers) {
    const float s = ctx.dpiScale;
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    const std::size_t rowCount = peers.empty() ? 1u : peers.size();
    const ImVec2 tableMin = ImGui::GetCursorScreenPos();
    const float padY = 11.0f * s;
    float tableH = avail.y;

    {
        FontGuard pf(ctx.fonts, FontRole::Regular, 13.0f);
        const float exactRowH = ImGui::GetTextLineHeight() + padY * 2.0f + 1.0f;
        tableH = std::min(avail.y, static_cast<float>(rowCount + 1) * exactRowH);

        StyleColorGuard hdrBg(ImGuiCol_TableHeaderBg, ctx.palette.modalBg);
        StyleColorGuard rowBg(ImGuiCol_TableRowBg, ctx.palette.modalBg);
        StyleColorGuard altBg(ImGuiCol_TableRowBgAlt, ctx.palette.modalBg);
        StyleColorGuard bdrL(ImGuiCol_TableBorderLight, ctx.palette.borderSubtle);
        StyleColorGuard bdrS(ImGuiCol_TableBorderStrong, ctx.palette.borderSubtle);
        StyleVarGuard cellPad(ImGuiStyleVar_CellPadding, 12.0f * s, padY);

        constexpr ImGuiTableFlags kFlags = ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_ScrollY;
        TableGuard tbl("##peers_tbl", {5, kFlags, avail.x, tableH});
        if (tbl.IsOpen()) {
            ImGui::TableSetupColumn("host", ImGuiTableColumnFlags_WidthStretch, 1.45f);
            ImGui::TableSetupColumn("client", ImGuiTableColumnFlags_WidthStretch, 1.25f);
            ImGui::TableSetupColumn("pct", ImGuiTableColumnFlags_WidthFixed, 54.0f * s);
            ImGui::TableSetupColumn("up", ImGuiTableColumnFlags_WidthFixed, 86.0f * s);
            ImGui::TableSetupColumn("dl", ImGuiTableColumnFlags_WidthFixed, 86.0f * s);

            const std::string hostHdr = std::format("{}:", ctx.tr.Tr("detail.host"));
            const std::string clientHdr = std::format("{}:", ctx.tr.Tr("detail.client"));
            ImGui::TableNextRow(ImGuiTableRowFlags_Headers);
            ImGui::TableSetColumnIndex(0);
            DrawCellAlignedText(hostHdr, false, ctx.palette.subLabelText);
            ImGui::TableSetColumnIndex(1);
            DrawCellAlignedText(clientHdr, false, ctx.palette.subLabelText);
            ImGui::TableSetColumnIndex(2);
            DrawCellAlignedText(ctx.tr.Tr("detail.progress"), true, ctx.palette.subLabelText);
            ImGui::TableSetColumnIndex(3);
            DrawCellAlignedText("\xE2\x86\x91", true, ctx.palette.subLabelText);
            ImGui::TableSetColumnIndex(4);
            DrawCellAlignedText("\xE2\x86\x93", true, ctx.palette.subLabelText);

            if (peers.empty()) {
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                DrawCellAlignedText(ctx.tr.Tr("detail.no_peers"), false, ctx.palette.subLabelText);
            } else {
                for (const auto& peer : peers) {
                    ImGui::TableNextRow();
                    const std::string host = std::format("{}:{}", peer.ip, peer.port);
                    std::string decodedClient;
                    std::string_view clientStr = peer.client;
                    if (clientStr.empty() && !peer.peerId.empty()) {
                        decodedClient = motrix::domain::DecodePeerId(peer.peerId);
                        clientStr = decodedClient;
                    }
                    const std::string pctStr =
                        std::format("{:.0f}%", std::clamp(peer.progress * 100.0f, 0.0f, 100.0f));
                    const std::string upStr = motrix::domain::FormatSpeed(peer.uploadSpeed);
                    const std::string dlStr = motrix::domain::FormatSpeed(peer.downloadSpeed);

                    ImGui::TableSetColumnIndex(0);
                    DrawCellAlignedText(host, false, ctx.palette.tableText);
                    ImGui::TableSetColumnIndex(1);
                    DrawCellAlignedText(
                        clientStr.empty() ? "unknown" : clientStr, false, ctx.palette.tableText);
                    ImGui::TableSetColumnIndex(2);
                    DrawCellAlignedText(pctStr, true, ctx.palette.subLabelText);
                    ImGui::TableSetColumnIndex(3);
                    DrawCellAlignedText(upStr, true, ctx.palette.subLabelText);
                    ImGui::TableSetColumnIndex(4);
                    DrawCellAlignedText(dlStr, true, ctx.palette.subLabelText);
                }
            }
        }
    }
    DrawTableOuterFrame(ImGui::GetWindowDrawList(), tableMin, avail.x, tableH, ctx.palette.borderSubtle);
}

void RenderFilesTab(UiFrameContext& ctx,
                    DrawerUiState& ds,
                    const motrix::domain::DownloadTaskView& task) {
    SyncDrawerFilesDraft(ds, task);
    const float s = ctx.dpiScale;
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    const float bottomGap = 14.0f * s;
    const float bottomBarH = 30.0f * s;
    const float maxTableH = std::max(100.0f * s, avail.y - bottomGap - bottomBarH);
    const std::size_t rowCount = task.files.empty() ? 1u : task.files.size();
    const ImVec2 tableMin = ImGui::GetCursorScreenPos();
    const float selColW = 36.0f * s;
    const float padY = 10.0f * s;
    float tableH = maxTableH;

    {
        FontGuard ff(ctx.fonts, FontRole::Regular, 13.0f);
        const float exactRowH = std::max(14.0f * s, ImGui::GetTextLineHeight()) + padY * 2.0f + 1.0f;
        tableH = std::min(maxTableH, static_cast<float>(rowCount + 1) * exactRowH);

        StyleColorGuard hdrBg(ImGuiCol_TableHeaderBg, ctx.palette.modalBg);
        StyleColorGuard rowBg(ImGuiCol_TableRowBg, ctx.palette.modalBg);
        StyleColorGuard altBg(ImGuiCol_TableRowBgAlt, ctx.palette.modalBg);
        StyleColorGuard bdrL(ImGuiCol_TableBorderLight, ctx.palette.borderSubtle);
        StyleColorGuard bdrS(ImGuiCol_TableBorderStrong, ctx.palette.borderSubtle);
        StyleVarGuard cellPad(ImGuiStyleVar_CellPadding, 10.0f * s, padY);

        constexpr ImGuiTableFlags kFlags = ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_ScrollY;
        TableGuard tbl("##files_tbl", {6, kFlags, avail.x, tableH});
        if (tbl.IsOpen()) {
            ImGui::TableSetupColumn("sel", ImGuiTableColumnFlags_WidthFixed, selColW);
            ImGui::TableSetupColumn("name", ImGuiTableColumnFlags_WidthStretch, 2.5f);
            ImGui::TableSetupColumn("ext", ImGuiTableColumnFlags_WidthFixed, 68.0f * s);
            ImGui::TableSetupColumn("pct", ImGuiTableColumnFlags_WidthFixed, 56.0f * s);
            ImGui::TableSetupColumn("done", ImGuiTableColumnFlags_WidthFixed, 78.0f * s);
            ImGui::TableSetupColumn("size", ImGuiTableColumnFlags_WidthFixed, 78.0f * s);

            DrawFilesHeaderRow(ctx, ds, task, selColW);
            for (std::size_t i = 0; i < task.files.size(); ++i) {
                DrawFileDataRow(ctx, ds, task, i);
            }
        }
    }

    DrawTableOuterFrame(ImGui::GetWindowDrawList(), tableMin, avail.x, tableH, ctx.palette.borderSubtle);
    ImGui::SetCursorScreenPos(ImVec2(tableMin.x, tableMin.y + tableH + bottomGap));
    RenderFilesMediaGroupAndSummary(ctx, ds, task, avail.x);
}

}  // namespace motrix::ui

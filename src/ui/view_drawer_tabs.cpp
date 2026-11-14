module;

#include "imgui.h"

module motrix.ui;

import std;
import motrix.domain;
import motrix.app;
import :views_internal;
import :theme;
import :scope_guard;

namespace motrix::ui {
namespace {

bool MatchesFileFilter(std::string_view path, int filter) noexcept {
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

void SyncDrawerFileDraft(DrawerUiState& ds, const motrix::domain::DownloadTaskView& task) {
    const bool gidChanged = (ds.loadedGid != task.gid);
    if (gidChanged || (!ds.dirty && ds.draftSelected.size() != task.files.size())) {
        ds.loadedGid = task.gid;
        ds.dirty = false;
        ds.fileFilter = 0;
        ds.draftSelected.resize(task.files.size());
        for (std::size_t i = 0; i < task.files.size(); ++i) {
            ds.draftSelected[i] = task.files[i].selected;
        }
    } else if (!ds.dirty) {
        for (std::size_t i = 0; i < task.files.size(); ++i) {
            ds.draftSelected[i] = task.files[i].selected;
        }
    }
}

void ApplyDrawerFileFilter(DrawerUiState& ds,
                           const motrix::domain::DownloadTaskView& task,
                           int filterIdx,
                           std::size_t selectedCount) {
    if (filterIdx == 0) {
        const bool selectAll = (selectedCount < task.files.size());
        std::fill(ds.draftSelected.begin(), ds.draftSelected.end(), selectAll);
        ds.fileFilter = 0;
        ds.dirty = true;
        return;
    }
    ds.fileFilter = (ds.fileFilter == filterIdx) ? 0 : filterIdx;
    if (ds.fileFilter == 0) {
        return;
    }
    for (std::size_t i = 0; i < task.files.size(); ++i) {
        ds.draftSelected[i] = MatchesFileFilter(task.files[i].path, ds.fileFilter);
    }
    ds.dirty = true;
}

void RenderFilesFilterControls(UiFrameContext& ctx,
                               DrawerUiState& ds,
                               const motrix::domain::DownloadTaskView& task,
                               std::size_t selectedCount) {
    const float s = ctx.dpiScale;
    const float filterBtnW = 58.0f * s;
    const float rightStart = ImGui::GetWindowWidth() - 24.0f * s - filterBtnW * 4.0f - 18.0f * s;
    if (rightStart > ImGui::GetCursorPosX()) {
        ImGui::SameLine(rightStart);
    }

    const std::array<std::pair<std::string_view, int>, 4> filters{{
        {ctx.tr.Tr("detail.all"), 0},
        {ctx.tr.Tr("detail.video"), 1},
        {ctx.tr.Tr("detail.audio"), 2},
        {ctx.tr.Tr("detail.image"), 3},
    }};
    for (std::size_t k = 0; k < filters.size(); ++k) {
        if (k > 0) {
            ImGui::SameLine();
        }
        const bool active = (ds.fileFilter == filters[k].second);
        StyleColorGuard bg(ImGuiCol_Button, active ? ctx.palette.primary : ctx.palette.subnavBg);
        StyleColorGuard fgCol(ImGuiCol_Text, active ? HexColor(0xFFFFFF) : ctx.palette.panelTitle);
        FontGuard fnt(ctx.fonts, FontRole::Regular, 12.0f);
        if (ImGui::Button(filters[k].first.data(), ImVec2(filterBtnW, 24.0f * s))) {
            ApplyDrawerFileFilter(ds, task, filters[k].second, selectedCount);
        }
    }
}

void RenderFilesTable(UiFrameContext& ctx,
                      DrawerUiState& ds,
                      const motrix::domain::DownloadTaskView& task) {
    const float s = ctx.dpiScale;
    constexpr ImGuiTableFlags kFlags = ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH
                                       | ImGuiTableFlags_ScrollY | ImGuiTableFlags_Resizable;
    const float tableH = std::max(160.0f * s, ImGui::GetContentRegionAvail().y - 4.0f * s);
    FontGuard fg(ctx.fonts, FontRole::Regular, 13.0f);
    TableGuard tbl("##detail_files_tbl", {5, kFlags, -1.0f, tableH});
    if (!tbl.IsOpen()) {
        return;
    }
    ImGui::TableSetupColumn("##sel", ImGuiTableColumnFlags_WidthFixed, 30.0f * s);
    ImGui::TableSetupColumn(
        ctx.tr.Tr("detail.file_name").data(), ImGuiTableColumnFlags_WidthStretch, 2.2f);
    ImGui::TableSetupColumn(
        ctx.tr.Tr("detail.file_ext").data(), ImGuiTableColumnFlags_WidthFixed, 56.0f * s);
    ImGui::TableSetupColumn("%", ImGuiTableColumnFlags_WidthFixed, 56.0f * s);
    ImGui::TableSetupColumn(
        ctx.tr.Tr("detail.file_size").data(), ImGuiTableColumnFlags_WidthFixed, 82.0f * s);
    ImGui::TableHeadersRow();

    for (std::size_t i = 0; i < task.files.size(); ++i) {
        const auto& f = task.files[i];
        if (ds.fileFilter != 0 && !MatchesFileFilter(f.path, ds.fileFilter)) {
            continue;
        }
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        IdGuard rowId(static_cast<int>(i));
        bool checked = (i < ds.draftSelected.size()) ? ds.draftSelected[i] : f.selected;
        if (ImGui::Checkbox("##chk", &checked) && i < ds.draftSelected.size()) {
            ds.draftSelected[i] = checked;
            ds.dirty = true;
        }

        ImGui::TableNextColumn();
        const std::size_t slash = f.path.find_last_of("/\\");
        const std::string_view base = (slash == std::string::npos)
                                          ? std::string_view(f.path)
                                          : std::string_view(f.path).substr(slash + 1);
        ImGui::TextUnformatted(base.data(), base.data() + base.size());

        ImGui::TableNextColumn();
        const std::string ext = motrix::domain::ExtractFileExtension(f.path);
        ImGui::TextUnformatted(ext.empty() ? "-" : ext.c_str());

        ImGui::TableNextColumn();
        const float pct = (f.length > 0) ? static_cast<float>(f.completedLength) * 100.0f
                                               / static_cast<float>(f.length)
                                         : 0.0f;
        ImGui::Text("%.0f%%", std::clamp(pct, 0.0f, 100.0f));

        ImGui::TableNextColumn();
        const std::string lenStr = motrix::domain::FormatBytes(f.length);
        ImGui::TextUnformatted(lenStr.c_str());
    }
}

}  // namespace

void RenderTrackersTab(UiFrameContext& ctx, std::span<const std::string> trackers) {
    const float s = ctx.dpiScale;
    if (trackers.empty()) {
        const std::string_view msg = ctx.tr.Tr("detail.no_trackers");
        FontGuard fg(ctx.fonts, FontRole::Regular, 14.0f);
        StyleColorGuard cg(ImGuiCol_Text, ctx.palette.taskMetaText);
        ImGui::TextUnformatted(msg.data(), msg.data() + msg.size());
        return;
    }

    std::string joined;
    for (std::size_t i = 0; i < trackers.size(); ++i) {
        if (i > 0) {
            joined.push_back('\n');
        }
        joined += trackers[i];
    }

    const float availH = std::max(140.0f * s, ImGui::GetContentRegionAvail().y - 8.0f * s);
    FontGuard fg(ctx.fonts, FontRole::Mono, 13.0f);
    ImGui::InputTextMultiline("##detail_trackers",
                              joined.data(),
                              joined.size() + 1,
                              ImVec2(-1.0f, availH),
                              ImGuiInputTextFlags_ReadOnly);
}

void RenderPeersTab(UiFrameContext& ctx, std::span<const motrix::domain::TaskPeerView> peers) {
    const float s = ctx.dpiScale;
    if (peers.empty()) {
        const std::string_view msg = ctx.tr.Tr("detail.no_peers");
        FontGuard fg(ctx.fonts, FontRole::Regular, 14.0f);
        StyleColorGuard cg(ImGuiCol_Text, ctx.palette.taskMetaText);
        ImGui::TextUnformatted(msg.data(), msg.data() + msg.size());
        return;
    }

    constexpr ImGuiTableFlags kFlags = ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH
                                       | ImGuiTableFlags_ScrollY | ImGuiTableFlags_Resizable;
    const float tableH = std::max(160.0f * s, ImGui::GetContentRegionAvail().y - 4.0f * s);
    FontGuard fg(ctx.fonts, FontRole::Regular, 13.0f);
    TableGuard tbl("##detail_peers_tbl", {5, kFlags, -1.0f, tableH});
    if (!tbl.IsOpen()) {
        return;
    }
    ImGui::TableSetupColumn(ctx.tr.Tr("detail.host").data(), ImGuiTableColumnFlags_WidthStretch, 1.4f);
    ImGui::TableSetupColumn(ctx.tr.Tr("detail.client").data(), ImGuiTableColumnFlags_WidthStretch, 1.3f);
    ImGui::TableSetupColumn("%", ImGuiTableColumnFlags_WidthFixed, 54.0f * s);
    ImGui::TableSetupColumn("↑", ImGuiTableColumnFlags_WidthFixed, 86.0f * s);
    ImGui::TableSetupColumn("↓", ImGuiTableColumnFlags_WidthFixed, 86.0f * s);
    ImGui::TableHeadersRow();

    for (const auto& peer : peers) {
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        const std::string host = std::format("{}:{}", peer.ip, peer.port);
        ImGui::TextUnformatted(host.c_str());

        ImGui::TableNextColumn();
        ImGui::TextUnformatted(peer.client.c_str());

        ImGui::TableNextColumn();
        ImGui::Text("%.0f%%", peer.progress * 100.0f);

        ImGui::TableNextColumn();
        const std::string up = motrix::domain::FormatSpeed(peer.uploadSpeed);
        ImGui::TextUnformatted(up.c_str());

        ImGui::TableNextColumn();
        const std::string dn = motrix::domain::FormatSpeed(peer.downloadSpeed);
        ImGui::TextUnformatted(dn.c_str());
    }
}

void RenderFilesTab(UiFrameContext& ctx,
                    DrawerUiState& ds,
                    const motrix::domain::DownloadTaskView& task) {
    const float s = ctx.dpiScale;
    SyncDrawerFileDraft(ds, task);

    std::size_t selectedCount = 0;
    std::uint64_t selectedBytes = 0;
    std::uint64_t totalBytes = 0;
    for (std::size_t i = 0; i < task.files.size(); ++i) {
        totalBytes += task.files[i].length;
        if (i < ds.draftSelected.size() && ds.draftSelected[i]) {
            ++selectedCount;
            selectedBytes += task.files[i].length;
        }
    }

    {
        FontGuard fg(ctx.fonts, FontRole::Regular, 13.0f);
        StyleColorGuard cg(ImGuiCol_Text, ctx.palette.taskMetaText);
        const std::string summary = std::format("{}: {}/{} ({} / {})",
                                                ctx.tr.Tr("detail.selected_summary"),
                                                selectedCount,
                                                task.files.size(),
                                                motrix::domain::FormatBytes(selectedBytes),
                                                motrix::domain::FormatBytes(totalBytes));
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(summary.c_str());
    }

    RenderFilesFilterControls(ctx, ds, task, selectedCount);
    ImGui::Dummy(ImVec2(0.0f, 6.0f * s));
    RenderFilesTable(ctx, ds, task);
}

}  // namespace motrix::ui

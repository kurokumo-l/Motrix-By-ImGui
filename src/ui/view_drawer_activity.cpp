module;

#include "imgui.h"

module motrix.ui;

import std;
import motrix.domain;
import motrix.app;
import :views_internal;
import :theme;
import :icons;
import :scope_guard;

namespace motrix::ui {
namespace {

[[nodiscard]] RgbaColor StatusBadgeColor(const UiFrameContext& ctx,
                                         motrix::domain::TaskStatus status) noexcept {
    using S = motrix::domain::TaskStatus;
    switch (status) {
        case S::Active: return ctx.palette.primary;
        case S::Waiting: return HexColor(0x909399);
        case S::Paused: return HexColor(0xE6A23C);
        case S::Complete: return HexColor(0x67C23A);
        case S::Error:
        case S::Removed: return ctx.palette.progressError;
    }
    return ctx.palette.taskMetaText;
}

[[nodiscard]] std::string_view StatusLabelKey(motrix::domain::TaskStatus status) noexcept {
    using S = motrix::domain::TaskStatus;
    switch (status) {
        case S::Active: return "status.active";
        case S::Waiting: return "status.waiting";
        case S::Paused: return "status.paused";
        case S::Complete: return "status.complete";
        case S::Error: return "status.error";
        case S::Removed: return "status.removed";
    }
    return "status.waiting";
}

struct DetailRowSpec {
    std::string_view label{};
    std::string_view value{};
    float labelW = 128.0f;
    std::optional<RgbaColor> valColor{};
    std::optional<MotrixIcon> actionIcon{};
    const char* actionId = nullptr;
    std::function<void()> onAction{};
};

void DrawFormRow(UiFrameContext& ctx, const DetailRowSpec& row) {
    const float s = ctx.dpiScale;
    const float startX = ImGui::GetCursorPosX();
    {
        FontGuard fg(ctx.fonts, FontRole::Regular, 13.0f);
        StyleColorGuard cg(ImGuiCol_Text, ctx.palette.taskMetaText);
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(row.label.data(), row.label.data() + row.label.size());
    }
    ImGui::SameLine(startX + row.labelW);
    {
        FontGuard fg(ctx.fonts, FontRole::Regular, 13.5f);
        StyleColorGuard cg(ImGuiCol_Text, row.valColor.value_or(ctx.palette.panelTitle));
        const float wrapW = std::max(
            80.0f * s, ImGui::GetContentRegionAvail().x - (row.actionIcon ? 32.0f * s : 4.0f * s));
        TextWrapGuard wrap(ImGui::GetCursorPosX() + wrapW);
        ImGui::TextUnformatted(row.value.data(), row.value.data() + row.value.size());
    }
    if (row.actionIcon && row.actionId != nullptr) {
        ImGui::SameLine();
        const ImVec2 p = ImGui::GetCursorScreenPos();
        const float btnSz = 22.0f * s;
        const float iconSz = 13.0f * s;
        if (ImGui::InvisibleButton(row.actionId, ImVec2(btnSz, btnSz)) && row.onAction) {
            row.onAction();
        }
        const bool hov = ImGui::IsItemHovered();
        DrawMotrixIcon(IconSpec{
            *row.actionIcon,
            p.x + (btnSz - iconSz) * 0.5f,
            p.y + (btnSz - iconSz) * 0.5f,
            iconSz,
            iconSz,
            hov ? ctx.palette.primary : ctx.palette.pillIcon,
            s,
        });
    }
    ImGui::Dummy(ImVec2(0.0f, 4.0f * s));
}

int HexNibbleVal(char ch) noexcept {
    if (ch >= '0' && ch <= '9') {
        return ch - '0';
    }
    if (ch >= 'a' && ch <= 'f') {
        return 10 + (ch - 'a');
    }
    if (ch >= 'A' && ch <= 'F') {
        return 10 + (ch - 'A');
    }
    return 0;
}

RgbaColor BucketColor(const MotrixPalette& p, int nibble) noexcept {
    if (nibble <= 0) {
        return p.graphicS0;
    }
    if (nibble <= 4) {
        return p.graphicS1;
    }
    if (nibble <= 9) {
        return p.graphicS2;
    }
    if (nibble <= 14) {
        return p.graphicS3;
    }
    return p.graphicS4;
}

void RenderTaskGraphicHeatmap(UiFrameContext& ctx,
                              std::string_view bitfieldHex,
                              std::uint32_t numPieces) {
    const float s = ctx.dpiScale;
    const float availW = ImGui::GetContentRegionAvail().x;
    const float pad = 12.0f * s;
    const float atom = 10.0f * s;
    const float gutter = 3.0f * s;
    const float step = atom + gutter;
    const float radius = 2.0f * s;

    const int cols = std::max(1, static_cast<int>((availW - pad * 2.0f + gutter) / step));
    const std::size_t rawCount =
        !bitfieldHex.empty() ? bitfieldHex.size()
                             : std::clamp<std::size_t>(numPieces > 0 ? (numPieces + 3) / 4 : 36, 24, 96);
    constexpr std::size_t kMaxCells = 360;
    const std::size_t cellCount = std::min(rawCount, kMaxCells);
    const int rows = static_cast<int>((cellCount + static_cast<std::size_t>(cols) - 1)
                                      / static_cast<std::size_t>(cols));
    const float boxH = pad * 2.0f + static_cast<float>(rows) * atom
                       + static_cast<float>(std::max(0, rows - 1)) * gutter;

    const ImVec2 min = ImGui::GetCursorScreenPos();
    const ImVec2 max(min.x + availW, min.y + boxH);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(min, max, ToAbgrU32(ctx.palette.graphicBoxBg), 4.0f * s);
    dl->AddRect(min, max, ToAbgrU32(ctx.palette.borderSubtle), 4.0f * s, 0, 1.0f);

    const ImU32 outlineCol = ToAbgrU32(ctx.palette.graphicAtomOutline);
    for (std::size_t i = 0; i < cellCount; ++i) {
        const int r = static_cast<int>(i / static_cast<std::size_t>(cols));
        const int c = static_cast<int>(i % static_cast<std::size_t>(cols));
        const float ax = min.x + pad + static_cast<float>(c) * step;
        const float ay = min.y + pad + static_cast<float>(r) * step;

        int nib = 0;
        if (!bitfieldHex.empty()) {
            const std::size_t srcIdx = (i * bitfieldHex.size()) / cellCount;
            nib = HexNibbleVal(bitfieldHex[std::min(srcIdx, bitfieldHex.size() - 1)]);
        }
        const RgbaColor fill = BucketColor(ctx.palette, nib);
        dl->AddRectFilled(ImVec2(ax, ay), ImVec2(ax + atom, ay + atom), ToAbgrU32(fill), radius);
        dl->AddRect(ImVec2(ax, ay), ImVec2(ax + atom, ay + atom), outlineCol, radius, 0, 1.0f);
    }

    ImGui::Dummy(ImVec2(availW, boxH + 12.0f * s));
}

}  // namespace

void RenderGeneralBtMetaRows(UiFrameContext& ctx,
                             const motrix::domain::DownloadTaskView& task,
                             float labelW) {
    if (task.taskType != motrix::domain::TaskType::BitTorrent && !task.btInfo.has_value()) {
        if (!task.uri.empty()) {
            DrawFormRow(ctx,
                        DetailRowSpec{
                            "URL",
                            task.uri,
                            labelW,
                            std::nullopt,
                            MotrixIcon::Link,
                            "##detail_copy_uri",
                            [&]() { ctx.sink.Dispatch(motrix::app::CopyTaskLinkCmd{task.gid}); },
                        });
        }
        return;
    }

    if (task.btInfo && !task.btInfo->infoHash.empty()) {
        DrawFormRow(ctx,
                    DetailRowSpec{
                        ctx.tr.Tr("detail.info_hash"),
                        task.btInfo->infoHash,
                        labelW,
                        std::nullopt,
                        MotrixIcon::Link,
                        "##detail_copy_hash",
                        [&]() { ctx.sink.Dispatch(motrix::app::CopyTaskLinkCmd{task.gid}); },
                    });
    }
    if (task.pieceLength > 0) {
        DrawFormRow(ctx,
                    DetailRowSpec{ctx.tr.Tr("detail.piece_length"),
                                  motrix::domain::FormatBytes(task.pieceLength),
                                  labelW});
    }
    if (task.numPieces > 0) {
        DrawFormRow(
            ctx, DetailRowSpec{ctx.tr.Tr("detail.num_pieces"), std::to_string(task.numPieces), labelW});
    }
    if (task.btInfo && !task.btInfo->creationDate.empty() && task.btInfo->creationDate != "0") {
        DrawFormRow(ctx,
                    DetailRowSpec{ctx.tr.Tr("detail.creation_date"), task.btInfo->creationDate, labelW});
    }
    if (task.btInfo && !task.btInfo->comment.empty()) {
        DrawFormRow(ctx, DetailRowSpec{ctx.tr.Tr("detail.comment"), task.btInfo->comment, labelW});
    }
}

void RenderGeneralTab(UiFrameContext& ctx, const motrix::domain::DownloadTaskView& task) {
    const float labelW = 128.0f * ctx.dpiScale;

    DrawFormRow(ctx, DetailRowSpec{ctx.tr.Tr("detail.name"), task.name, labelW});
    DrawFormRow(ctx,
                DetailRowSpec{
                    ctx.tr.Tr("detail.dir"),
                    task.dir,
                    labelW,
                    std::nullopt,
                    MotrixIcon::Folder,
                    "##detail_open_dir",
                    [&]() { ctx.sink.Dispatch(motrix::app::ShowInFolderCmd{task.gid}); },
                });

    const RgbaColor stColor = StatusBadgeColor(ctx, task.status);
    DrawFormRow(
        ctx,
        DetailRowSpec{
            ctx.tr.Tr("detail.status"), ctx.tr.Tr(StatusLabelKey(task.status)), labelW, stColor});

    if (task.errorCode != 0) {
        DrawFormRow(ctx,
                    DetailRowSpec{ctx.tr.Tr("detail.error_code"),
                                  std::to_string(task.errorCode),
                                  labelW,
                                  ctx.palette.progressError});
    }
    if (!task.errorMessage.empty()) {
        DrawFormRow(
            ctx,
            DetailRowSpec{
                ctx.tr.Tr("detail.error_msg"), task.errorMessage, labelW, ctx.palette.progressError});
    }

    RenderGeneralBtMetaRows(ctx, task, labelW);
}

void RenderActivityTab(UiFrameContext& ctx, const motrix::domain::DownloadTaskView& task) {
    const float s = ctx.dpiScale;
    const float labelW = 128.0f * s;

    RenderTaskGraphicHeatmap(ctx, task.bitfieldHex, task.numPieces);

    const float progress = motrix::domain::ProgressOf(task);
    const float barW = std::max(100.0f * s, ImGui::GetContentRegionAvail().x - 56.0f * s);
    const ImVec2 bMin = ImGui::GetCursorScreenPos();
    const float barH = 6.0f * s;
    const float barY = bMin.y + 6.0f * s;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(ImVec2(bMin.x, barY),
                      ImVec2(bMin.x + barW, barY + barH),
                      ToAbgrU32(ctx.palette.progressTrack),
                      3.0f * s);
    if (progress > 0.0f) {
        const RgbaColor fill = (task.status == motrix::domain::TaskStatus::Complete)
                                   ? HexColor(0x67C23A)
                                   : ctx.palette.primary;
        dl->AddRectFilled(ImVec2(bMin.x, barY),
                          ImVec2(bMin.x + barW * std::clamp(progress, 0.0f, 1.0f), barY + barH),
                          ToAbgrU32(fill),
                          3.0f * s);
    }
    ImGui::SetCursorScreenPos(ImVec2(bMin.x + barW + 10.0f * s, bMin.y));
    {
        FontGuard fg(ctx.fonts, FontRole::Medium, 13.0f);
        StyleColorGuard cg(ImGuiCol_Text, ctx.palette.taskMetaText);
        ImGui::Text("%.1f%%", progress * 100.0f);
    }
    ImGui::Dummy(ImVec2(0.0f, 10.0f * s));

    const std::string compText = std::format("{} / {}",
                                             motrix::domain::FormatBytes(task.completedLength),
                                             motrix::domain::FormatBytes(task.totalLength));
    DrawFormRow(ctx, DetailRowSpec{ctx.tr.Tr("detail.completed"), compText, labelW});
    DrawFormRow(ctx,
                DetailRowSpec{ctx.tr.Tr("detail.dl_speed"),
                              motrix::domain::FormatSpeed(task.downloadSpeed),
                              labelW});
    DrawFormRow(
        ctx, DetailRowSpec{ctx.tr.Tr("detail.connections"), std::to_string(task.connections), labelW});

    if (task.taskType == motrix::domain::TaskType::BitTorrent) {
        DrawFormRow(ctx,
                    DetailRowSpec{ctx.tr.Tr("detail.seeders"), std::to_string(task.numSeeders), labelW});
        DrawFormRow(ctx,
                    DetailRowSpec{ctx.tr.Tr("detail.ul_speed"),
                                  motrix::domain::FormatSpeed(task.uploadSpeed),
                                  labelW});
        DrawFormRow(ctx,
                    DetailRowSpec{ctx.tr.Tr("detail.uploaded"),
                                  motrix::domain::FormatBytes(task.uploadLength),
                                  labelW});
        const double ratio = (task.completedLength > 0) ? static_cast<double>(task.uploadLength)
                                                              / static_cast<double>(task.completedLength)
                                                        : 0.0;
        DrawFormRow(ctx, DetailRowSpec{ctx.tr.Tr("detail.ratio"), std::format("{:.2f}", ratio), labelW});
    }
}

}  // namespace motrix::ui

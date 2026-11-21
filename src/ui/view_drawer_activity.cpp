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

constexpr float kDetailLabelW = 104.0f;
constexpr float kDetailColGap = 14.0f;
constexpr float kDetailRowGap = 12.0f;

struct StatusTagVisual {
    const char* label = "WAITING";
    RgbaColor color{};
};

struct DetailRowSpec {
    std::string_view label;
    std::string_view value;
    float labelW = 104.0f;
    RgbaColor valueColor{};
    bool customColor = false;
    bool wrapValue = false;
};

[[nodiscard]] StatusTagVisual StatusTagFor(const UiFrameContext& ctx,
                                           const motrix::domain::DownloadTaskView& task) noexcept {
    using motrix::domain::TaskStatus;
    const MotrixPalette& p = ctx.palette;
    switch (task.status) {
        case TaskStatus::Active:
            if (task.isSeeding) {
                return {"SEEDING", p.progressComplete};
            }
            return {"ACTIVE", p.primary};
        case TaskStatus::Waiting: return {"WAITING", p.subLabelText};
        case TaskStatus::Paused: return {"PAUSED", p.subLabelText};
        case TaskStatus::Complete: return {"COMPLETE", p.progressComplete};
        case TaskStatus::Error: return {"ERROR", p.progressError};
        case TaskStatus::Removed: return {"REMOVED", p.subLabelText};
    }
    return {"WAITING", p.subLabelText};
}

[[nodiscard]] std::string TrimTrailingZeros(std::string text) {
    const auto dotPos = text.find('.');
    if (dotPos == std::string::npos) {
        return text;
    }
    while (!text.empty() && text.back() == '0') {
        text.pop_back();
    }
    if (!text.empty() && text.back() == '.') {
        text.pop_back();
    }
    return text;
}

[[nodiscard]] std::string FormatTrimmedPercent(double fraction) {
    const double pct = std::clamp(fraction * 100.0, 0.0, 100.0);
    return TrimTrailingZeros(std::format("{:.2f}", pct)) + "%";
}

[[nodiscard]] std::string FormatTrimmedRatio(std::uint64_t uploaded, std::uint64_t total) {
    if (total == 0 || uploaded == 0) {
        return "0";
    }
    const double ratio = static_cast<double>(uploaded) / static_cast<double>(total);
    return TrimTrailingZeros(std::format("{:.4f}", ratio));
}

struct ColonLabelSpec {
    ImVec2 pos{};
    float labelW = 104.0f;
    float rowH = 24.0f;
    std::string_view label;
};

void DrawColonLabel(const UiFrameContext& ctx, const ColonLabelSpec& spec) {
    if (spec.label.empty()) {
        return;
    }
    const std::string text = std::format("{}:", spec.label);
    FontGuard lf(ctx.fonts, FontRole::Regular, 13.5f);
    const ImVec2 sz = ImGui::CalcTextSize(text.data(), text.data() + text.size());
    const float lx = spec.pos.x + std::max(0.0f, spec.labelW - sz.x);
    const float ly = spec.pos.y + (spec.rowH - sz.y) * 0.5f;
    ImGui::GetWindowDrawList()->AddText(
        ImVec2(lx, ly), ToAbgrU32(ctx.palette.subLabelText), text.data(), text.data() + text.size());
}

void DrawDetailValueRow(UiFrameContext& ctx, const DetailRowSpec& row) {
    const float s = ctx.dpiScale;
    const float labelW = row.labelW * s;
    const float gap = kDetailColGap * s;
    const float rowH = 24.0f * s;
    const float availW = ImGui::GetContentRegionAvail().x;
    const float valW = std::max(80.0f * s, availW - labelW - gap);
    const ImVec2 origin = ImGui::GetCursorScreenPos();

    DrawColonLabel(ctx, {origin, labelW, rowH, row.label});

    FontGuard vf(ctx.fonts, FontRole::Regular, 13.5f);
    const float textY = origin.y + (rowH - ImGui::GetFontSize()) * 0.5f;
    const RgbaColor valCol = row.customColor ? row.valueColor : ctx.palette.panelTitle;
    float consumedH = rowH;
    if (row.wrapValue) {
        StyleColorGuard tc(ImGuiCol_Text, valCol);
        TextWrapGuard wrap(origin.x - ImGui::GetWindowPos().x + labelW + gap + valW);
        ImGui::SetCursorScreenPos(ImVec2(origin.x + labelW + gap, textY));
        ImGui::TextUnformatted(row.value.data(), row.value.data() + row.value.size());
        consumedH = std::max(rowH, ImGui::GetItemRectMax().y - origin.y);
    } else {
        ImGui::GetWindowDrawList()->AddText(ImVec2(origin.x + labelW + gap, textY),
                                            ToAbgrU32(valCol),
                                            row.value.data(),
                                            row.value.data() + row.value.size());
    }
    ImGui::SetCursorScreenPos(ImVec2(origin.x, origin.y + consumedH + 8.0f * s));
    ImGui::Dummy(ImVec2(0.0f, 0.0f));
}

void DrawStatusTagRow(UiFrameContext& ctx, const motrix::domain::DownloadTaskView& task) {
    const float s = ctx.dpiScale;
    const float labelW = kDetailLabelW * s;
    const float gap = kDetailColGap * s;
    const float rowH = 26.0f * s;
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    DrawColonLabel(ctx, {origin, labelW, rowH, ctx.tr.Tr("detail.status")});

    const StatusTagVisual tag = StatusTagFor(ctx, task);
    FontGuard tf(ctx.fonts, FontRole::Regular, 11.5f);
    const ImVec2 txtSz = ImGui::CalcTextSize(tag.label);
    const float padX = 9.0f * s;
    const float tagW = txtSz.x + padX * 2.0f;
    const float tagH = 24.0f * s;
    const ImVec2 tMin(origin.x + labelW + gap, origin.y + (rowH - tagH) * 0.5f);
    const ImVec2 tMax(tMin.x + tagW, tMin.y + tagH);

    const float fillA = ctx.palette.isDark ? 0.14f : 0.10f;
    const float borderA = ctx.palette.isDark ? 0.36f : 0.28f;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(tMin, tMax, ToAbgrU32(WithAlpha(tag.color, fillA)), 4.0f * s);
    dl->AddRect(tMin, tMax, ToAbgrU32(WithAlpha(tag.color, borderA)), 4.0f * s, 0, 1.0f);
    dl->AddText(
        ImVec2(tMin.x + padX, tMin.y + (tagH - txtSz.y) * 0.5f), ToAbgrU32(tag.color), tag.label);

    ImGui::SetCursorScreenPos(ImVec2(origin.x, origin.y + rowH + 8.0f * s));
    ImGui::Dummy(ImVec2(0.0f, 0.0f));
}

void DrawDirPathRow(UiFrameContext& ctx, const motrix::domain::DownloadTaskView& task) {
    const float s = ctx.dpiScale;
    const float labelW = kDetailLabelW * s;
    const float gap = kDetailColGap * s;
    const float rowH = 32.0f * s;
    const float availW = ImGui::GetContentRegionAvail().x;
    const float fieldW = std::max(120.0f * s, availW - labelW - gap);
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    DrawColonLabel(ctx, {origin, labelW, rowH, ctx.tr.Tr("detail.dir")});

    const std::string fullPath = motrix::domain::ResolveTaskFullPath(task);
    std::array<char, 1024> dirBuf{};
    CopyUtf8ToBuffer(fullPath, dirBuf.data(), dirBuf.size());

    const ElStyle elSt{ctx.palette, ctx.fonts, s, &ctx.anim};
    ImGui::SetCursorScreenPos(ImVec2(origin.x + labelW + gap, origin.y));
    const ElCompositeResult res = ElCompositeInput(elSt,
                                                   ElCompositeSpec{
                                                       "##drw_save_path",
                                                       dirBuf.data(),
                                                       dirBuf.size(),
                                                       fieldW,
                                                       std::nullopt,
                                                       MotrixIcon::Folder,
                                                       {},
                                                       true,
                                                       false,
                                                       false,
                                                   });
    if (res.rightClicked) {
        ctx.sink.Dispatch(motrix::app::ShowInFolderCmd{task.gid});
    }

    ImGui::SetCursorScreenPos(ImVec2(origin.x, origin.y + rowH + kDetailRowGap * s));
    ImGui::Dummy(ImVec2(0.0f, 0.0f));
}

void DrawBtInfoDivider(const UiFrameContext& ctx) {
    const float s = ctx.dpiScale;
    const float availW = ImGui::GetContentRegionAvail().x;
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const float divH = 34.0f * s;
    const float lineY = origin.y + divH * 0.5f;
    const std::string_view btLbl = ctx.tr.Tr("detail.bt_info");

    FontGuard bf(ctx.fonts, FontRole::Regular, 13.0f);
    const ImVec2 txtSz = ImGui::CalcTextSize(btLbl.data(), btLbl.data() + btLbl.size());
    const float iconSz = 13.0f * s;
    const float iconGap = 6.0f * s;
    const float padX = 12.0f * s;
    const float contentW = iconSz + iconGap + txtSz.x;
    const float cx = origin.x + availW * 0.5f;
    const float gapL = cx - contentW * 0.5f - padX;
    const float gapR = cx + contentW * 0.5f + padX;

    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImU32 lineCol = ToAbgrU32(ctx.palette.borderSubtle);
    if (gapL > origin.x) {
        dl->AddLine(ImVec2(origin.x, lineY), ImVec2(gapL, lineY), lineCol, 1.0f);
    }
    if (gapR < origin.x + availW) {
        dl->AddLine(ImVec2(gapR, lineY), ImVec2(origin.x + availW, lineY), lineCol, 1.0f);
    }

    const float contentX = cx - contentW * 0.5f;
    DrawMotrixIcon(IconSpec{
        MotrixIcon::Magnet,
        contentX,
        lineY - iconSz * 0.5f,
        iconSz,
        iconSz,
        ctx.palette.panelTitle,
        s,
    });
    dl->AddText(ImVec2(contentX + iconSz + iconGap, lineY - txtSz.y * 0.5f),
                ToAbgrU32(ctx.palette.panelTitle),
                btLbl.data(),
                btLbl.data() + btLbl.size());

    ImGui::SetCursorScreenPos(ImVec2(origin.x, origin.y + divH + 6.0f * s));
    ImGui::Dummy(ImVec2(0.0f, 0.0f));
}

void DrawInfoHashRow(UiFrameContext& ctx, std::string_view hash) {
    const float s = ctx.dpiScale;
    const float labelW = kDetailLabelW * s;
    const float gap = kDetailColGap * s;
    const float rowH = 24.0f * s;
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    DrawColonLabel(ctx, {origin, labelW, rowH, ctx.tr.Tr("detail.info_hash")});

    FontGuard hf(ctx.fonts, FontRole::Regular, 13.5f);
    const float valX = origin.x + labelW + gap;
    const float textY = origin.y + (rowH - ImGui::GetFontSize()) * 0.5f;
    const ImVec2 hSz = ImGui::CalcTextSize(hash.data(), hash.data() + hash.size());
    ImGui::GetWindowDrawList()->AddText(
        ImVec2(valX, textY), ToAbgrU32(ctx.palette.panelTitle), hash.data(), hash.data() + hash.size());

    const float btnSz = 20.0f * s;
    const ImVec2 btnPos(valX + hSz.x + 6.0f * s, origin.y + (rowH - btnSz) * 0.5f);
    ImGui::SetCursorScreenPos(btnPos);
    if (ImGui::InvisibleButton("##drw_copy_hash", ImVec2(btnSz, btnSz))) {
        ctx.host.CopyTextToClipboard(hash);
    }
    const bool hov = ImGui::IsItemHovered();
    if (hov) {
        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
        ElDrawTooltip({ctx.palette, ctx.fonts, s, &ctx.anim},
                      {ctx.tr.Tr("task.copy_link"), ElTooltipPlacement::Top});
    }
    const float icoSz = 12.0f * s;
    DrawMotrixIcon(IconSpec{
        MotrixIcon::Link,
        btnPos.x + (btnSz - icoSz) * 0.5f,
        btnPos.y + (btnSz - icoSz) * 0.5f,
        icoSz,
        icoSz,
        hov ? ctx.palette.primary : ctx.palette.subLabelText,
        s,
    });

    ImGui::SetCursorScreenPos(ImVec2(origin.x, origin.y + rowH + 8.0f * s));
    ImGui::Dummy(ImVec2(0.0f, 0.0f));
}

constexpr std::size_t kMaxHeatmapCells = 280;

[[nodiscard]] unsigned ParseHexNibble(char ch) noexcept {
    if (ch >= '0' && ch <= '9') {
        return static_cast<unsigned>(ch - '0');
    }
    if (ch >= 'a' && ch <= 'f') {
        return static_cast<unsigned>(10 + (ch - 'a'));
    }
    if (ch >= 'A' && ch <= 'F') {
        return static_cast<unsigned>(10 + (ch - 'A'));
    }
    return 0u;
}

[[nodiscard]] RgbaColor BucketColorForRatio(const MotrixPalette& p, float ratio) noexcept {
    if (ratio <= 0.0f) {
        return p.graphicS0;
    }
    if (ratio <= 0.5f) {
        return p.graphicS1;
    }
    if (ratio < 1.0f) {
        return p.graphicS2;
    }
    return p.graphicS3;
}

[[nodiscard]] float SampleBitfieldBucketRatio(std::string_view bitfieldHex,
                                              std::size_t cellIdx,
                                              std::size_t cellCount) noexcept {
    const std::size_t n = bitfieldHex.size();
    const std::size_t start = (cellIdx * n) / cellCount;
    const std::size_t end = std::max(start + 1u, ((cellIdx + 1u) * n) / cellCount);
    std::uint32_t doneBits = 0u;
    const std::uint32_t totalBits = static_cast<std::uint32_t>(end - start) * 4u;
    for (std::size_t i = start; i < end && i < n; ++i) {
        doneBits += static_cast<std::uint32_t>(std::popcount(ParseHexNibble(bitfieldHex[i])));
    }
    return totalBits > 0u ? static_cast<float>(doneBits) / static_cast<float>(totalBits) : 0.0f;
}

void RenderTaskGraphicHeatmap(UiFrameContext& ctx, std::string_view bitfieldHex) {
    const float s = ctx.dpiScale;
    const float availW = ImGui::GetContentRegionAvail().x;
    const float atom = 10.0f * s;
    const float gutter = 4.0f * s;
    const float step = atom + gutter;
    const float padX = 16.0f * s;
    const float padY = 14.0f * s;
    const int cols = std::max(1, static_cast<int>((availW - padX * 2.0f + gutter) / step));
    const std::size_t cellCount = std::min(bitfieldHex.size(), kMaxHeatmapCells);
    const int rows =
        cellCount == 0 ? 1 : static_cast<int>((cellCount + static_cast<std::size_t>(cols) - 1) / cols);
    const float boxH = padY * 2.0f + static_cast<float>(rows) * atom
                       + static_cast<float>(std::max(0, rows - 1)) * gutter;

    const ImVec2 boxMin = ImGui::GetCursorScreenPos();
    const ImVec2 boxMax(boxMin.x + availW, boxMin.y + boxH);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(boxMin, boxMax, ToAbgrU32(ctx.palette.graphicBoxBg), 4.0f * s);
    dl->AddRect(boxMin, boxMax, ToAbgrU32(ctx.palette.borderSubtle), 4.0f * s, 0, 1.0f);

    const float startX =
        (cellCount >= static_cast<std::size_t>(cols))
            ? boxMin.x + std::max(padX, (availW - static_cast<float>(cols) * step + gutter) * 0.5f)
            : boxMin.x + padX;
    for (std::size_t i = 0; i < cellCount; ++i) {
        const float ratio = SampleBitfieldBucketRatio(bitfieldHex, i, cellCount);
        const int col = static_cast<int>(i) % cols;
        const int row = static_cast<int>(i) / cols;
        const float cx = startX + static_cast<float>(col) * step;
        const float cy = boxMin.y + padY + static_cast<float>(row) * step;
        dl->AddRectFilled(ImVec2(cx, cy),
                          ImVec2(cx + atom, cy + atom),
                          ToAbgrU32(BucketColorForRatio(ctx.palette, ratio)),
                          2.0f * s);
    }

    ImGui::SetCursorScreenPos(ImVec2(boxMin.x, boxMax.y + 18.0f * s));
    ImGui::Dummy(ImVec2(0.0f, 0.0f));
}

void DrawActivityProgressBlock(UiFrameContext& ctx, const motrix::domain::DownloadTaskView& task) {
    const float s = ctx.dpiScale;
    const float labelW = kDetailLabelW * s;
    const float gap = kDetailColGap * s;
    const float rowH = 24.0f * s;
    const float availW = ImGui::GetContentRegionAvail().x;
    const float valW = std::max(100.0f * s, availW - labelW - gap);
    const float barX = ImGui::GetCursorScreenPos().x + labelW + gap;
    const ImVec2 origin = ImGui::GetCursorScreenPos();

    DrawColonLabel(ctx, {origin, labelW, rowH, ctx.tr.Tr("detail.progress_info")});

    const double frac = static_cast<double>(motrix::domain::ProgressOf(task));
    const std::string pctStr = FormatTrimmedPercent(frac);

    FontGuard pf(ctx.fonts, FontRole::Regular, 13.5f);
    const float pctSlotW = 54.0f * s;
    const float barW = std::max(60.0f * s, valW - pctSlotW - 10.0f * s);
    const float barH = 6.0f * s;
    const float barY = origin.y + (rowH - barH) * 0.5f;

    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(ImVec2(barX, barY),
                      ImVec2(barX + barW, barY + barH),
                      ToAbgrU32(ctx.palette.progressTrack),
                      barH * 0.5f);
    if (frac > 0.001) {
        const float fillW = std::max(barH, barW * static_cast<float>(frac));
        dl->AddRectFilled(ImVec2(barX, barY),
                          ImVec2(barX + fillW, barY + barH),
                          ToAbgrU32(ProgressColorFor(ctx.palette, task)),
                          barH * 0.5f);
    }

    const float pctTextY = origin.y + (rowH - ImGui::GetFontSize()) * 0.5f;
    dl->AddText(
        ImVec2(barX + barW + 14.0f * s, pctTextY), ToAbgrU32(ctx.palette.panelTitle), pctStr.c_str());

    const float sizeRowY = origin.y + rowH + 6.0f * s;
    const std::string sizeText =
        (task.totalLength == 0 && task.completedLength > 0)
            ? motrix::domain::FormatBytesFixed(task.completedLength, 2)
            : std::format("{} / {}",
                          motrix::domain::FormatBytesFixed(task.completedLength, 2),
                          motrix::domain::FormatBytesFixed(task.totalLength, 2));
    dl->AddText(ImVec2(barX, sizeRowY), ToAbgrU32(ctx.palette.subLabelText), sizeText.c_str());

    if (task.status == motrix::domain::TaskStatus::Active && !task.isSeeding) {
        if (const auto eta = motrix::domain::EtaSecondsOf(task); eta.has_value()) {
            const std::string etaText = motrix::domain::FormatRemainingEta(
                motrix::domain::RemainingBytesOf(task), task.downloadSpeed, ctx.tr.Tr("task.remaining"));
            const ImVec2 eSz = ImGui::CalcTextSize(etaText.c_str());
            dl->AddText(ImVec2(barX + barW - eSz.x, sizeRowY),
                        ToAbgrU32(ctx.palette.subLabelText),
                        etaText.c_str());
        }
    }

    ImGui::SetCursorScreenPos(ImVec2(origin.x, sizeRowY + 26.0f * s + 4.0f * s));
    ImGui::Dummy(ImVec2(0.0f, 0.0f));
}

}  // namespace

void RenderGeneralTab(UiFrameContext& ctx, const motrix::domain::DownloadTaskView& task) {
    const float s = ctx.dpiScale;
    ImGui::Dummy(ImVec2(0.0f, 4.0f * s));

    const std::string_view displayName =
        (task.btInfo.has_value() && !task.btInfo->name.empty()) ? task.btInfo->name : task.name;
    DrawDetailValueRow(ctx, {ctx.tr.Tr("detail.name"), displayName, kDetailLabelW, {}, false, true});
    DrawDirPathRow(ctx, task);
    DrawStatusTagRow(ctx, task);

    if (task.errorCode != 0) {
        DrawDetailValueRow(ctx,
                           {ctx.tr.Tr("detail.error_code"),
                            std::to_string(task.errorCode),
                            kDetailLabelW,
                            ctx.palette.progressError,
                            true});
    }
    if (!task.errorMessage.empty()) {
        DrawDetailValueRow(ctx,
                           {ctx.tr.Tr("detail.error_msg"),
                            task.errorMessage,
                            kDetailLabelW,
                            ctx.palette.progressError,
                            true,
                            true});
    }

    if (task.taskType == motrix::domain::TaskType::BitTorrent && task.btInfo.has_value()) {
        const auto& bt = *task.btInfo;
        DrawBtInfoDivider(ctx);
        if (!bt.infoHash.empty()) {
            DrawInfoHashRow(ctx, bt.infoHash);
        }
        DrawDetailValueRow(
            ctx,
            {ctx.tr.Tr("detail.piece_length"), motrix::domain::FormatBytesFixed(task.pieceLength, 1)});
        DrawDetailValueRow(ctx, {ctx.tr.Tr("detail.num_pieces"), std::to_string(task.numPieces)});
        const bool zhCn = (ctx.tr.CurrentLocale() == motrix::core::Locale::ZhCN);
        const std::string creationStr =
            (bt.creationTimestamp > 0)
                ? motrix::domain::FormatUnixTimestampLocal(bt.creationTimestamp, zhCn)
                : bt.creationDate;
        if (!creationStr.empty()) {
            DrawDetailValueRow(ctx, {ctx.tr.Tr("detail.creation_date"), creationStr});
        }
        if (!bt.comment.empty()) {
            DrawDetailValueRow(
                ctx, {ctx.tr.Tr("detail.comment"), bt.comment, kDetailLabelW, {}, false, true});
        }
    }
}

void RenderActivityTab(UiFrameContext& ctx, const motrix::domain::DownloadTaskView& task) {
    const float s = ctx.dpiScale;
    ImGui::Dummy(ImVec2(0.0f, 4.0f * s));
    RenderTaskGraphicHeatmap(ctx, task.bitfieldHex);
    DrawActivityProgressBlock(ctx, task);

    if (task.taskType == motrix::domain::TaskType::BitTorrent) {
        DrawDetailValueRow(ctx, {ctx.tr.Tr("detail.seeders"), std::to_string(task.numSeeders)});
        DrawDetailValueRow(ctx, {ctx.tr.Tr("detail.connections"), std::to_string(task.connections)});
        DrawDetailValueRow(
            ctx, {ctx.tr.Tr("detail.dl_speed"), motrix::domain::FormatSpeed(task.downloadSpeed)});
        DrawDetailValueRow(
            ctx, {ctx.tr.Tr("detail.ul_speed"), motrix::domain::FormatSpeed(task.uploadSpeed)});
        DrawDetailValueRow(
            ctx, {ctx.tr.Tr("detail.uploaded"), motrix::domain::FormatBytesFixed(task.uploadLength, 1)});
        DrawDetailValueRow(
            ctx, {ctx.tr.Tr("detail.ratio"), FormatTrimmedRatio(task.uploadLength, task.totalLength)});
    } else {
        DrawDetailValueRow(ctx, {ctx.tr.Tr("detail.connections"), std::to_string(task.connections)});
        DrawDetailValueRow(
            ctx, {ctx.tr.Tr("detail.dl_speed"), motrix::domain::FormatSpeed(task.downloadSpeed)});
    }
}

}  // namespace motrix::ui

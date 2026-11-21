module;

#include "imgui.h"

module motrix.ui;

import std;
import motrix.domain;
import motrix.app;
import :views_internal;
import :el_widgets;
import :theme;
import :anim;
import :icons;
import :scope_guard;

namespace motrix::ui {

namespace {

struct PillActionSpec {
    enum class Kind {
        Pause,
        Resume,
        StopSeeding,
        Restart,
        Delete,
        Folder,
        Link,
        Info
    };
    Kind kind;
    MotrixIcon icon;
    std::string_view tooltipKey;
};

[[nodiscard]] std::vector<PillActionSpec> ActionsForTask(const motrix::domain::DownloadTaskView& task) {
    using motrix::domain::TaskStatus;
    std::vector<PillActionSpec> out;
    if (task.status == TaskStatus::Active) {
        if (task.isSeeding) {
            out.push_back(
                {PillActionSpec::Kind::StopSeeding, MotrixIcon::TaskStopLine, "task.stop_seeding"});
        } else {
            out.push_back({PillActionSpec::Kind::Pause, MotrixIcon::TaskPauseLine, "task.pause"});
        }
        out.push_back({PillActionSpec::Kind::Delete, MotrixIcon::Delete, "task.delete"});
    } else if (task.status == TaskStatus::Waiting || task.status == TaskStatus::Paused) {
        out.push_back({PillActionSpec::Kind::Resume, MotrixIcon::TaskStartLine, "task.resume"});
        out.push_back({PillActionSpec::Kind::Delete, MotrixIcon::Delete, "task.delete"});
    } else {
        out.push_back({PillActionSpec::Kind::Restart, MotrixIcon::TaskRestart, "task.restart"});
        out.push_back({PillActionSpec::Kind::Delete, MotrixIcon::Trash, "task.remove_record"});
    }
    out.push_back({PillActionSpec::Kind::Folder, MotrixIcon::Folder, "task.show_in_folder"});
    out.push_back({PillActionSpec::Kind::Link, MotrixIcon::Link, "task.copy_link"});
    out.push_back({PillActionSpec::Kind::Info, MotrixIcon::InfoCircle, "task.more_info"});
    return out;
}

void DispatchPillAction(UiFrameContext& ctx,
                        const motrix::domain::DownloadTaskView& task,
                        PillActionSpec::Kind kind) {
    switch (kind) {
        case PillActionSpec::Kind::Pause:
        case PillActionSpec::Kind::StopSeeding:
            ctx.sink.Dispatch(motrix::app::PauseTaskCmd{task.gid});
            break;
        case PillActionSpec::Kind::Resume:
            ctx.sink.Dispatch(motrix::app::ResumeTaskCmd{task.gid});
            break;
        case PillActionSpec::Kind::Restart:
            ctx.sink.Dispatch(motrix::app::RestartTaskCmd{task.gid});
            break;
        case PillActionSpec::Kind::Delete: RequestTaskRemoval(ctx, task.gid, task.name); break;
        case PillActionSpec::Kind::Folder:
            ctx.sink.Dispatch(motrix::app::ShowInFolderCmd{task.gid});
            break;
        case PillActionSpec::Kind::Link:
            ctx.sink.Dispatch(motrix::app::CopyTaskLinkCmd{task.gid});
            break;
        case PillActionSpec::Kind::Info:
            ctx.sink.Dispatch(motrix::app::SelectTaskCmd{task.gid, true});
            break;
    }
}

void RenderFloatingPillActions(UiFrameContext& ctx,
                               const motrix::domain::DownloadTaskView& task,
                               float pillRightX,
                               float pillTopY) {
    const float s = ctx.dpiScale;
    const auto actions = ActionsForTask(task);
    const float slotW = 28.0f * s;
    const float iconSz = 14.0f * s;
    const float padX = LayoutMetrics::kPillPadX * s;
    const float pillH = LayoutMetrics::kPillHeight * s;
    const float pillW = padX * 2.0f + static_cast<float>(actions.size()) * slotW;
    const ImVec2 pMin(pillRightX - pillW, pillTopY);
    const ImVec2 pMax(pillRightX, pillTopY + pillH);

    const bool pillHov = ImGui::IsMouseHoveringRect(pMin, pMax, false);
    const float hovT = ctx.anim.Float(HashId(task.gid) ^ 0x5111u, pillHov ? 1.0f : 0.0f, 18.0f, 0.0f);

    ImDrawList* dl = ImGui::GetWindowDrawList();
    const RgbaColor bg = LerpColor(ctx.palette.pillBg, ctx.palette.pillHoverBg, hovT);
    const RgbaColor border = LerpColor(ctx.palette.pillBorder, ctx.palette.pillHoverBorder, hovT);
    dl->AddRectFilled(pMin, pMax, ToAbgrU32(bg), LayoutMetrics::kPillRadius * s);
    dl->AddRect(pMin, pMax, ToAbgrU32(border), LayoutMetrics::kPillRadius * s, 0, 1.0f);

    const RgbaColor iconCol = LerpColor(ctx.palette.pillIcon, ctx.palette.pillHoverIcon, hovT);
    for (std::size_t i = 0; i < actions.size(); ++i) {
        const ImVec2 bMin(pMin.x + padX + static_cast<float>(i) * slotW, pMin.y);
        ImGui::SetCursorScreenPos(bMin);
        IdGuard btnId(static_cast<int>(i));
        if (ImGui::InvisibleButton("##pill_btn", ImVec2(slotW, pillH))) {
            DispatchPillAction(ctx, task, actions[i].kind);
        }
        const bool btnHov = ImGui::IsItemHovered();
        const float btnHovT =
            ctx.anim.Float(HashId(task.gid) ^ (0x5200u + static_cast<std::uint32_t>(i)),
                           btnHov ? 1.0f : 0.0f,
                           24.0f,
                           0.0f);
        const float drawSz = iconSz * (1.0f + 0.12f * btnHovT);
        DrawMotrixIcon(IconSpec{
            actions[i].icon,
            bMin.x + (slotW - drawSz) * 0.5f,
            bMin.y + (pillH - drawSz) * 0.5f,
            drawSz,
            drawSz,
            iconCol,
            s,
        });
        if (btnHov) {
            ElDrawTooltip({ctx.palette, ctx.fonts, s, &ctx.anim},
                          {ctx.tr.Tr(actions[i].tooltipKey), ElTooltipPlacement::Top});
        }
    }
}

}  // namespace

RgbaColor ProgressColorFor(const MotrixPalette& p,
                           const motrix::domain::DownloadTaskView& task) noexcept {
    using motrix::domain::TaskStatus;
    switch (task.status) {
        case TaskStatus::Active: return task.isSeeding ? p.progressComplete : p.progressActive;
        case TaskStatus::Complete: return p.progressComplete;
        case TaskStatus::Error: return p.progressError;
        case TaskStatus::Waiting:
        case TaskStatus::Paused:
        case TaskStatus::Removed: return p.progressWaiting;
    }
    return p.progressWaiting;
}

namespace {

struct TaskCardGeometry {
    ImVec2 cardPos{};
    ImVec2 cardMax{};
    float padX = 0.0f;
    float padY = 0.0f;
    float progY = 0.0f;
    float progH = 0.0f;
    float progW = 0.0f;
    float nameLineH = 0.0f;
    float metaY = 0.0f;
};

void RenderTaskCardNameAndBar(UiFrameContext& ctx,
                              const motrix::domain::DownloadTaskView& task,
                              const TaskCardGeometry& g) {
    const float s = ctx.dpiScale;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    {
        FontGuard nameFont(ctx.fonts, FontRole::Regular, 15.0f);
        const float textMaxX = g.cardMax.x - 196.0f * s;
        ClipRectGuard clip(dl,
                           {g.cardPos.x + g.padX,
                            g.cardPos.y + g.padY,
                            std::max(g.cardPos.x + g.padX + 40.0f * s, textMaxX),
                            g.cardPos.y + g.padY + g.nameLineH,
                            true});
        dl->AddText(ImVec2(g.cardPos.x + g.padX, g.cardPos.y + g.padY + 3.0f * s),
                    ToAbgrU32(ctx.palette.taskNameText),
                    task.name.c_str(),
                    task.name.c_str() + task.name.size());
    }

    const ImVec2 trackMin(g.cardPos.x + g.padX, g.progY);
    const ImVec2 trackMax(g.cardPos.x + g.padX + g.progW, g.progY + g.progH);
    const float progRadius = LayoutMetrics::kProgressRadius * s;
    dl->AddRectFilled(trackMin, trackMax, ToAbgrU32(ctx.palette.progressTrack), progRadius);

    const float ratio = motrix::domain::ProgressOf(task);
    const float dispRatio = ctx.anim.Float(HashId(task.gid) ^ 0x7001u, ratio, 12.0f, ratio);
    if (dispRatio > 0.001f) {
        const float fillW = std::max(g.progH, g.progW * dispRatio);
        dl->AddRectFilled(trackMin,
                          ImVec2(trackMin.x + fillW, trackMax.y),
                          ToAbgrU32(ProgressColorFor(ctx.palette, task)),
                          progRadius);
    }
}

struct TaskMetaChip {
    bool hasIcon = false;
    MotrixIcon icon = MotrixIcon::Node;
    std::string text;
    bool optional = false;
};

[[nodiscard]] float MeasureChipsWidth(std::span<const TaskMetaChip> chips, float s) {
    const float iconW = 10.0f * s;
    const float iconGap = 3.0f * s;
    const float chipGap = 12.0f * s;
    float total = 0.0f;
    for (std::size_t i = 0; i < chips.size(); ++i) {
        if (i > 0) {
            total += chipGap;
        }
        if (chips[i].hasIcon) {
            total += iconW + iconGap;
        }
        total += ImGui::CalcTextSize(chips[i].text.c_str()).x;
    }
    return total;
}

void DrawActiveMetaChips(UiFrameContext& ctx,
                         const TaskCardGeometry& g,
                         std::span<const TaskMetaChip> chips) {
    const float s = ctx.dpiScale;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float iconW = 10.0f * s;
    const float iconH = 10.0f * s;
    const float iconGap = 3.0f * s;
    const float chipGap = 12.0f * s;
    const float lineH = ImGui::GetTextLineHeight();
    const float iconY = g.metaY + (lineH - iconH) * 0.5f;

    float curX = g.cardMax.x - g.padX - MeasureChipsWidth(chips, s);
    for (const auto& chip : chips) {
        if (chip.hasIcon) {
            DrawMotrixIcon(IconSpec{chip.icon, curX, iconY, iconW, iconH, ctx.palette.taskMetaText, s});
            curX += iconW + iconGap;
        }
        dl->AddText(ImVec2(curX, g.metaY), ToAbgrU32(ctx.palette.taskMetaText), chip.text.c_str());
        curX += ImGui::CalcTextSize(chip.text.c_str()).x + chipGap;
    }
}

void RenderActiveTaskMeta(UiFrameContext& ctx,
                          const motrix::domain::DownloadTaskView& task,
                          const TaskCardGeometry& g,
                          float minRightX) {
    const float s = ctx.dpiScale;
    const bool wide = ctx.viewportW >= LayoutMetrics::kAsideHideThreshold * s;
    const bool isBt = (task.taskType == motrix::domain::TaskType::BitTorrent);
    std::vector<TaskMetaChip> chips;
    if (isBt && (task.uploadSpeed > 0 || task.isSeeding)) {
        chips.push_back(
            {true, MotrixIcon::ArrowUp, motrix::domain::FormatSpeed(task.uploadSpeed), false});
    }
    if (!task.isSeeding || task.downloadSpeed > 0) {
        chips.push_back(
            {true, MotrixIcon::ArrowDown, motrix::domain::FormatSpeed(task.downloadSpeed), false});
    }
    if (wide) {
        const std::string eta = motrix::domain::FormatRemainingEta(
            motrix::domain::RemainingBytesOf(task), task.downloadSpeed, ctx.tr.Tr("task.remaining"));
        if (!eta.empty()) {
            chips.push_back({false, MotrixIcon::Node, eta, true});
        }
        if (isBt) {
            chips.push_back({true,
                             MotrixIcon::Magnet,
                             std::format("{} {}", task.numSeeders, ctx.tr.Tr("task.seeders")),
                             true});
        }
    }
    const std::string connLabel =
        wide ? std::format("{} {}", task.connections, ctx.tr.Tr("task.connections"))
             : std::to_string(task.connections);
    chips.push_back({true, MotrixIcon::Node, connLabel, false});

    while (g.cardMax.x - g.padX - MeasureChipsWidth(chips, s) < minRightX) {
        auto optIt =
            std::find_if(chips.begin(), chips.end(), [](const TaskMetaChip& c) { return c.optional; });
        if (optIt == chips.end()) {
            break;
        }
        chips.erase(optIt);
    }
    if (g.cardMax.x - g.padX - MeasureChipsWidth(chips, s) < minRightX && chips.size() > 1) {
        chips.back().text = std::to_string(task.connections);
    }
    DrawActiveMetaChips(ctx, g, chips);
}

void RenderTaskCardMeta(UiFrameContext& ctx,
                        const motrix::domain::DownloadTaskView& task,
                        const TaskCardGeometry& g) {
    const float s = ctx.dpiScale;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    FontGuard metaFont(ctx.fonts, FontRole::Regular, 13.0f);
    const std::string sizeText = (task.totalLength == 0 && task.completedLength > 0)
                                     ? motrix::domain::FormatBytes(task.completedLength)
                                     : std::format("{} / {}",
                                                   motrix::domain::FormatBytes(task.completedLength),
                                                   motrix::domain::FormatBytes(task.totalLength));
    dl->AddText(
        ImVec2(g.cardPos.x + g.padX, g.metaY), ToAbgrU32(ctx.palette.taskMetaText), sizeText.c_str());
    const float minRightX = g.cardPos.x + g.padX + ImGui::CalcTextSize(sizeText.c_str()).x + 12.0f * s;

    if (task.status == motrix::domain::TaskStatus::Active) {
        RenderActiveTaskMeta(ctx, task, g, minRightX);
    } else if (task.status == motrix::domain::TaskStatus::Error && !task.errorMessage.empty()) {
        const float rightEdge = g.cardMax.x - g.padX;
        if (rightEdge > minRightX + 8.0f * s) {
            const ImVec2 rSz = ImGui::CalcTextSize(task.errorMessage.c_str());
            ClipRectGuard errClip(
                dl, ClipRectSpec{minRightX, g.metaY - 2.0f * s, rightEdge, g.metaY + 20.0f * s, true});
            dl->AddText(ImVec2(std::max(minRightX, rightEdge - rSz.x), g.metaY),
                        ToAbgrU32(ctx.palette.progressError),
                        task.errorMessage.c_str());
        }
    }
}

}  // namespace

void RenderTaskCard(UiFrameContext& ctx, const motrix::domain::DownloadTaskView& task, float width) {
    const float s = ctx.dpiScale;
    const float padX = LayoutMetrics::kTaskCardPadX * s;
    const float padY = LayoutMetrics::kTaskCardPadY * s;
    const float progH = LayoutMetrics::kProgressHeight * s;
    const float nameLineH = 26.0f * s;
    const float cardH = padY * 2.0f + nameLineH + 24.0f * s + progH + 8.0f * s + 16.0f * s;

    const ImVec2 cardPos = ImGui::GetCursorScreenPos();
    const ImVec2 cardMax(cardPos.x + width, cardPos.y + cardH);

    IdGuard cardId(task.gid);
    ImGui::SetCursorScreenPos(cardPos);
    ImGui::SetNextItemAllowOverlap();
    if (ImGui::InvisibleButton("##task_card", ImVec2(width, cardH))) {
        const ImGuiIO& io = ImGui::GetIO();
        ctx.sink.Dispatch(motrix::app::SelectTaskCmd{task.gid, false, io.KeyCtrl, io.KeyShift});
    }
    if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
        ctx.sink.Dispatch(motrix::app::SelectTaskCmd{task.gid, true});
    }

    const bool hovered = ImGui::IsMouseHoveringRect(cardPos, cardMax, false);
    const bool inMultiSel =
        std::ranges::find(ctx.state.selectedTaskGids, task.gid) != ctx.state.selectedTaskGids.end();
    const bool selected =
        (ctx.state.selectedTaskGid == task.gid || ctx.state.detailedTaskGid == task.gid || inMultiSel);
    const float focusTarget = (hovered || selected) ? 1.0f : 0.0f;
    const float focusT = ctx.anim.Float(HashId(task.gid) ^ 0x4341u, focusTarget, 20.0f, focusTarget);
    const RgbaColor borderCol =
        LerpColor(ctx.palette.taskCardBorder, ctx.palette.taskCardHoverBorder, focusT);

    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(
        cardPos, cardMax, ToAbgrU32(ctx.palette.taskCardBg), LayoutMetrics::kTaskCardRadius * s);
    dl->AddRect(cardPos, cardMax, ToAbgrU32(borderCol), LayoutMetrics::kTaskCardRadius * s, 0, 1.0f);

    const float progY = cardPos.y + padY + nameLineH + 24.0f * s;
    const TaskCardGeometry geom{
        cardPos,
        cardMax,
        padX,
        padY,
        progY,
        progH,
        width - padX * 2.0f,
        nameLineH,
        progY + progH + 8.0f * s,
    };
    RenderTaskCardNameAndBar(ctx, task, geom);
    RenderTaskCardMeta(ctx, task, geom);
    RenderFloatingPillActions(ctx, task, cardMax.x - padX, cardPos.y + padY);

    ImGui::SetCursorScreenPos(ImVec2(cardPos.x, cardMax.y + LayoutMetrics::kTaskCardMarginBottom * s));
    ImGui::Dummy(ImVec2(0.0f, 0.0f));
}

}  // namespace motrix::ui

module;

#include "imgui.h"

module motrix.ui;

import std;
import motrix.domain;
import motrix.app;
import :views_internal;
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
        case PillActionSpec::Kind::Delete:
            ctx.sink.Dispatch(motrix::app::RemoveTaskCmd{task.gid, false});
            break;
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
    const RgbaColor bg = (hovT > 0.5f) ? ctx.palette.pillHoverBg : ctx.palette.pillBg;
    const RgbaColor border = (hovT > 0.5f) ? ctx.palette.pillHoverBorder : ctx.palette.pillBorder;
    dl->AddRectFilled(pMin, pMax, ToAbgrU32(bg), LayoutMetrics::kPillRadius * s);
    dl->AddRect(pMin, pMax, ToAbgrU32(border), LayoutMetrics::kPillRadius * s, 0, 1.0f);

    const RgbaColor iconCol = (hovT > 0.5f) ? ctx.palette.pillHoverIcon : ctx.palette.pillIcon;
    for (std::size_t i = 0; i < actions.size(); ++i) {
        const ImVec2 bMin(pMin.x + padX + static_cast<float>(i) * slotW, pMin.y);
        ImGui::SetCursorScreenPos(bMin);
        IdGuard btnId(static_cast<int>(i));
        if (ImGui::InvisibleButton("##pill_btn", ImVec2(slotW, pillH))) {
            DispatchPillAction(ctx, task, actions[i].kind);
        }
        const bool btnHov = ImGui::IsItemHovered();
        const float drawSz = btnHov ? (iconSz * 1.12f) : iconSz;
        DrawMotrixIcon(actions[i].icon,
                       bMin.x + (slotW - drawSz) * 0.5f,
                       bMin.y + (pillH - drawSz) * 0.5f,
                       drawSz,
                       drawSz,
                       iconCol,
                       s);
        if (btnHov) {
            const std::string_view tip = ctx.tr.Tr(actions[i].tooltipKey);
            ImGui::SetTooltip("%.*s", static_cast<int>(tip.size()), tip.data());
        }
    }
}

[[nodiscard]] RgbaColor ProgressColorFor(const MotrixPalette& p,
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

}  // namespace

void RenderTaskCard(UiFrameContext& ctx, const motrix::domain::DownloadTaskView& task, float width) {
    const float s = ctx.dpiScale;
    const float padX = LayoutMetrics::kTaskCardPadX * s;
    const float padY = LayoutMetrics::kTaskCardPadY * s;
    const float progH = LayoutMetrics::kProgressHeight * s;
    const float nameBottomGap = 24.0f * s;
    const float metaTopGap = 8.0f * s;
    const float nameLineH = 26.0f * s;
    const float metaLineH = 16.0f * s;
    const float cardH = padY * 2.0f + nameLineH + nameBottomGap + progH + metaTopGap + metaLineH;

    const ImVec2 cardPos = ImGui::GetCursorScreenPos();
    const ImVec2 cardMax(cardPos.x + width, cardPos.y + cardH);

    IdGuard cardId(task.gid);
    ImGui::SetCursorScreenPos(cardPos);
    ImGui::SetNextItemAllowOverlap();
    if (ImGui::InvisibleButton("##task_card", ImVec2(width, cardH))) {
        ctx.sink.Dispatch(motrix::app::SelectTaskCmd{task.gid, false});
    }

    const bool hovered = ImGui::IsMouseHoveringRect(cardPos, cardMax, false);
    const bool selected = (ctx.state.selectedTaskGid == task.gid);
    const RgbaColor borderCol =
        (hovered || selected) ? ctx.palette.taskCardHoverBorder : ctx.palette.taskCardBorder;

    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(
        cardPos, cardMax, ToAbgrU32(ctx.palette.taskCardBg), LayoutMetrics::kTaskCardRadius * s);
    dl->AddRect(cardPos, cardMax, ToAbgrU32(borderCol), LayoutMetrics::kTaskCardRadius * s, 0, 1.0f);

    // 1. Task name (left-aligned, truncated cleanly before pill area)
    {
        FontGuard nameFont(ctx.fonts, FontRole::Regular, 15.0f);
        const float textMaxX = cardMax.x - 196.0f * s;
        dl->PushClipRect(
            ImVec2(cardPos.x + padX, cardPos.y + padY),
            ImVec2(std::max(cardPos.x + padX + 40.0f * s, textMaxX), cardPos.y + padY + nameLineH),
            true);
        dl->AddText(ImVec2(cardPos.x + padX, cardPos.y + padY + 3.0f * s),
                    ToAbgrU32(ctx.palette.taskNameText),
                    task.name.c_str(),
                    task.name.c_str() + task.name.size());
        dl->PopClipRect();
    }

    // 2. Progress bar (6px height, pill radius 3px)
    const float progY = cardPos.y + padY + nameLineH + nameBottomGap;
    const float progW = width - padX * 2.0f;
    const ImVec2 trackMin(cardPos.x + padX, progY);
    const ImVec2 trackMax(cardPos.x + padX + progW, progY + progH);
    const float progRadius = LayoutMetrics::kProgressRadius * s;
    dl->AddRectFilled(trackMin, trackMax, ToAbgrU32(ctx.palette.progressTrack), progRadius);

    const float ratio = motrix::domain::ProgressOf(task);
    if (ratio > 0.001f) {
        const float fillW = std::max(progH, progW * ratio);
        dl->AddRectFilled(trackMin,
                          ImVec2(trackMin.x + fillW, trackMax.y),
                          ToAbgrU32(ProgressColorFor(ctx.palette, task)),
                          progRadius);
    }

    // 3. Bottom metadata row (completed/total on left, speed + remaining on right)
    {
        FontGuard metaFont(ctx.fonts, FontRole::Regular, 13.0f);
        const float metaY = progY + progH + metaTopGap;
        const std::string sizeText = std::format("{} / {}",
                                                 motrix::domain::FormatBytes(task.completedLength),
                                                 motrix::domain::FormatBytes(task.totalLength));
        dl->AddText(
            ImVec2(cardPos.x + padX, metaY), ToAbgrU32(ctx.palette.taskMetaText), sizeText.c_str());

        if (task.status == motrix::domain::TaskStatus::Active) {
            std::string rightMeta;
            if (task.taskType == motrix::domain::TaskType::BitTorrent && task.uploadSpeed > 0) {
                rightMeta += motrix::domain::FormatSpeed(task.uploadSpeed) + "   ";
            }
            rightMeta += motrix::domain::FormatSpeed(task.downloadSpeed);
            if (ctx.viewportW >= LayoutMetrics::kAsideHideThreshold * s) {
                const std::string eta =
                    motrix::domain::FormatRemainingEta(motrix::domain::RemainingBytesOf(task),
                                                       task.downloadSpeed,
                                                       ctx.tr.Tr("task.remaining"));
                if (!eta.empty()) {
                    rightMeta += " " + eta;
                }
            }
            const ImVec2 rSz = ImGui::CalcTextSize(rightMeta.c_str());
            dl->AddText(ImVec2(cardMax.x - padX - rSz.x, metaY),
                        ToAbgrU32(ctx.palette.taskMetaText),
                        rightMeta.c_str());
        } else if (task.status == motrix::domain::TaskStatus::Error && !task.errorMessage.empty()) {
            const ImVec2 rSz = ImGui::CalcTextSize(task.errorMessage.c_str());
            dl->AddText(ImVec2(std::max(cardPos.x + padX + 140.0f * s, cardMax.x - padX - rSz.x), metaY),
                        ToAbgrU32(ctx.palette.progressError),
                        task.errorMessage.c_str());
        }
    }

    // 4. Floating pill action bar at top-right (interactive on top of card button)
    RenderFloatingPillActions(ctx, task, cardMax.x - padX, cardPos.y + padY);

    ImGui::SetCursorScreenPos(ImVec2(cardPos.x, cardMax.y + LayoutMetrics::kTaskCardMarginBottom * s));
    ImGui::Dummy(ImVec2(0.0f, 0.0f));
}

}  // namespace motrix::ui

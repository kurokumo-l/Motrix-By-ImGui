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

[[nodiscard]] const std::vector<motrix::domain::DownloadTaskView>&
TasksForActiveCategory(const motrix::app::AppStateSnapshot& state) noexcept {
    switch (state.activeCategory) {
        case motrix::domain::TaskCategory::Downloading: return state.downloadingTasks;
        case motrix::domain::TaskCategory::Waiting: return state.waitingTasks;
        case motrix::domain::TaskCategory::Stopped: return state.stoppedTasks;
    }
    return state.downloadingTasks;
}

[[nodiscard]] std::string_view CategoryTitleKey(motrix::domain::TaskCategory cat) noexcept {
    switch (cat) {
        case motrix::domain::TaskCategory::Downloading: return "task.active";
        case motrix::domain::TaskCategory::Waiting: return "task.waiting";
        case motrix::domain::TaskCategory::Stopped: return "task.stopped";
    }
    return "task.active";
}

struct HeaderActionBtnSpec {
    const char* id = "";
    MotrixIcon icon = MotrixIcon::Refresh;
    float x = 0.0f;
    float y = 0.0f;
    std::string_view tooltip{};
    bool disabled = false;
};

bool HeaderActionIconButton(UiFrameContext& ctx, const HeaderActionBtnSpec& spec) {
    const float s = ctx.dpiScale;
    const float box = 24.0f * s;
    const float iconSz = 14.0f * s;

    ImGui::SetCursorScreenPos(ImVec2(spec.x, spec.y));
    const bool clicked = ImGui::InvisibleButton(spec.id, ImVec2(box, box));
    const bool hovered = !spec.disabled && ImGui::IsItemHovered();

    const RgbaColor col = spec.disabled
                              ? ctx.palette.headerActionDisabled
                              : (hovered ? ctx.palette.headerActionHover : ctx.palette.headerAction);
    DrawMotrixIcon(IconSpec{
        spec.icon,
        spec.x + (box - iconSz) * 0.5f,
        spec.y + (box - iconSz) * 0.5f,
        iconSz,
        iconSz,
        col,
        s,
    });

    if (hovered && !spec.tooltip.empty()) {
        ImGui::SetTooltip("%.*s", static_cast<int>(spec.tooltip.size()), spec.tooltip.data());
    }
    return !spec.disabled && clicked;
}

void RenderHeaderActions(UiFrameContext& ctx, float rightEdgeX, float topY) {
    const float s = ctx.dpiScale;
    const float step = 32.0f * s;
    float curX = rightEdgeX - 24.0f * s;

    if (ctx.state.activeCategory == motrix::domain::TaskCategory::Stopped) {
        if (HeaderActionIconButton(
                ctx, {"##act_purge", MotrixIcon::Purge, curX, topY, ctx.tr.Tr("task.purge_record")})) {
            ctx.sink.Dispatch(motrix::app::PurgeStoppedCmd{});
        }
        curX -= step;
    }

    if (HeaderActionIconButton(
            ctx,
            {"##act_pause_all", MotrixIcon::TaskPauseLine, curX, topY, ctx.tr.Tr("task.pause_all")})) {
        ctx.sink.Dispatch(motrix::app::PauseAllCmd{});
    }
    curX -= step;

    if (HeaderActionIconButton(
            ctx,
            {"##act_resume_all", MotrixIcon::TaskStartLine, curX, topY, ctx.tr.Tr("task.resume_all")})) {
        ctx.sink.Dispatch(motrix::app::ResumeAllCmd{});
    }
    curX -= step;

    if (HeaderActionIconButton(
            ctx, {"##act_refresh", MotrixIcon::Refresh, curX, topY, ctx.tr.Tr("task.refresh_list")})) {
        ctx.sink.Dispatch(motrix::app::RefreshTasksCmd{});
    }
    curX -= step;

    if (ctx.state.activeCategory != motrix::domain::TaskCategory::Stopped) {
        const bool disabled = ctx.state.selectedTaskGid.empty();
        if (HeaderActionIconButton(ctx,
                                   {"##act_del_sel",
                                    MotrixIcon::Delete,
                                    curX,
                                    topY,
                                    ctx.tr.Tr("task.delete_selected"),
                                    disabled})) {
            ctx.sink.Dispatch(motrix::app::RemoveTaskCmd{ctx.state.selectedTaskGid, false});
        }
        curX -= step;
    }

    if (ctx.viewportW < LayoutMetrics::kAsideHideThreshold * s
        && HeaderActionIconButton(
            ctx, {"##act_add_mobile", MotrixIcon::MenuAdd, curX, topY, ctx.tr.Tr("task.new_task")})) {
        ctx.sink.Dispatch(motrix::app::OpenModalCmd{motrix::app::ActiveModal::AddTask});
    }
}

void RenderEmptyState(UiFrameContext& ctx, const PanelRect& area) {
    const float s = ctx.dpiScale;
    const float ufoW = std::min(380.0f * s, area.w * 0.72f);
    const float ufoH = ufoW * (309.0f / 504.0f);
    const float blockH = ufoH + 28.0f * s;
    const float topY = area.y + std::max(24.0f * s, (area.h - blockH) * 0.42f);
    const float cx = area.x + area.w * 0.5f;

    DrawEmptyUfoIllustration({cx, topY, ufoW, ufoH, ctx.palette.isDark});

    FontGuard emptyFont(ctx.fonts, FontRole::Regular, 15.0f);
    const std::string_view label = ctx.tr.Tr("task.empty");
    const ImVec2 tSz = ImGui::CalcTextSize(label.data(), label.data() + label.size());
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddText(ImVec2(cx - tSz.x * 0.5f, topY + ufoH + 22.0f * s),
                ToAbgrU32(ctx.palette.emptyText),
                label.data(),
                label.data() + label.size());
}

}  // namespace

void RenderTaskPanel(UiFrameContext& ctx, const PanelRect& rect) {
    const float s = ctx.dpiScale;
    const bool wide = ctx.viewportW >= LayoutMetrics::kSubnavHideThreshold * s;
    const float marginX =
        (wide ? LayoutMetrics::kPanelMarginWide : LayoutMetrics::kPanelMarginNarrow) * s;
    const float headerH = LayoutMetrics::kPanelHeaderHeight * s;

    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(
        ImVec2(rect.x, rect.y), ImVec2(rect.x + rect.w, rect.y + rect.h), ToAbgrU32(ctx.palette.mainBg));

    const float titleY = rect.y + LayoutMetrics::kPanelHeaderTopPad * s;
    {
        FontGuard titleFont(ctx.fonts, FontRole::Regular, 18.0f);
        const std::string_view title = ctx.tr.Tr(CategoryTitleKey(ctx.state.activeCategory));
        dl->AddText(ImVec2(rect.x + marginX, titleY),
                    ToAbgrU32(ctx.palette.panelTitle),
                    title.data(),
                    title.data() + title.size());
    }
    RenderHeaderActions(ctx, rect.x + rect.w - marginX, titleY);

    const float borderY = rect.y + headerH - 2.0f * s;
    dl->AddRectFilled(ImVec2(rect.x + marginX, borderY),
                      ImVec2(rect.x + rect.w - marginX, borderY + 2.0f * s),
                      ToAbgrU32(ctx.palette.panelBorder));

    const auto& tasks = TasksForActiveCategory(ctx.state);
    const float contentY = rect.y + headerH;
    const float contentH = std::max(1.0f, rect.h - headerH);

    if (tasks.empty()) {
        RenderEmptyState(ctx, {rect.x, contentY, rect.w, contentH});
        ImGui::SetCursorScreenPos(ImVec2(rect.x, rect.y));
        ImGui::Dummy(ImVec2(0.0f, 0.0f));
        return;
    }

    ImGui::SetCursorScreenPos(ImVec2(rect.x, contentY));
    StyleVarGuard padGuard(ImGuiStyleVar_WindowPadding, marginX, 16.0f * s);
    ChildGuard scrollChild(
        "##task_list_scroll",
        ChildWindowSpec{
            rect.w, contentH, ImGuiChildFlags_AlwaysUseWindowPadding, ImGuiWindowFlags_None});
    if (scrollChild.IsVisible()) {
        const float cardW = std::max(120.0f * s, rect.w - marginX * 2.0f);
        for (const auto& task : tasks) {
            RenderTaskCard(ctx, task, cardW);
        }
        ImGui::Dummy(ImVec2(0.0f, 56.0f * s));
    }
}

}  // namespace motrix::ui

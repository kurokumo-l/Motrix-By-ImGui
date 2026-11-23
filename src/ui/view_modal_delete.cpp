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
import :anim;
import :icons;
import :scope_guard;

namespace motrix::ui {

namespace {

struct DeleteLayout {
    ImVec2 winPos{};
    float modalW = 420.0f;
    float padX = 24.0f;
    float x0 = 0.0f;
    float bodyW = 372.0f;
};

[[nodiscard]] std::string
ResolveTaskDisplayName(const UiFrameContext& ctx, std::string_view gid, std::string_view taskName) {
    if (!taskName.empty()) {
        return std::string(taskName);
    }
    if (const auto* task = motrix::app::FindTaskByGid(ctx.state, gid)) {
        return task->name;
    }
    return std::string(gid);
}

void CloseDeleteConfirm(DeleteConfirmState& dc) {
    dc.open = false;
    dc.requestOpen = false;
    dc.deleteFiles = false;
    dc.gids.clear();
    dc.taskName.clear();
    ImGui::CloseCurrentPopup();
}

[[nodiscard]] bool RenderDeleteHeader(UiFrameContext& ctx, const DeleteLayout& lay, float& curY) {
    const float s = ctx.dpiScale;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float closeBox = 24.0f * s;
    const float closeX = lay.winPos.x + lay.modalW - 14.0f * s - closeBox;
    const float closeY = lay.winPos.y + 14.0f * s;
    ImGui::SetCursorScreenPos(ImVec2(closeX, closeY));
    const bool closeClicked = ImGui::InvisibleButton("##del_close", ImVec2(closeBox, closeBox));
    const bool closeHov = ImGui::IsItemHovered();
    if (closeHov) {
        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    }
    DrawMotrixIcon(IconSpec{
        MotrixIcon::WinClose,
        closeX + 6.5f * s,
        closeY + 6.5f * s,
        11.0f * s,
        11.0f * s,
        LerpColor(ctx.palette.taskMetaText,
                  ctx.palette.primary,
                  ctx.anim.Float(HashId("##del_modal_close"), closeHov ? 1.0f : 0.0f, 22.0f, 0.0f)),
        s,
    });

    {
        FontGuard tf(ctx.fonts, FontRole::Medium, 17.5f);
        const std::string_view title = ctx.tr.Tr("task.confirm_delete_title");
        dl->AddText(ImVec2(lay.x0, curY + 2.0f * s),
                    ToAbgrU32(ctx.palette.panelTitle),
                    title.data(),
                    title.data() + title.size());
    }
    curY += 36.0f * s;
    return closeClicked;
}

void RenderDeleteBody(UiFrameContext& ctx, const DeleteLayout& lay, float& curY) {
    const float s = ctx.dpiScale;
    auto& dc = *ctx.deleteConfirm;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ElStyle elSt{ctx.palette, ctx.fonts, s, &ctx.anim};

    const bool isBatch = dc.gids.size() > 1;
    {
        FontGuard pf(ctx.fonts, FontRole::Regular, 14.5f);
        const std::string_view prompt =
            ctx.tr.Tr(isBatch ? "task.confirm_delete_batch" : "task.confirm_delete_single");
        dl->AddText(ImVec2(lay.x0, curY),
                    ToAbgrU32(ctx.palette.subLabelText),
                    prompt.data(),
                    prompt.data() + prompt.size());
    }
    curY += 28.0f * s;

    const float boxH = 34.0f * s;
    dl->AddRectFilled(ImVec2(lay.x0, curY),
                      ImVec2(lay.x0 + lay.bodyW, curY + boxH),
                      ToAbgrU32(ctx.palette.subnavBg),
                      4.0f * s);
    {
        FontGuard nf(ctx.fonts, FontRole::Medium, 14.0f);
        ClipRectGuard clip(
            dl,
            ClipRectSpec{lay.x0 + 12.0f * s, curY, lay.x0 + lay.bodyW - 12.0f * s, curY + boxH, true});
        const std::string boxText =
            isBatch ? std::format("{} {}", dc.gids.size(), ctx.tr.Tr("task.selected_count"))
                    : dc.taskName;
        const ImVec2 sz = ImGui::CalcTextSize(boxText.c_str());
        dl->AddText(ImVec2(lay.x0 + 12.0f * s, curY + (boxH - sz.y) * 0.5f),
                    ToAbgrU32(ctx.palette.panelTitle),
                    boxText.c_str());
    }
    curY += boxH + 14.0f * s;

    ImGui::SetCursorScreenPos(ImVec2(lay.x0, curY));
    ElCheckbox(elSt, "##del_files_chk", ctx.tr.Tr("task.delete_files_checkbox"), &dc.deleteFiles);
    curY += 34.0f * s;
}

void RenderDeleteActions(UiFrameContext& ctx, const DeleteLayout& lay, float curY) {
    const float s = ctx.dpiScale;
    auto& dc = *ctx.deleteConfirm;
    const ElStyle elSt{ctx.palette, ctx.fonts, s, &ctx.anim};

    MotrixPalette confirmPal = ctx.palette;
    if (dc.deleteFiles) {
        confirmPal.primary = confirmPal.progressError;
    }
    const ElStyle confirmSt{confirmPal, ctx.fonts, s, &ctx.anim};

    float cancelW = 86.0f * s;
    float confirmW = 96.0f * s;
    {
        FontGuard bf(ctx.fonts, FontRole::Regular, 14.5f);
        const std::string_view cancelTxt = ctx.tr.Tr("app.cancel");
        const std::string_view confirmTxt = ctx.tr.Tr("task.confirm_remove_btn");
        cancelW = std::max(84.0f * s,
                           ImGui::CalcTextSize(cancelTxt.data(), cancelTxt.data() + cancelTxt.size()).x
                               + 28.0f * s);
        confirmW = std::max(
            96.0f * s,
            ImGui::CalcTextSize(confirmTxt.data(), confirmTxt.data() + confirmTxt.size()).x + 28.0f * s);
    }

    const float btnH = 34.0f * s;
    const float gap = 10.0f * s;
    const float confirmX = lay.x0 + lay.bodyW - confirmW;
    const float cancelX = confirmX - gap - cancelW;

    ImGui::SetCursorScreenPos(ImVec2(cancelX, curY));
    if (ElSecondaryButton(elSt, {"##del_cancel", ctx.tr.Tr("app.cancel"), cancelW, btnH})) {
        CloseDeleteConfirm(dc);
    }

    ImGui::SetCursorScreenPos(ImVec2(confirmX, curY));
    if (ElPrimaryButton(confirmSt,
                        {"##del_confirm", ctx.tr.Tr("task.confirm_remove_btn"), confirmW, btnH})) {
        if (dc.gids.size() == 1) {
            ctx.sink.Dispatch(motrix::app::RemoveTaskCmd{dc.gids.front(), dc.deleteFiles});
        } else if (!dc.gids.empty()) {
            ctx.sink.Dispatch(motrix::app::RemoveTasksCmd{dc.gids, dc.deleteFiles});
        }
        CloseDeleteConfirm(dc);
    }

    ImGui::SetCursorScreenPos(ImVec2(lay.winPos.x, curY + btnH + 20.0f * s));
    ImGui::Dummy(ImVec2(lay.modalW, 0.0f));
}

[[nodiscard]] bool AllTasksInStopped(const motrix::app::AppStateSnapshot& state,
                                     std::span<const std::string> gids) noexcept {
    if (gids.empty()) {
        return false;
    }
    return std::ranges::all_of(gids, [&](const std::string& gid) {
        return std::ranges::any_of(state.stoppedTasks, [&](const auto& t) { return t.gid == gid; });
    });
}

}  // namespace

void RequestTasksRemoval(UiFrameContext& ctx,
                         std::vector<std::string> gids,
                         std::string_view singleName) {
    std::erase_if(gids, [](const std::string& g) { return g.empty(); });
    if (gids.empty()) {
        return;
    }
    if (AllTasksInStopped(ctx.state, gids)) {
        if (gids.size() == 1) {
            ctx.sink.Dispatch(motrix::app::RemoveTaskCmd{std::move(gids.front()), false});
        } else {
            ctx.sink.Dispatch(motrix::app::RemoveTasksCmd{std::move(gids), false});
        }
        return;
    }
    const bool bypassConfirm =
        ctx.state.prefs.noConfirmBeforeDeleteTask || (ctx.deleteConfirm == nullptr);
    if (bypassConfirm) {
        const bool deleteFiles = ctx.state.prefs.deleteFilesWhenDeleteTask;
        if (gids.size() == 1) {
            ctx.sink.Dispatch(motrix::app::RemoveTaskCmd{std::move(gids.front()), deleteFiles});
        } else {
            ctx.sink.Dispatch(motrix::app::RemoveTasksCmd{std::move(gids), deleteFiles});
        }
        return;
    }
    auto& dc = *ctx.deleteConfirm;
    dc.taskName =
        (gids.size() == 1) ? ResolveTaskDisplayName(ctx, gids.front(), singleName) : std::string{};
    dc.gids = std::move(gids);
    dc.deleteFiles = ctx.state.prefs.deleteFilesWhenDeleteTask;
    dc.requestOpen = true;
    dc.open = true;
}

void RequestTaskRemoval(UiFrameContext& ctx, std::string_view gid, std::string_view taskName) {
    if (!gid.empty()) {
        RequestTasksRemoval(ctx, {std::string(gid)}, taskName);
    }
}

void RenderDeleteConfirmModal(UiFrameContext& ctx) {
    if (ctx.deleteConfirm == nullptr || !ctx.deleteConfirm->open) {
        return;
    }
    bool justOpened = false;
    if (ctx.deleteConfirm->requestOpen) {
        ImGui::OpenPopup("##DeleteConfirmModal");
        ctx.deleteConfirm->requestOpen = false;
        justOpened = true;
    }

    const float s = ctx.dpiScale;
    const float openT = SmoothOpenProgress(ctx.anim, HashId("modal.del.open"), justOpened);
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    const float modalW = 430.0f * s;
    const float openOffsetY = 6.0f * s * (1.0f - openT);
    ImGui::SetNextWindowPos(
        ImVec2(vp->Pos.x + vp->Size.x * 0.5f, vp->Pos.y + vp->Size.y * 0.5f + openOffsetY),
        ImGuiCond_Always,
        ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(modalW, 0.0f), ImGuiCond_Always);

    StyleVarGuard winPad(ImGuiStyleVar_WindowPadding, 0.0f, 0.0f);
    StyleVarGuard winRound(ImGuiStyleVar_WindowRounding, 6.0f * s);
    StyleVarGuard winBorder(ImGuiStyleVar_WindowBorderSize, 0.0f);
    StyleColorGuard popBg(ImGuiCol_PopupBg, ctx.palette.modalBg);
    const float maxDim = ctx.palette.isDark ? 0.56f : 0.45f;
    StyleColorGuard dimBg(ImGuiCol_ModalWindowDimBg, RgbaColor{0.0f, 0.0f, 0.0f, maxDim * openT});

    constexpr ImGuiWindowFlags kFlags = ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoTitleBar
                                        | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings;
    PopupModalGuard modal("##DeleteConfirmModal", nullptr, kFlags);
    if (!modal.IsOpen()) {
        CloseDeleteConfirm(*ctx.deleteConfirm);
        return;
    }

    const ImVec2 winPos = ImGui::GetWindowPos();
    const float padX = 24.0f * s;
    const DeleteLayout lay{winPos, modalW, padX, winPos.x + padX, modalW - padX * 2.0f};
    float curY = winPos.y + 18.0f * s;

    const bool headerClose = RenderDeleteHeader(ctx, lay, curY);
    RenderDeleteBody(ctx, lay, curY);
    RenderDeleteActions(ctx, lay, curY);

    if (headerClose || ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
        CloseDeleteConfirm(*ctx.deleteConfirm);
    }
}

}  // namespace motrix::ui

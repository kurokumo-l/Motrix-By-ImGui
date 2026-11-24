module;

#include "imgui.h"

module motrix.ui;

import std;
import motrix.domain;
import motrix.app;
import :views_internal;
import :el_widgets;
import :theme;
import :icons;
import :anim;
import :scope_guard;

namespace motrix::ui {

namespace {

struct DrawerBox {
    float x = 0.0f;
    float y = 0.0f;
    float w = 0.0f;
    float h = 0.0f;
    float padX = 24.0f;
};

struct DrawerFooterSpec {
    const motrix::domain::DownloadTaskView& task;
    bool showFileButtons = false;
    bool interactive = true;
    float drawerW = 0.0f;
    float padX = 24.0f;
};

struct DrawerActionSpec {
    enum class Kind {
        Pause,
        Resume,
        StopSeeding,
        Restart,
        Delete,
        Folder,
        Link,
    };
    Kind kind;
    MotrixIcon icon;
    std::string_view tipKey;
};

struct DetailTabItem {
    int index = 0;
    MotrixIcon icon = MotrixIcon::DetailInfo;
    std::string_view tipKey;
};

[[nodiscard]] const motrix::domain::DownloadTaskView*
FindDetailedTask(const motrix::app::AppStateSnapshot& st) noexcept {
    if (st.detailedTask.has_value()) {
        return &(*st.detailedTask);
    }
    if (st.detailedTaskGid.empty()) {
        return nullptr;
    }
    const auto search = [&](const auto& vec) -> const motrix::domain::DownloadTaskView* {
        for (const auto& t : vec) {
            if (t.gid == st.detailedTaskGid) {
                return &t;
            }
        }
        return nullptr;
    };
    if (auto* p = search(st.downloadingTasks)) {
        return p;
    }
    if (auto* p = search(st.waitingTasks)) {
        return p;
    }
    return search(st.stoppedTasks);
}

[[nodiscard]] std::vector<DrawerActionSpec>
ActionsForDrawer(const motrix::domain::DownloadTaskView& task) {
    using motrix::domain::TaskStatus;
    std::vector<DrawerActionSpec> out;
    if (task.status == TaskStatus::Active) {
        if (task.isSeeding) {
            out.push_back(
                {DrawerActionSpec::Kind::StopSeeding, MotrixIcon::TaskStopLine, "task.stop_seeding"});
        } else {
            out.push_back({DrawerActionSpec::Kind::Pause, MotrixIcon::TaskPauseLine, "task.pause"});
        }
        out.push_back({DrawerActionSpec::Kind::Delete, MotrixIcon::Delete, "task.delete"});
    } else if (task.status == TaskStatus::Waiting || task.status == TaskStatus::Paused) {
        out.push_back({DrawerActionSpec::Kind::Resume, MotrixIcon::TaskStartLine, "task.resume"});
        out.push_back({DrawerActionSpec::Kind::Delete, MotrixIcon::Delete, "task.delete"});
    } else {
        out.push_back({DrawerActionSpec::Kind::Restart, MotrixIcon::TaskRestart, "task.restart"});
        out.push_back({DrawerActionSpec::Kind::Delete, MotrixIcon::Trash, "task.remove_record"});
    }
    out.push_back({DrawerActionSpec::Kind::Folder, MotrixIcon::Folder, "task.show_in_folder"});
    out.push_back({DrawerActionSpec::Kind::Link, MotrixIcon::Link, "task.copy_link"});
    return out;
}

void DispatchDrawerAction(UiFrameContext& ctx,
                          const motrix::domain::DownloadTaskView& task,
                          DrawerActionSpec::Kind kind) {
    switch (kind) {
        case DrawerActionSpec::Kind::Pause:
        case DrawerActionSpec::Kind::StopSeeding:
            ctx.sink.Dispatch(motrix::app::PauseTaskCmd{task.gid});
            break;
        case DrawerActionSpec::Kind::Resume:
            ctx.sink.Dispatch(motrix::app::ResumeTaskCmd{task.gid});
            break;
        case DrawerActionSpec::Kind::Restart:
            ctx.sink.Dispatch(motrix::app::RestartTaskCmd{task.gid});
            break;
        case DrawerActionSpec::Kind::Delete: RequestTaskRemoval(ctx, task.gid, task.name); break;
        case DrawerActionSpec::Kind::Folder:
            ctx.sink.Dispatch(motrix::app::ShowInFolderCmd{task.gid});
            break;
        case DrawerActionSpec::Kind::Link:
            ctx.sink.Dispatch(motrix::app::CopyTaskLinkCmd{task.gid});
            break;
    }
}

void ResetDrawerFilesDraft(DrawerUiState& ds, const motrix::domain::DownloadTaskView& task) {
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

void ApplyDrawerFilesDraft(UiFrameContext& ctx,
                           DrawerUiState& ds,
                           const motrix::domain::DownloadTaskView& task) {
    std::vector<int> indices;
    const std::size_t n = std::min(ds.draftSelected.size(), task.files.size());
    for (std::size_t i = 0; i < n; ++i) {
        if (ds.draftSelected[i]) {
            indices.push_back(task.files[i].index > 0 ? task.files[i].index : static_cast<int>(i + 1));
        }
    }
    if (!indices.empty()) {
        ctx.sink.Dispatch(motrix::app::ApplyTaskFileSelectionCmd{task.gid, std::move(indices)});
        ds.pendingApply = true;
        ds.pendingExpected = ds.draftSelected;
        ds.pendingUntil = std::chrono::steady_clock::now() + std::chrono::seconds(3);
        ds.dirty = false;
    }
}

void RenderDrawerPill(UiFrameContext& ctx,
                      const motrix::domain::DownloadTaskView& task,
                      ImVec2 pillMin,
                      float pillH,
                      bool interactive) {
    const float s = ctx.dpiScale;
    const auto actions = ActionsForDrawer(task);
    const float padX = 12.0f * s;
    const float slotW = 36.0f * s;
    const float iconSz = 14.0f * s;
    const float pillW = padX * 2.0f + static_cast<float>(actions.size()) * slotW;
    const ImVec2 pillMax(pillMin.x + pillW, pillMin.y + pillH);
    const bool modalBlocking = (ctx.state.activeModal != motrix::app::ActiveModal::None)
                               || (ctx.deleteConfirm != nullptr && ctx.deleteConfirm->open)
                               || ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopup);
    const bool pillHovered = interactive && !modalBlocking
                             && ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem)
                             && ImGui::IsMouseHoveringRect(pillMin, pillMax, true);
    const float pillHovT =
        AnimStep(&ctx.anim, {HashId("drawer.pill.hover"), pillHovered ? 1.0f : 0.0f, 18.0f, 0.0f});

    ImDrawList* dl = ImGui::GetWindowDrawList();
    const RgbaColor pillBg = LerpColor(ctx.palette.pillBg, ctx.palette.pillHoverBg, pillHovT);
    const RgbaColor pillBorder =
        LerpColor(ctx.palette.pillBorder, ctx.palette.pillHoverBorder, pillHovT);
    dl->AddRectFilled(pillMin, pillMax, ToAbgrU32(pillBg), pillH * 0.5f);
    dl->AddRect(pillMin, pillMax, ToAbgrU32(pillBorder), pillH * 0.5f, 0, 1.0f);

    const RgbaColor iconCol = LerpColor(ctx.palette.pillIcon, ctx.palette.pillHoverIcon, pillHovT);
    for (std::size_t i = 0; i < actions.size(); ++i) {
        IdGuard id(static_cast<int>(i));
        const float sx = pillMin.x + padX + static_cast<float>(i) * slotW;
        ImGui::SetCursorScreenPos(ImVec2(sx, pillMin.y));
        if (ImGui::InvisibleButton("##drw_act", ImVec2(slotW, pillH)) && interactive) {
            DispatchDrawerAction(ctx, task, actions[i].kind);
        }
        const bool hovered = interactive && ImGui::IsItemHovered();
        if (hovered) {
            ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
            ElDrawTooltip({ctx.palette, ctx.fonts, s, &ctx.anim},
                          {ctx.tr.Tr(actions[i].tipKey), ElTooltipPlacement::Top});
        }
        const float t =
            AnimStep(&ctx.anim, {ImGui::GetItemID() ^ 0xD201u, hovered ? 1.0f : 0.0f, 22.0f, 0.0f});
        if (t > 0.01f) {
            const float ringPad = 3.0f * s;
            dl->AddRectFilled(ImVec2(sx + ringPad, pillMin.y + ringPad),
                              ImVec2(sx + slotW - ringPad, pillMin.y + pillH - ringPad),
                              ToAbgrU32(WithAlpha(ctx.palette.pillHoverIcon, 0.18f * t)),
                              (pillH - ringPad * 2.0f) * 0.5f);
        }
        const float drawSz = iconSz * (1.0f + 0.12f * t);
        DrawMotrixIcon(IconSpec{
            actions[i].icon,
            sx + (slotW - drawSz) * 0.5f,
            pillMin.y + (pillH - drawSz) * 0.5f,
            drawSz,
            drawSz,
            iconCol,
            s,
        });
    }
}

void RenderDrawerFooter(UiFrameContext& ctx, DrawerUiState& ds, const DrawerFooterSpec& spec) {
    const float s = ctx.dpiScale;
    const ImVec2 footOrigin = ImGui::GetCursorScreenPos();
    const float footerH = 56.0f * s;
    const auto actions = ActionsForDrawer(spec.task);
    const float pillH = 30.0f * s;
    const float pillW = 24.0f * s + static_cast<float>(actions.size()) * 36.0f * s;
    const float pillX = footOrigin.x + (spec.drawerW - pillW) * 0.5f;
    const float pillY = footOrigin.y + (footerH - pillH) * 0.5f;
    RenderDrawerPill(ctx, spec.task, ImVec2(pillX, pillY), pillH, spec.interactive);

    if (!spec.showFileButtons || !ds.dirty) {
        return;
    }

    const ElStyle elSt{ctx.palette, ctx.fonts, s, &ctx.anim};
    const std::string_view resetLbl = ctx.tr.Tr("detail.reset");
    const std::string_view saveLbl = ctx.tr.Tr("common.save");
    const float btnH = 30.0f * s;
    const float btnY = footOrigin.y + (footerH - btnH) * 0.5f;

    FontGuard bf(ctx.fonts, FontRole::Regular, 13.0f);
    const ImVec2 resetSz = ImGui::CalcTextSize(resetLbl.data(), resetLbl.data() + resetLbl.size());
    const ImVec2 saveSz = ImGui::CalcTextSize(saveLbl.data(), saveLbl.data() + saveLbl.size());
    const float resetW = std::max(64.0f * s, resetSz.x + 26.0f * s);
    const float saveW = std::max(64.0f * s, saveSz.x + 26.0f * s);

    ImGui::SetCursorScreenPos(ImVec2(footOrigin.x + spec.padX, btnY));
    if (ElSecondaryButton(elSt, ElButtonSpec{"##drw_reset", resetLbl, resetW, btnH, !spec.interactive})
        && spec.interactive) {
        ResetDrawerFilesDraft(ds, spec.task);
    }

    const bool anySelected = std::ranges::any_of(ds.draftSelected, [](bool v) { return v; });
    ImGui::SetCursorScreenPos(ImVec2(footOrigin.x + spec.drawerW - spec.padX - saveW, btnY));
    if (ElPrimaryButton(
            elSt, ElButtonSpec{"##drw_save", saveLbl, saveW, btnH, !spec.interactive || !anySelected})
        && spec.interactive) {
        ApplyDrawerFilesDraft(ctx, ds, spec.task);
    }
}

[[nodiscard]] std::vector<DetailTabItem> TabsForTask(const motrix::domain::DownloadTaskView& task) {
    if (task.taskType == motrix::domain::TaskType::BitTorrent) {
        return {
            {0, MotrixIcon::DetailInfo, "detail.general"},
            {1, MotrixIcon::DetailGrid, "detail.activity"},
            {2, MotrixIcon::DetailCompass, "detail.trackers"},
            {3, MotrixIcon::DetailUser, "detail.peers"},
            {4, MotrixIcon::DetailFiles, "detail.files"},
        };
    }
    return {
        {0, MotrixIcon::DetailInfo, "detail.general"},
        {1, MotrixIcon::DetailGrid, "detail.activity"},
        {4, MotrixIcon::DetailFiles, "detail.files"},
    };
}

int RenderDrawerHeaderAndTabs(UiFrameContext& ctx,
                              const motrix::domain::DownloadTaskView& task,
                              const DrawerBox& box,
                              bool interactive) {
    const float s = ctx.dpiScale;
    const float headerY = box.y + 38.0f * s;
    const float contentL = box.x + box.padX;
    const float contentR = box.x + box.w - box.padX;

    ImDrawList* dl = ImGui::GetWindowDrawList();
    {
        FontGuard titleFnt(ctx.fonts, FontRole::Regular, 18.0f);
        const std::string_view title = ctx.tr.Tr("detail.title");
        dl->AddText(ImVec2(contentL, headerY),
                    ToAbgrU32(ctx.palette.panelTitle),
                    title.data(),
                    title.data() + title.size());
    }

    const float closeSz = 26.0f * s;
    const ImVec2 closePos(contentR - closeSz, headerY - 2.0f * s);
    ImGui::SetCursorScreenPos(closePos);
    if (ImGui::InvisibleButton("##drw_close", ImVec2(closeSz, closeSz)) && interactive) {
        ctx.sink.Dispatch(motrix::app::SelectTaskCmd{ctx.state.detailedTaskGid, true});
    }
    const bool closeHov = interactive && ImGui::IsItemHovered();
    if (closeHov) {
        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    }
    const float closeIco = 12.0f * s;
    DrawMotrixIcon(IconSpec{
        MotrixIcon::WinClose,
        closePos.x + (closeSz - closeIco) * 0.5f,
        closePos.y + (closeSz - closeIco) * 0.5f,
        closeIco,
        closeIco,
        closeHov ? ctx.palette.panelTitle : ctx.palette.subLabelText,
        s,
    });

    const auto tabs = TabsForTask(task);
    int activeTab = ctx.state.activeDetailTab;
    const bool tabValid =
        std::ranges::any_of(tabs, [activeTab](const DetailTabItem& t) { return t.index == activeTab; });
    if (!tabValid) {
        activeTab = tabs.front().index;
    }

    const float tabsY = box.y + 74.0f * s;
    const float tabH = 34.0f * s;
    const float slotW = 38.0f * s;
    const float slotPitch = 76.0f * s;
    const float iconSz = 15.0f * s;

    dl->AddLine(ImVec2(contentL, tabsY + tabH),
                ImVec2(contentR, tabsY + tabH),
                ToAbgrU32(ctx.palette.borderSubtle),
                1.0f);

    for (std::size_t i = 0; i < tabs.size(); ++i) {
        IdGuard tid(tabs[i].index);
        const float tx = contentL + static_cast<float>(i) * slotPitch;
        ImGui::SetCursorScreenPos(ImVec2(tx, tabsY));
        if (ImGui::InvisibleButton("##drw_tab", ImVec2(slotW, tabH)) && interactive) {
            ctx.sink.Dispatch(motrix::app::SetDetailTabCmd{tabs[i].index});
        }
        const bool hovered = interactive && ImGui::IsItemHovered();
        const bool isActive = (tabs[i].index == activeTab);
        if (hovered) {
            ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
            ElDrawTooltip({ctx.palette, ctx.fonts, s, &ctx.anim},
                          {ctx.tr.Tr(tabs[i].tipKey), ElTooltipPlacement::Bottom});
        }
        const float hovT =
            AnimStep(&ctx.anim, {ImGui::GetItemID() ^ 0xD301u, hovered ? 1.0f : 0.0f, 20.0f, 0.0f});
        const RgbaColor col = isActive ? ctx.palette.primary
                                       : LerpColor(ctx.palette.panelTitle, ctx.palette.primary, hovT);
        DrawMotrixIcon(IconSpec{
            tabs[i].icon,
            tx + (slotW - iconSz) * 0.5f,
            tabsY + (tabH - iconSz) * 0.5f - 1.0f * s,
            iconSz,
            iconSz,
            col,
            s,
            false,
            ctx.palette.modalBg,
        });
        if (isActive) {
            dl->AddRectFilled(ImVec2(tx, tabsY + tabH - 2.0f * s),
                              ImVec2(tx + slotW, tabsY + tabH),
                              ToAbgrU32(ctx.palette.primary));
        }
    }
    return activeTab;
}

void RenderDrawerTabBody(UiFrameContext& ctx,
                         DrawerUiState& ds,
                         const motrix::domain::DownloadTaskView& task,
                         int activeTab) {
    switch (activeTab) {
        case 0: RenderGeneralTab(ctx, task); break;
        case 1: RenderActivityTab(ctx, task); break;
        case 2: RenderTrackersTab(ctx, ctx.state.detailedTrackers); break;
        case 3: RenderPeersTab(ctx, ctx.state.detailedPeers); break;
        case 4: RenderFilesTab(ctx, ds, task); break;
        default: RenderGeneralTab(ctx, task); break;
    }
}

}  // namespace

void RenderTaskDetailDrawer(UiFrameContext& ctx, DrawerUiState& ds) {
    const auto* liveTask = FindDetailedTask(ctx.state);
    if (liveTask != nullptr) {
        ds.cachedTask = *liveTask;
    }
    const bool wantOpen = liveTask != nullptr;
    const float targetSlide = wantOpen ? 1.0f : 0.0f;
    const float initialSlide = (wantOpen && ctx.anim.FrameCount() <= 2) ? 1.0f : 0.0f;
    ds.slideProgress = ctx.anim.Float(HashId("drawer.slide"), targetSlide, 20.0f, initialSlide);
    if ((!wantOpen && ds.slideProgress < 0.01f) || !ds.cachedTask.has_value()) {
        if (!wantOpen && ds.slideProgress < 0.01f) {
            ds.cachedTask.reset();
            ds.dirty = false;
            ds.pendingApply = false;
            ds.pendingExpected.clear();
            ds.draftSelected.clear();
            ds.draftFileIndices.clear();
            ds.loadedGid.clear();
            ds.fileFilter = 0;
            ds.prevTab = -1;
        }
        return;
    }

    const bool deleteConfirmOpen = (ctx.deleteConfirm != nullptr && ctx.deleteConfirm->open);
    if (wantOpen && ctx.state.activeModal == motrix::app::ActiveModal::None && !deleteConfirmOpen
        && !ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopup)
        && ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
        ctx.sink.Dispatch(motrix::app::SelectTaskCmd{ctx.state.detailedTaskGid, true});
    }

    const auto* taskPtr = &(*ds.cachedTask);
    const float s = ctx.dpiScale;
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    const float drawerW = std::min(vp->Size.x, std::max(478.0f * s, vp->Size.x * 0.618f));
    const float offset = drawerW * (1.0f - ds.slideProgress);
    const DrawerBox box{
        vp->Pos.x + vp->Size.x - drawerW + offset, vp->Pos.y, drawerW, vp->Size.y, 24.0f * s};

    ImGui::SetNextWindowPos(vp->Pos);
    ImGui::SetNextWindowSize(vp->Size);
    constexpr ImGuiWindowFlags kWinFlags =
        ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoSavedSettings
        | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;
    StyleVarGuard padZero(ImGuiStyleVar_WindowPadding, 0.0f, 0.0f);
    WindowGuard win("##task_detail_overlay", nullptr, kWinFlags);
    if (!win.IsOpen()) {
        return;
    }

    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float dimAlpha = 0.50f * ds.slideProgress;
    dl->AddRectFilled(vp->Pos,
                      ImVec2(box.x, vp->Pos.y + vp->Size.y),
                      ToAbgrU32(RgbaColor{0.0f, 0.0f, 0.0f, dimAlpha}));

    const float backdropW = std::max(0.0f, box.x - vp->Pos.x);
    if (backdropW > 1.0f) {
        ImGui::SetCursorScreenPos(vp->Pos);
        if (ImGui::InvisibleButton("##drawer_backdrop", ImVec2(backdropW, box.h)) && wantOpen) {
            ctx.sink.Dispatch(motrix::app::SelectTaskCmd{ctx.state.detailedTaskGid, true});
        }
    }

    dl->AddRectFilled(
        ImVec2(box.x, box.y), ImVec2(box.x + box.w, box.y + box.h), ToAbgrU32(ctx.palette.modalBg));
    dl->AddLine(
        ImVec2(box.x, box.y), ImVec2(box.x, box.y + box.h), ToAbgrU32(ctx.palette.borderSubtle), 1.0f);

    const int activeTab = RenderDrawerHeaderAndTabs(ctx, *taskPtr, box, wantOpen);
    ds.prevTab = activeTab;

    const float bodyY = box.y + 124.0f * s;
    const float footerH = 56.0f * s;
    const float bodyH = std::max(100.0f * s, (box.y + box.h) - bodyY - footerH);

    ImGui::SetCursorScreenPos(ImVec2(box.x + box.padX, bodyY));
    {
        const ImGuiWindowFlags bodyFlags = wantOpen ? ImGuiWindowFlags_None : ImGuiWindowFlags_NoInputs;
        ChildGuard bodyChild("##drawer_body",
                             {box.w - box.padX * 2.0f, bodyH, ImGuiChildFlags_None, bodyFlags});
        if (bodyChild.IsVisible()) {
            RenderDrawerTabBody(ctx, ds, *taskPtr, activeTab);
        }
    }

    ImGui::SetCursorScreenPos(ImVec2(box.x, box.y + box.h - footerH));
    RenderDrawerFooter(ctx, ds, {*taskPtr, activeTab == 4, wantOpen, box.w, box.padX});
}

}  // namespace motrix::ui

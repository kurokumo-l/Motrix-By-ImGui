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

struct DrawerBox {
    float x = 0.0f;
    float y = 0.0f;
    float w = 0.0f;
    float h = 0.0f;
    float padX = 0.0f;
};

struct DrawerFooterSpec {
    const motrix::domain::DownloadTaskView& task;
    bool showFileButtons = false;
    float drawerW = 0.0f;
};

struct DrawerFileBtnSpec {
    const motrix::domain::DownloadTaskView& task;
    ImVec2 footerOrigin{};
    float drawerW = 0.0f;
};

void RenderDrawerFileButtons(UiFrameContext& ctx, DrawerUiState& ds, const DrawerFileBtnSpec& spec) {
    const auto& task = spec.task;
    const float s = ctx.dpiScale;
    const float h = 58.0f * s;
    const float padX = 24.0f * s;
    const float btnW = 78.0f * s;
    const float btnH = 30.0f * s;
    ImGui::SetCursorScreenPos(ImVec2(spec.footerOrigin.x + spec.drawerW - padX - btnW * 2.0f - 10.0f * s,
                                     spec.footerOrigin.y + (h - btnH) * 0.5f));
    {
        StyleColorGuard bg(ImGuiCol_Button, ctx.palette.subnavBg);
        StyleColorGuard fg(ImGuiCol_Text, ctx.palette.panelTitle);
        FontGuard fnt(ctx.fonts, FontRole::Regular, 13.0f);
        if (ImGui::Button(ctx.tr.Tr("prefs.reset").data(), ImVec2(btnW, btnH))) {
            ds.dirty = false;
            ds.loadedGid.clear();
        }
    }
    ImGui::SameLine(0.0f, 10.0f * s);

    std::vector<int> indices;
    for (std::size_t i = 0; i < task.files.size() && i < ds.draftSelected.size(); ++i) {
        if (ds.draftSelected[i]) {
            indices.push_back(task.files[i].index);
        }
    }
    const bool canSave = !indices.empty();
    StyleColorGuard bg(ImGuiCol_Button,
                       canSave ? ctx.palette.primary : ctx.palette.headerActionDisabled);
    StyleColorGuard fg(ImGuiCol_Text, HexColor(0xFFFFFF));
    FontGuard fnt(ctx.fonts, FontRole::Medium, 13.0f);
    if (ImGui::Button("Save", ImVec2(btnW, btnH)) && canSave) {
        ds.dirty = false;
        ctx.sink.Dispatch(motrix::app::ApplyTaskFileSelectionCmd{task.gid, std::move(indices)});
    }
}

void RenderDrawerFooter(UiFrameContext& ctx, DrawerUiState& ds, const DrawerFooterSpec& spec) {
    const float s = ctx.dpiScale;
    const float h = 58.0f * s;
    const float padX = 24.0f * s;
    const ImVec2 min = ImGui::GetCursorScreenPos();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddLine(ImVec2(min.x + padX, min.y),
                ImVec2(min.x + spec.drawerW - padX, min.y),
                ToAbgrU32(ctx.palette.borderSubtle),
                1.0f);

    ImGui::SetCursorScreenPos(ImVec2(min.x + padX, min.y + (h - 32.0f * s) * 0.5f));
    const auto pillAct = [&](const char* id, MotrixIcon icon, const std::function<void()>& fn) {
        const ImVec2 p = ImGui::GetCursorScreenPos();
        const float sz = 30.0f * s;
        if (ImGui::InvisibleButton(id, ImVec2(sz, sz))) {
            fn();
        }
        const bool hov = ImGui::IsItemHovered();
        dl->AddCircleFilled(ImVec2(p.x + sz * 0.5f, p.y + sz * 0.5f),
                            sz * 0.5f,
                            ToAbgrU32(hov ? ctx.palette.subnavActiveBg : ctx.palette.pillBg));
        dl->AddCircle(
            ImVec2(p.x + sz * 0.5f, p.y + sz * 0.5f), sz * 0.5f, ToAbgrU32(ctx.palette.pillBorder));
        const float ico = 14.0f * s;
        DrawMotrixIcon(IconSpec{
            icon,
            p.x + (sz - ico) * 0.5f,
            p.y + (sz - ico) * 0.5f,
            ico,
            ico,
            ctx.palette.pillIcon,
            s,
        });
        ImGui::SameLine(0.0f, 8.0f * s);
    };

    const auto& task = spec.task;
    const bool isActive = (task.status == motrix::domain::TaskStatus::Active);
    const bool isStopped = (task.status == motrix::domain::TaskStatus::Complete
                            || task.status == motrix::domain::TaskStatus::Error
                            || task.status == motrix::domain::TaskStatus::Removed);
    if (isActive) {
        pillAct("##drw_pause", MotrixIcon::TaskPauseLine, [&]() {
            ctx.sink.Dispatch(motrix::app::PauseTaskCmd{task.gid});
        });
    } else if (!isStopped) {
        pillAct("##drw_resume", MotrixIcon::TaskStartLine, [&]() {
            ctx.sink.Dispatch(motrix::app::ResumeTaskCmd{task.gid});
        });
    }
    pillAct("##drw_del", MotrixIcon::Trash, [&]() {
        ctx.sink.Dispatch(motrix::app::RemoveTaskCmd{task.gid, false});
    });
    pillAct("##drw_dir", MotrixIcon::Folder, [&]() {
        ctx.sink.Dispatch(motrix::app::ShowInFolderCmd{task.gid});
    });
    pillAct("##drw_link", MotrixIcon::Link, [&]() {
        ctx.sink.Dispatch(motrix::app::CopyTaskLinkCmd{task.gid});
    });

    if (spec.showFileButtons && ds.dirty) {
        RenderDrawerFileButtons(ctx, ds, DrawerFileBtnSpec{task, min, spec.drawerW});
    }
}

[[nodiscard]] const motrix::domain::DownloadTaskView*
FindDetailedTask(const motrix::app::AppStateSnapshot& st) noexcept {
    if (st.detailedTask && st.detailedTask->gid == st.detailedTaskGid) {
        return &(*st.detailedTask);
    }
    const auto search = [&](std::span<const motrix::domain::DownloadTaskView> list)
        -> const motrix::domain::DownloadTaskView* {
        for (const auto& t : list) {
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

struct DrawerTabSpec {
    int id = 0;
    std::string_view key{};
};

int RenderDrawerTabStrip(UiFrameContext& ctx,
                         const DrawerBox& box,
                         std::span<const DrawerTabSpec> tabs,
                         float tabsY) {
    const float s = ctx.dpiScale;
    int activeTab = ctx.state.activeDetailTab;
    const bool validTab =
        std::any_of(tabs.begin(), tabs.end(), [&](const DrawerTabSpec& t) { return t.id == activeTab; });
    if (!validTab) {
        activeTab = 0;
    }

    ImDrawList* dl = ImGui::GetWindowDrawList();
    float curTabX = box.x + box.padX;
    for (const auto& t : tabs) {
        const std::string_view lbl = ctx.tr.Tr(t.key);
        FontGuard tf(ctx.fonts, activeTab == t.id ? FontRole::Medium : FontRole::Regular, 14.0f);
        const ImVec2 sz = ImGui::CalcTextSize(lbl.data(), lbl.data() + lbl.size());
        const float tw = sz.x + 16.0f * s;
        const float th = 32.0f * s;
        ImGui::SetCursorScreenPos(ImVec2(curTabX, tabsY));
        IdGuard tid(t.id);
        if (ImGui::InvisibleButton("##drw_tab", ImVec2(tw, th))) {
            ctx.sink.Dispatch(motrix::app::SetDetailTabCmd{t.id});
            activeTab = t.id;
        }
        const bool selected = (activeTab == t.id);
        const RgbaColor txtCol = selected ? ctx.palette.primary : ctx.palette.subnavItemText;
        dl->AddText(ImVec2(curTabX + 8.0f * s, tabsY + (th - sz.y) * 0.5f),
                    ToAbgrU32(txtCol),
                    lbl.data(),
                    lbl.data() + lbl.size());
        if (selected) {
            dl->AddRectFilled(ImVec2(curTabX + 6.0f * s, tabsY + th - 2.0f * s),
                              ImVec2(curTabX + tw - 6.0f * s, tabsY + th),
                              ToAbgrU32(ctx.palette.primary));
        }
        curTabX += tw + 8.0f * s;
    }
    dl->AddLine(ImVec2(box.x + box.padX, tabsY + 32.0f * s),
                ImVec2(box.x + box.w - box.padX, tabsY + 32.0f * s),
                ToAbgrU32(ctx.palette.borderSubtle),
                1.0f);
    return activeTab;
}

int RenderDrawerHeaderAndTabs(UiFrameContext& ctx,
                              const motrix::domain::DownloadTaskView& task,
                              const DrawerBox& box) {
    const float s = ctx.dpiScale;
    const float headerY = box.y + 36.0f * s;
    {
        FontGuard tf(ctx.fonts, FontRole::Semibold, 16.0f);
        StyleColorGuard tc(ImGuiCol_Text, ctx.palette.panelTitle);
        ImGui::SetCursorScreenPos(ImVec2(box.x + box.padX, headerY));
        ImGui::TextUnformatted(task.name.c_str());
    }

    const float closeSz = 26.0f * s;
    const ImVec2 closePos(box.x + box.w - box.padX - closeSz, headerY - 2.0f * s);
    ImGui::SetCursorScreenPos(closePos);
    if (ImGui::InvisibleButton("##drawer_close", ImVec2(closeSz, closeSz))) {
        ctx.sink.Dispatch(motrix::app::SelectTaskCmd{ctx.state.detailedTaskGid, true});
    }
    DrawMotrixIcon(IconSpec{
        MotrixIcon::WinClose,
        closePos.x + (closeSz - 12.0f * s) * 0.5f,
        closePos.y + (closeSz - 12.0f * s) * 0.5f,
        12.0f * s,
        12.0f * s,
        ctx.palette.headerAction,
        s,
    });

    static constexpr std::array<DrawerTabSpec, 5> kBtTabs{{
        {0, "detail.general"},
        {1, "detail.activity"},
        {2, "detail.trackers"},
        {3, "detail.peers"},
        {4, "detail.files"},
    }};
    static constexpr std::array<DrawerTabSpec, 3> kPlainTabs{{
        {0, "detail.general"},
        {1, "detail.activity"},
        {4, "detail.files"},
    }};
    const std::span<const DrawerTabSpec> tabs = (task.taskType == motrix::domain::TaskType::BitTorrent)
                                                    ? std::span<const DrawerTabSpec>(kBtTabs)
                                                    : std::span<const DrawerTabSpec>(kPlainTabs);
    return RenderDrawerTabStrip(ctx, box, tabs, headerY + 32.0f * s);
}

void RenderDrawerTabBody(UiFrameContext& ctx,
                         DrawerUiState& ds,
                         const motrix::domain::DownloadTaskView& task,
                         int activeTab) {
    if (activeTab == 0) {
        RenderGeneralTab(ctx, task);
    } else if (activeTab == 1) {
        RenderActivityTab(ctx, task);
    } else if (activeTab == 2) {
        RenderTrackersTab(ctx, ctx.state.detailedTrackers);
    } else if (activeTab == 3) {
        RenderPeersTab(ctx, ctx.state.detailedPeers);
    } else if (activeTab == 4) {
        RenderFilesTab(ctx, ds, task);
    }
}

}  // namespace

void RenderTaskDetailDrawer(UiFrameContext& ctx, DrawerUiState& ds) {
    const bool wantOpen = !ctx.state.detailedTaskGid.empty();
    const float targetSlide = wantOpen ? 1.0f : 0.0f;
    ds.slideProgress = ctx.anim.Float(HashId("drawer.slide"), targetSlide, 18.0f, targetSlide);
    if (!wantOpen && ds.slideProgress < 0.01f) {
        return;
    }

    const auto* taskPtr = FindDetailedTask(ctx.state);
    if (taskPtr == nullptr) {
        return;
    }

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
    const float dimAlpha = (ctx.palette.isDark ? 0.45f : 0.25f) * ds.slideProgress;
    dl->AddRectFilled(vp->Pos,
                      ImVec2(box.x, vp->Pos.y + vp->Size.y),
                      ToAbgrU32(RgbaColor{0.0f, 0.0f, 0.0f, dimAlpha}));

    const float backdropW = std::max(0.0f, box.x - vp->Pos.x);
    if (backdropW > 1.0f) {
        ImGui::SetCursorScreenPos(vp->Pos);
        if (ImGui::InvisibleButton("##drawer_backdrop", ImVec2(backdropW, box.h))) {
            ctx.sink.Dispatch(motrix::app::SelectTaskCmd{ctx.state.detailedTaskGid, true});
        }
    }

    dl->AddRectFilled(
        ImVec2(box.x, box.y), ImVec2(box.x + box.w, box.y + box.h), ToAbgrU32(ctx.palette.modalBg));
    dl->AddLine(
        ImVec2(box.x, box.y), ImVec2(box.x, box.y + box.h), ToAbgrU32(ctx.palette.borderSubtle), 1.0f);

    const int activeTab = RenderDrawerHeaderAndTabs(ctx, *taskPtr, box);
    const float bodyY = box.y + 114.0f * s;
    const float footerH = 58.0f * s;
    const float bodyH = std::max(100.0f * s, (box.y + box.h) - bodyY - footerH);

    ImGui::SetCursorScreenPos(ImVec2(box.x + box.padX, bodyY));
    {
        ChildGuard bodyChild(
            "##drawer_body",
            {box.w - box.padX * 2.0f, bodyH, ImGuiChildFlags_None, ImGuiWindowFlags_None});
        if (bodyChild.IsVisible()) {
            RenderDrawerTabBody(ctx, ds, *taskPtr, activeTab);
        }
    }

    ImGui::SetCursorScreenPos(ImVec2(box.x, box.y + box.h - footerH));
    RenderDrawerFooter(ctx, ds, {*taskPtr, activeTab == 4, box.w});
}

}  // namespace motrix::ui

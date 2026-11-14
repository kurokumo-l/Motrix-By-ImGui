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

void DrawAlignedLabel(const UiFrameContext& ctx, float x, float y, float h, std::string_view text) {
    FontGuard fnt(ctx.fonts, FontRole::Regular, 13.5f);
    const ImVec2 sz = ImGui::CalcTextSize(text.data(), text.data() + text.size());
    ImGui::GetWindowDrawList()->AddText(ImVec2(x, y + (h - sz.y) * 0.5f),
                                        ToAbgrU32(ctx.palette.subLabelText),
                                        text.data(),
                                        text.data() + text.size());
}

void ResetAddFormFields(const UiFrameContext& ctx, AddTaskFormState& form) {
    const std::string defaultDir =
        ctx.state.sysConfig.dir.empty() ? "C:/Users/Public/Downloads" : ctx.state.sysConfig.dir;
    CopyUtf8ToBuffer(defaultDir, form.dirBuffer.data(), form.dirBuffer.size());
    form.splitCount = ctx.state.sysConfig.split > 0 ? ctx.state.sysConfig.split : 32;
    CopyUtf8ToBuffer(
        ctx.state.sysConfig.userAgent, form.userAgentBuffer.data(), form.userAgentBuffer.size());
    CopyUtf8ToBuffer(ctx.state.prefs.useProxy ? ctx.state.sysConfig.allProxy : std::string{},
                     form.proxyBuffer.data(),
                     form.proxyBuffer.size());
    form.authBuffer.fill('\0');
    form.refererBuffer.fill('\0');
    form.cookieBuffer.fill('\0');
    form.uriBuffer.fill('\0');
    form.nameBuffer.fill('\0');
    form.torrentMeta.reset();
    form.torrentBase64.clear();
    form.torrentFileSelected.clear();
    form.torrentPath.clear();
    form.torrentError.clear();
    form.activeTab = 0;
    form.showAdvanced = false;
    form.navigateToDownloading = ctx.state.prefs.newTaskShowDownloading;
    form.focusRequested = true;
    form.initialized = true;
}

void CloseAddModalAndReset(UiFrameContext& ctx, AddTaskFormState& form) {
    form.uriBuffer.fill('\0');
    form.nameBuffer.fill('\0');
    form.torrentMeta.reset();
    form.torrentBase64.clear();
    form.torrentFileSelected.clear();
    form.torrentPath.clear();
    form.torrentError.clear();
    form.showAdvanced = false;
    form.initialized = false;
    ctx.sink.Dispatch(motrix::app::CloseModalCmd{});
    ImGui::CloseCurrentPopup();
}

template <typename Cmd> void PopulateCommonAddCmd(Cmd& cmd, const AddTaskFormState& form) {
    cmd.outName = form.nameBuffer.data();
    cmd.dir = form.dirBuffer.data();
    cmd.split = std::clamp(form.splitCount, 1, 128);
    cmd.userAgent = form.userAgentBuffer.data();
    cmd.authorization = form.authBuffer.data();
    cmd.referer = form.refererBuffer.data();
    cmd.cookie = form.cookieBuffer.data();
    cmd.allProxy = form.proxyBuffer.data();
    cmd.newTaskShowDownloading = form.navigateToDownloading;
}

void SubmitAddTaskForm(UiFrameContext& ctx, AddTaskFormState& form) {
    if (form.activeTab == 0) {
        auto uris = motrix::domain::SplitAndNormalizeUris(form.uriBuffer.data());
        if (uris.empty()) {
            return;
        }
        motrix::app::SubmitAddUriCmd cmd;
        cmd.uris = std::move(uris);
        PopulateCommonAddCmd(cmd, form);
        ctx.sink.Dispatch(std::move(cmd));
        CloseAddModalAndReset(ctx, form);
        return;
    }

    if (!form.torrentMeta.has_value() || form.torrentBase64.empty()) {
        return;
    }
    std::vector<int> selIndices;
    const auto& files = form.torrentMeta->files;
    for (std::size_t i = 0; i < files.size(); ++i) {
        if (i < form.torrentFileSelected.size() && form.torrentFileSelected[i]) {
            selIndices.push_back(files[i].index);
        }
    }
    if (selIndices.empty()) {
        form.torrentError = std::string(ctx.tr.Tr("task.select_at_least_one"));
        return;
    }

    motrix::app::SubmitAddTorrentCmd cmd;
    cmd.torrentBase64 = form.torrentBase64;
    cmd.selectedIndices = std::move(selIndices);
    cmd.totalFiles = files.size();
    PopulateCommonAddCmd(cmd, form);
    ctx.sink.Dispatch(std::move(cmd));
    CloseAddModalAndReset(ctx, form);
}

void SyncDroppedAndPrefilled(AddTaskFormState& form, const motrix::app::AppStateSnapshot& st) {
    if (st.dropSeq != form.seenDropSeq && !st.pendingDroppedTorrentPath.empty()) {
        form.seenDropSeq = st.dropSeq;
        if (st.pendingDroppedTorrentPath == "__TAB_TORRENT__") {
            form.activeTab = 1;
        } else if (st.pendingDroppedTorrentPath == "__TAB_URI__") {
            form.activeTab = 0;
        } else {
            form.activeTab = 1;
            LoadTorrentFileIntoForm(form, st.pendingDroppedTorrentPath);
        }
    }
    if (st.prefillUriSeq != form.seenPrefillSeq && !st.pendingPrefillUri.empty()) {
        form.seenPrefillSeq = st.prefillUriSeq;
        form.activeTab = 0;
        CopyUtf8ToBuffer(st.pendingPrefillUri, form.uriBuffer.data(), form.uriBuffer.size());
    }
}

void RenderAddTaskHeaderTabs(
    UiFrameContext& ctx, AddTaskFormState& form, float x0, float modalW, float curY) {
    const float s = ctx.dpiScale;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const auto drawTab = [&](const char* id, std::string_view label, int tabIdx, float tx) {
        FontGuard fg(ctx.fonts, form.activeTab == tabIdx ? FontRole::Medium : FontRole::Regular, 14.5f);
        const ImVec2 sz = ImGui::CalcTextSize(label.data(), label.data() + label.size());
        ImGui::SetCursorScreenPos(ImVec2(tx, curY));
        if (ImGui::InvisibleButton(id, ImVec2(sz.x, 30.0f * s))) {
            if (form.activeTab != tabIdx) {
                form.activeTab = tabIdx;
                if (tabIdx == 0) {
                    form.focusRequested = true;
                }
            }
        }
        const bool hov = ImGui::IsItemHovered();
        if (hov) {
            ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
        }
        const bool active = (form.activeTab == tabIdx);
        const RgbaColor txtCol = (active || hov) ? ctx.palette.primary : ctx.palette.panelTitle;
        dl->AddText(ImVec2(tx, curY), ToAbgrU32(txtCol), label.data(), label.data() + label.size());
        if (active) {
            const float barY = curY + sz.y + 8.0f * s;
            dl->AddRectFilled(
                ImVec2(tx, barY), ImVec2(tx + sz.x, barY + 2.0f * s), ToAbgrU32(ctx.palette.primary));
        }
        return tx + sz.x;
    };

    const float tab0End = drawTab("##tab_uri", ctx.tr.Tr("task.tab_uri"), 0, x0);
    drawTab("##tab_torrent", ctx.tr.Tr("task.tab_torrent"), 1, tab0End + 30.0f * s);

    const float closeBox = 24.0f * s;
    const float closeX = x0 + (modalW - 48.0f * s) + 24.0f * s - 16.0f * s - closeBox;
    ImGui::SetCursorScreenPos(ImVec2(closeX, curY - 2.0f * s));
    if (ImGui::InvisibleButton("##add_modal_close", ImVec2(closeBox, closeBox))) {
        CloseAddModalAndReset(ctx, form);
    }
    const bool closeHov = ImGui::IsItemHovered();
    if (closeHov) {
        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    }
    const float iconSz = 11.0f * s;
    DrawMotrixIcon(MotrixIcon::WinClose,
                   closeX + (closeBox - iconSz) * 0.5f,
                   curY - 2.0f * s + (closeBox - iconSz) * 0.5f,
                   iconSz,
                   iconSz,
                   closeHov ? ctx.palette.primary : ctx.palette.taskMetaText,
                   s);
}

void RenderAddTaskModal(UiFrameContext& ctx, AddTaskFormState& form) {
    if (!form.initialized) {
        ResetAddFormFields(ctx, form);
    }
    SyncDroppedAndPrefilled(form, ctx.state);

    const float s = ctx.dpiScale;
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    const float modalW = std::clamp(ctx.viewportW * 0.67f, 380.0f * s, 632.0f * s);
    ImGui::SetNextWindowPos(ImVec2(vp->Pos.x + vp->Size.x * 0.5f, vp->Pos.y + vp->Size.y * 0.5f),
                            ImGuiCond_Always,
                            ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(modalW, 0.0f), ImGuiCond_Always);

    StyleVarGuard winPad(ImGuiStyleVar_WindowPadding, 0.0f, 0.0f);
    StyleVarGuard winRound(ImGuiStyleVar_WindowRounding, 6.0f * s);
    StyleVarGuard winBorder(ImGuiStyleVar_WindowBorderSize, 0.0f);
    StyleColorGuard popBg(ImGuiCol_PopupBg, ctx.palette.modalBg);
    StyleColorGuard dimBg(ImGuiCol_ModalWindowDimBg,
                          RgbaColor{0.0f, 0.0f, 0.0f, ctx.palette.isDark ? 0.56f : 0.45f});

    constexpr ImGuiWindowFlags kFlags = ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoTitleBar
                                        | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings;
    if (!ImGui::BeginPopupModal("##AddTaskModal", nullptr, kFlags)) {
        return;
    }

    const ImVec2 winPos = ImGui::GetWindowPos();
    const float padX = 24.0f * s;
    const float x0 = winPos.x + padX;
    const float bodyW = modalW - padX * 2.0f;
    float curY = winPos.y + 18.0f * s;

    RenderAddTaskHeaderTabs(ctx, form, x0, modalW, curY);
    curY += 38.0f * s;

    if (form.activeTab == 0) {
        if (form.focusRequested) {
            ImGui::SetKeyboardFocusHere();
            form.focusRequested = false;
        }
        ImGui::SetCursorScreenPos(ImVec2(x0, curY));
        ElInputTextMultiline("##add_uris",
                             form.uriBuffer.data(),
                             form.uriBuffer.size(),
                             bodyW,
                             80.0f * s,
                             ctx.palette,
                             ctx.fonts,
                             s,
                             ctx.tr.Tr("task.uri_tips"));
        curY += 80.0f * s;
    } else {
        ImGui::SetCursorScreenPos(ImVec2(x0, curY));
        StyleVarGuard zeroChildPad(ImGuiStyleVar_WindowPadding, 0.0f, 0.0f);
        StyleColorGuard clearChildBg(ImGuiCol_ChildBg, RgbaColor{0.0f, 0.0f, 0.0f, 0.0f});
        if (ImGui::BeginChild("##torrent_tab_wrap",
                              ImVec2(bodyW, 0.0f),
                              ImGuiChildFlags_AutoResizeY,
                              ImGuiWindowFlags_NoScrollbar)) {
            RenderAddTorrentTab(ctx, form);
        }
        const float childH = ImGui::GetWindowSize().y;
        ImGui::EndChild();
        curY += std::max(114.0f * s, childH);
    }

    curY += 16.0f * s;
    const float rowH = 30.0f * s;
    const float lblW = 74.0f * s;
    const float rightColW = 166.0f * s;
    const float colGap = 16.0f * s;
    const float renameW = std::max(120.0f * s, bodyW - lblW - colGap - rightColW);

    DrawAlignedLabel(ctx, x0, curY, rowH, ctx.tr.Tr("task.rename_label"));
    ImGui::SetCursorScreenPos(ImVec2(x0 + lblW, curY));
    ElInputText("##add_rename",
                form.nameBuffer.data(),
                form.nameBuffer.size(),
                renameW,
                ctx.palette,
                ctx.fonts,
                s,
                ctx.tr.Tr("task.rename_placeholder"));

    const float splitsX = x0 + lblW + renameW + colGap;
    const float splitsLblW = 60.0f * s;
    DrawAlignedLabel(ctx, splitsX, curY, rowH, ctx.tr.Tr("task.splits"));
    ImGui::SetCursorScreenPos(ImVec2(splitsX + splitsLblW, curY));
    ElInputNumberInt(
        "##add_splits", &form.splitCount, 1, 128, 1, rightColW - splitsLblW, ctx.palette, ctx.fonts, s);
    curY += rowH + 14.0f * s;

    DrawAlignedLabel(ctx, x0, curY, rowH, ctx.tr.Tr("task.save_to"));
    ImGui::SetCursorScreenPos(ImVec2(x0 + lblW, curY));
    ElDirectoryInput(
        "##add_dir",
        form.dirBuffer.data(),
        form.dirBuffer.size(),
        bodyW - lblW,
        ctx.state.prefs.historyDirectories,
        ctx.state.prefs.favoriteDirectories,
        [&ctx](std::string_view cur) { return ctx.host.PickFolder(cur); },
        ctx.palette,
        ctx.fonts,
        s);
    curY += rowH + 16.0f * s;

    if (form.showAdvanced) {
        RenderAddTaskAdvancedFields(ctx, form, x0, bodyW, curY);
        curY += 6.0f * s;
    }

    const float footerH = 60.0f * s;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(ImVec2(winPos.x, curY),
                      ImVec2(winPos.x + modalW, curY + footerH),
                      ToAbgrU32(ctx.palette.modalFooterBg),
                      6.0f * s,
                      ImDrawFlags_RoundCornersBottom);

    ImGui::SetCursorScreenPos(ImVec2(x0, curY + (footerH - 20.0f * s) * 0.5f));
    ElCheckbox("##add_adv_toggle",
               ctx.tr.Tr("task.show_advanced"),
               &form.showAdvanced,
               ctx.palette,
               ctx.fonts,
               s);

    const float btnW = 72.0f * s;
    const float btnH = 32.0f * s;
    const float btnY = curY + (footerH - btnH) * 0.5f;
    const float submitX = winPos.x + modalW - padX - btnW;
    const float cancelX = submitX - 10.0f * s - btnW;

    ImGui::SetCursorScreenPos(ImVec2(cancelX, btnY));
    if (ElSecondaryButton(
            "##add_cancel", ctx.tr.Tr("app.cancel"), btnW, btnH, ctx.palette, ctx.fonts, s)) {
        CloseAddModalAndReset(ctx, form);
    }

    ImGui::SetCursorScreenPos(ImVec2(submitX, btnY));
    if (ElPrimaryButton(
            "##add_submit", ctx.tr.Tr("app.submit"), btnW, btnH, ctx.palette, ctx.fonts, s)) {
        SubmitAddTaskForm(ctx, form);
    }

    ImGui::SetCursorScreenPos(ImVec2(winPos.x, curY + footerH));
    ImGui::Dummy(ImVec2(modalW, 0.0f));

    if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
        CloseAddModalAndReset(ctx, form);
    } else if (ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Enter)) {
        SubmitAddTaskForm(ctx, form);
    }
    ImGui::EndPopup();
}

}  // namespace

void RenderModals(UiFrameContext& ctx, AddTaskFormState& addForm) {
    static bool wasAddOpen = false;
    static bool wasAboutOpen = false;

    if (ctx.state.activeModal == motrix::app::ActiveModal::AddTask) {
        if (!wasAddOpen) {
            addForm.initialized = false;
            ImGui::OpenPopup("##AddTaskModal");
        }
        wasAddOpen = true;
        RenderAddTaskModal(ctx, addForm);
    } else {
        wasAddOpen = false;
    }

    if (ctx.state.activeModal == motrix::app::ActiveModal::About) {
        if (!wasAboutOpen) {
            ImGui::OpenPopup("##AboutModal");
        }
        wasAboutOpen = true;
        RenderAboutModalDialog(ctx);
    } else {
        wasAboutOpen = false;
    }
}

}  // namespace motrix::ui

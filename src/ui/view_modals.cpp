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

constexpr std::string_view kAddProxyTipsUrl =
    "https://aria2.github.io/manual/en/html/aria2c.html#cmdoption-all-proxy";

struct ModalFlowPos {
    float x0 = 0.0f;
    float bodyW = 0.0f;
    float curY = 0.0f;
};

struct AddModalTabSpec {
    const char* id = "";
    std::string_view label{};
    int tabIdx = 0;
    float tx = 0.0f;
    float curY = 0.0f;
};

struct AddModalFooterBox {
    ImVec2 winPos{};
    float modalW = 0.0f;
    float padX = 0.0f;
    float x0 = 0.0f;
    float curY = 0.0f;
};

void ResetAddFormFields(const UiFrameContext& ctx, AddTaskFormState& form) {
    const std::string defaultDir =
        ctx.state.sysConfig.dir.empty() ? "C:/Users/Public/Downloads" : ctx.state.sysConfig.dir;
    CopyUtf8ToBuffer(defaultDir, form.dirBuffer.data(), form.dirBuffer.size());
    form.splitCount = ctx.state.sysConfig.split > 0 ? ctx.state.sysConfig.split : 16;
    CopyUtf8ToBuffer(
        ctx.state.sysConfig.userAgent, form.userAgentBuffer.data(), form.userAgentBuffer.size());
    CopyUtf8ToBuffer(ctx.state.prefs.useProxy ? ctx.state.sysConfig.allProxy : std::string{},
                     form.proxyBuffer.data(),
                     form.proxyBuffer.size());
    form.authBuffer.fill('\0');
    form.refererBuffer.fill('\0');
    form.cookieBuffer.fill('\0');
    form.nameBuffer.fill('\0');
    form.torrentMeta.reset();
    form.torrentBase64.clear();
    form.torrentFileSelected.clear();
    form.torrentPath.clear();
    form.torrentError.clear();
    form.uriError.clear();
    form.autoFilledTorrentName.clear();
    form.activeTab = 0;
    form.showAdvanced = false;
    form.navigateToDownloading = ctx.state.prefs.newTaskShowDownloading;
    form.focusRequested = true;
    form.initialized = true;
}

void ClearAddTaskFormState(AddTaskFormState& form) {
    form.uriBuffer.fill('\0');
    form.nameBuffer.fill('\0');
    form.dirBuffer.fill('\0');
    form.userAgentBuffer.fill('\0');
    form.authBuffer.fill('\0');
    form.refererBuffer.fill('\0');
    form.cookieBuffer.fill('\0');
    form.proxyBuffer.fill('\0');
    form.torrentMeta.reset();
    form.torrentBase64.clear();
    form.torrentFileSelected.clear();
    form.torrentPath.clear();
    form.torrentError.clear();
    form.uriError.clear();
    form.autoFilledTorrentName.clear();
    form.torrentFileFilter = 0;
    form.showAdvanced = false;
    form.initialized = false;
}

void CloseAddModalAndReset(UiFrameContext& ctx, AddTaskFormState& form) {
    ClearAddTaskFormState(form);
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
            form.uriError = std::string(ctx.tr.Tr("task.uri_required"));
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
        form.torrentError = std::string(ctx.tr.Tr("task.torrent_required"));
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

struct AddModalTabResult {
    float width = 0.0f;
    float barY = 0.0f;
};

AddModalTabResult
DrawAddModalTabItem(UiFrameContext& ctx, AddTaskFormState& form, const AddModalTabSpec& spec) {
    const float s = ctx.dpiScale;
    FontGuard fg(ctx.fonts, form.activeTab == spec.tabIdx ? FontRole::Medium : FontRole::Regular, 14.5f);
    const ImVec2 sz = ImGui::CalcTextSize(spec.label.data(), spec.label.data() + spec.label.size());
    ImGui::SetCursorScreenPos(ImVec2(spec.tx, spec.curY));
    if (ImGui::InvisibleButton(spec.id, ImVec2(sz.x, 30.0f * s)) && form.activeTab != spec.tabIdx) {
        form.activeTab = spec.tabIdx;
        form.uriError.clear();
        if (spec.tabIdx == 0) {
            form.focusRequested = true;
            if (!form.autoFilledTorrentName.empty()
                && std::string_view(form.nameBuffer.data()) == form.autoFilledTorrentName) {
                form.nameBuffer.fill('\0');
            }
        } else if (form.nameBuffer[0] == '\0' && form.torrentMeta.has_value()
                   && !form.torrentMeta->name.empty()) {
            CopyUtf8ToBuffer(form.torrentMeta->name, form.nameBuffer.data(), form.nameBuffer.size());
            form.autoFilledTorrentName = form.nameBuffer.data();
        }
    }
    const bool hov = ImGui::IsItemHovered();
    if (hov) {
        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    }
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const bool active = (form.activeTab == spec.tabIdx);
    const float txtT = ctx.anim.Float(
        HashId(spec.id) ^ 0xA100u, (active || hov) ? 1.0f : 0.0f, 22.0f, active ? 1.0f : 0.0f);
    const RgbaColor txtCol = LerpColor(ctx.palette.panelTitle, ctx.palette.primary, txtT);
    dl->AddText(ImVec2(spec.tx, spec.curY),
                ToAbgrU32(txtCol),
                spec.label.data(),
                spec.label.data() + spec.label.size());
    return {sz.x, spec.curY + sz.y + 8.0f * s};
}

void RenderAddTaskHeaderTabs(UiFrameContext& ctx, AddTaskFormState& form, const AddModalFooterBox& box) {
    const float s = ctx.dpiScale;
    const AddModalTabResult tab0 =
        DrawAddModalTabItem(ctx, form, {"##tab_uri", ctx.tr.Tr("task.tab_uri"), 0, box.x0, box.curY});
    const float tab1RelX = tab0.width + 30.0f * s;
    const AddModalTabResult tab1 = DrawAddModalTabItem(
        ctx, form, {"##tab_torrent", ctx.tr.Tr("task.tab_torrent"), 1, box.x0 + tab1RelX, box.curY});

    const float targetRelX = (form.activeTab == 0) ? 0.0f : tab1RelX;
    const float targetW = (form.activeTab == 0) ? tab0.width : tab1.width;
    const float barY = (form.activeTab == 0) ? tab0.barY : tab1.barY;
    const float barRelX = ctx.anim.Float(HashId("add.tab_x"), targetRelX, 24.0f, targetRelX);
    const float barW = ctx.anim.Float(HashId("add.tab_w"), targetW, 24.0f, targetW);
    ImGui::GetWindowDrawList()->AddRectFilled(ImVec2(box.x0 + barRelX, barY),
                                              ImVec2(box.x0 + barRelX + barW, barY + 2.0f * s),
                                              ToAbgrU32(ctx.palette.primary));

    const float closeBox = 24.0f * s;
    const float closeX = box.x0 + (box.modalW - 48.0f * s) + 24.0f * s - 16.0f * s - closeBox;
    ImGui::SetCursorScreenPos(ImVec2(closeX, box.curY - 2.0f * s));
    if (ImGui::InvisibleButton("##add_modal_close", ImVec2(closeBox, closeBox))) {
        CloseAddModalAndReset(ctx, form);
    }
    const bool closeHov = ImGui::IsItemHovered();
    if (closeHov) {
        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    }
    const float closeT =
        ctx.anim.Float(HashId("##add_modal_close"), closeHov ? 1.0f : 0.0f, 22.0f, 0.0f);
    const float iconSz = 11.0f * s;
    DrawMotrixIcon(IconSpec{
        MotrixIcon::WinClose,
        closeX + (closeBox - iconSz) * 0.5f,
        box.curY - 2.0f * s + (closeBox - iconSz) * 0.5f,
        iconSz,
        iconSz,
        LerpColor(ctx.palette.taskMetaText, ctx.palette.primary, closeT),
        s,
    });
}

void RenderAddTaskUriError(UiFrameContext& ctx, const AddTaskFormState& form, ModalFlowPos& flow) {
    if (form.uriError.empty()) {
        return;
    }
    const float s = ctx.dpiScale;
    FontGuard ef(ctx.fonts, FontRole::Regular, 12.5f);
    flow.curY += 6.0f * s;
    ImGui::GetWindowDrawList()->AddText(ImGui::GetFont(),
                                        ImGui::GetFontSize(),
                                        ImVec2(flow.x0, flow.curY),
                                        ToAbgrU32(ctx.palette.progressError),
                                        form.uriError.c_str(),
                                        nullptr,
                                        flow.bodyW);
    flow.curY += ImGui::CalcTextSize(form.uriError.c_str(), nullptr, false, flow.bodyW).y;
}

void RenderAddTaskPrimaryInputs(UiFrameContext& ctx, AddTaskFormState& form, ModalFlowPos& flow) {
    const float s = ctx.dpiScale;
    const ElStyle elSt{ctx.palette, ctx.fonts, s, &ctx.anim};
    if (form.activeTab == 0) {
        if (form.focusRequested) {
            ImGui::SetKeyboardFocusHere();
            form.focusRequested = false;
        }
        ImGui::SetCursorScreenPos(ImVec2(flow.x0, flow.curY));
        if (ElInputTextMultiline(elSt,
                                 {"##add_uris",
                                  form.uriBuffer.data(),
                                  form.uriBuffer.size(),
                                  flow.bodyW,
                                  ctx.tr.Tr("task.uri_tips"),
                                  0,
                                  80.0f * s})) {
            form.uriError.clear();
        }
        flow.curY += 80.0f * s;
        RenderAddTaskUriError(ctx, form, flow);
    } else {
        ImGui::SetCursorScreenPos(ImVec2(flow.x0, flow.curY));
        StyleVarGuard zeroChildPad(ImGuiStyleVar_WindowPadding, 0.0f, 0.0f);
        StyleColorGuard clearChildBg(ImGuiCol_ChildBg, RgbaColor{0.0f, 0.0f, 0.0f, 0.0f});
        float childH = 0.0f;
        {
            ChildGuard wrap(
                "##torrent_tab_wrap",
                ChildWindowSpec{
                    flow.bodyW, 0.0f, ImGuiChildFlags_AutoResizeY, ImGuiWindowFlags_NoScrollbar});
            if (wrap.IsVisible()) {
                RenderAddTorrentTab(ctx, form);
            }
            childH = ImGui::GetWindowSize().y;
        }
        flow.curY += std::max(114.0f * s, childH);
    }

    flow.curY += 16.0f * s;
    const float rowH = 30.0f * s;
    const float lblW = 74.0f * s;
    const float rightColW = 166.0f * s;
    const float colGap = 16.0f * s;
    const float renameW = std::max(120.0f * s, flow.bodyW - lblW - colGap - rightColW);

    DrawAlignedCaption(ctx, flow.x0, flow.curY, rowH, ctx.tr.Tr("task.rename_label"), 13.5f);
    ImGui::SetCursorScreenPos(ImVec2(flow.x0 + lblW, flow.curY));
    ElInputText(elSt,
                {"##add_rename",
                 form.nameBuffer.data(),
                 form.nameBuffer.size(),
                 renameW,
                 ctx.tr.Tr("task.rename_placeholder")});

    const float splitsX = flow.x0 + lblW + renameW + colGap;
    const float splitsLblW = 60.0f * s;
    DrawAlignedCaption(ctx, splitsX, flow.curY, rowH, ctx.tr.Tr("task.splits"), 13.5f);
    ImGui::SetCursorScreenPos(ImVec2(splitsX + splitsLblW, flow.curY));
    ElInputNumberInt(elSt, "##add_splits", {&form.splitCount, 1, 128, 1, rightColW - splitsLblW});
    flow.curY += rowH + 14.0f * s;

    DrawAlignedCaption(ctx, flow.x0, flow.curY, rowH, ctx.tr.Tr("task.save_to"), 13.5f);
    ImGui::SetCursorScreenPos(ImVec2(flow.x0 + lblW, flow.curY));
    ElDirectoryInput(elSt,
                     {"##add_dir",
                      form.dirBuffer.data(),
                      form.dirBuffer.size(),
                      flow.bodyW - lblW,
                      ctx.state.prefs.historyDirectories,
                      ctx.state.prefs.favoriteDirectories,
                      [&ctx](std::string_view cur) { return ctx.host.PickFolder(cur); }});
    flow.curY += rowH + 16.0f * s;
}

void RenderAddTaskAdvancedFields(UiFrameContext& ctx, AddTaskFormState& form, ModalFlowPos& flow) {
    const float s = ctx.dpiScale;
    const float rowH = 30.0f * s;
    const float gapY = 12.0f * s;
    const float lblW = 92.0f * s;
    const float fieldW = flow.bodyW - lblW;
    const ElStyle elSt{ctx.palette, ctx.fonts, s, &ctx.anim};

    DrawAlignedCaption(ctx, flow.x0, flow.curY, rowH, ctx.tr.Tr("task.ua_label"), 13.5f);
    ImGui::SetCursorScreenPos(ImVec2(flow.x0 + lblW, flow.curY));
    ElInputText(elSt,
                {"##add_ua",
                 form.userAgentBuffer.data(),
                 form.userAgentBuffer.size(),
                 fieldW,
                 ctx.tr.Tr("task.ua_placeholder")});
    flow.curY += rowH + gapY;

    const float halfGap = 14.0f * s;
    const float halfColW = (flow.bodyW - halfGap) * 0.5f;
    DrawAlignedCaption(ctx, flow.x0, flow.curY, rowH, ctx.tr.Tr("task.auth_label"), 13.5f);
    ImGui::SetCursorScreenPos(ImVec2(flow.x0 + lblW, flow.curY));
    ElInputText(elSt,
                {"##add_auth",
                 form.authBuffer.data(),
                 form.authBuffer.size(),
                 halfColW - lblW,
                 ctx.tr.Tr("task.auth_placeholder")});

    const float rightX = flow.x0 + halfColW + halfGap;
    const float refLblW = 66.0f * s;
    DrawAlignedCaption(ctx, rightX, flow.curY, rowH, ctx.tr.Tr("task.referer_label"), 13.5f);
    ImGui::SetCursorScreenPos(ImVec2(rightX + refLblW, flow.curY));
    ElInputText(elSt,
                {"##add_referer",
                 form.refererBuffer.data(),
                 form.refererBuffer.size(),
                 halfColW - refLblW,
                 ctx.tr.Tr("task.referer_placeholder")});
    flow.curY += rowH + gapY;

    DrawAlignedCaption(ctx, flow.x0, flow.curY, rowH, ctx.tr.Tr("task.cookie_label"), 13.5f);
    ImGui::SetCursorScreenPos(ImVec2(flow.x0 + lblW, flow.curY));
    ElInputText(elSt,
                {"##add_cookie",
                 form.cookieBuffer.data(),
                 form.cookieBuffer.size(),
                 fieldW,
                 ctx.tr.Tr("task.cookie_placeholder")});
    flow.curY += rowH + gapY;

    DrawAlignedCaption(ctx, flow.x0, flow.curY, rowH, ctx.tr.Tr("task.proxy_label"), 13.5f);
    ImGui::SetCursorScreenPos(ImVec2(flow.x0 + lblW, flow.curY));
    ElInputText(elSt,
                {"##add_proxy",
                 form.proxyBuffer.data(),
                 form.proxyBuffer.size(),
                 fieldW,
                 "[http://][USER:PASSWORD@]HOST[:PORT]"});
    flow.curY += rowH + 4.0f * s;

    ImGui::SetCursorScreenPos(ImVec2(flow.x0 + lblW, flow.curY));
    ElDrawLinkText(elSt, {ctx.tr.Tr("task.proxy_tips"), [&ctx]() {
                              ctx.host.OpenPathInExplorer(kAddProxyTipsUrl);
                          }});
    flow.curY += 20.0f * s;

    ImGui::SetCursorScreenPos(ImVec2(flow.x0 + lblW, flow.curY));
    ElCheckbox(
        elSt, "##add_nav_dl", ctx.tr.Tr("task.navigate_to_downloading"), &form.navigateToDownloading);
    flow.curY += 22.0f * s;
}

void RenderAddTaskFooter(UiFrameContext& ctx, AddTaskFormState& form, const AddModalFooterBox& box) {
    const float s = ctx.dpiScale;
    const float footerH = 60.0f * s;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(ImVec2(box.winPos.x, box.curY),
                      ImVec2(box.winPos.x + box.modalW, box.curY + footerH),
                      ToAbgrU32(ctx.palette.modalFooterBg),
                      6.0f * s,
                      ImDrawFlags_RoundCornersBottom);

    const ElStyle elSt{ctx.palette, ctx.fonts, s, &ctx.anim};
    ImGui::SetCursorScreenPos(ImVec2(box.x0, box.curY + (footerH - 20.0f * s) * 0.5f));
    ElCheckbox(elSt, "##add_adv_toggle", ctx.tr.Tr("task.show_advanced"), &form.showAdvanced);

    const float btnW = 72.0f * s;
    const float btnH = 32.0f * s;
    const float btnY = box.curY + (footerH - btnH) * 0.5f;
    const float submitX = box.winPos.x + box.modalW - box.padX - btnW;
    const float cancelX = submitX - 10.0f * s - btnW;

    ImGui::SetCursorScreenPos(ImVec2(cancelX, btnY));
    if (ElSecondaryButton(elSt, {"##add_cancel", ctx.tr.Tr("app.cancel"), btnW, btnH})) {
        CloseAddModalAndReset(ctx, form);
    }

    ImGui::SetCursorScreenPos(ImVec2(submitX, btnY));
    if (ElPrimaryButton(elSt, {"##add_submit", ctx.tr.Tr("app.submit"), btnW, btnH})) {
        SubmitAddTaskForm(ctx, form);
    }

    ImGui::SetCursorScreenPos(ImVec2(box.winPos.x, box.curY + footerH));
    ImGui::Dummy(ImVec2(box.modalW, 0.0f));
}

void RenderAddTaskModal(UiFrameContext& ctx, AddTaskFormState& form, bool justOpened) {
    if (!form.initialized) {
        ResetAddFormFields(ctx, form);
    }
    SyncDroppedAndPrefilled(form, ctx.state);

    const float s = ctx.dpiScale;
    const float openT = SmoothOpenProgress(ctx.anim, HashId("modal.add.open"), justOpened);
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    const float modalW = std::clamp(ctx.viewportW * 0.67f, 380.0f * s, 632.0f * s);
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
    PopupModalGuard modal("##AddTaskModal", nullptr, kFlags);
    if (!modal.IsOpen()) {
        return;
    }

    const bool hadOpenChildPopup = ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId);
    const ImVec2 winPos = ImGui::GetWindowPos();
    const float padX = 24.0f * s;
    ModalFlowPos flow{winPos.x + padX, modalW - padX * 2.0f, winPos.y + 18.0f * s};

    RenderAddTaskHeaderTabs(ctx, form, {winPos, modalW, padX, flow.x0, flow.curY});
    flow.curY += 38.0f * s;

    RenderAddTaskPrimaryInputs(ctx, form, flow);
    if (form.showAdvanced) {
        RenderAddTaskAdvancedFields(ctx, form, flow);
        flow.curY += 6.0f * s;
    }

    RenderAddTaskFooter(ctx, form, {winPos, modalW, padX, flow.x0, flow.curY});

    if (!hadOpenChildPopup && !ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId)
        && ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
        CloseAddModalAndReset(ctx, form);
    } else if (ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Enter, false)) {
        SubmitAddTaskForm(ctx, form);
    }
}

}  // namespace

void RenderModals(UiFrameContext& ctx, AddTaskFormState& addForm) {
    const bool addActive = (ctx.state.activeModal == motrix::app::ActiveModal::AddTask);
    if (addActive) {
        const bool justOpened = !addForm.wasAddOpen;
        if (justOpened) {
            addForm.initialized = false;
            ImGui::OpenPopup("##AddTaskModal");
        }
        addForm.wasAddOpen = true;
        RenderAddTaskModal(ctx, addForm, justOpened);
    } else if (addForm.wasAddOpen) {
        ClearAddTaskFormState(addForm);
        addForm.wasAddOpen = false;
    }

    const bool aboutActive = (ctx.state.activeModal == motrix::app::ActiveModal::About);
    if (aboutActive && !addForm.wasAboutOpen) {
        ImGui::OpenPopup("##AboutModal");
    }
    if (aboutActive) {
        RenderAboutModalDialog(ctx, !addForm.wasAboutOpen);
    }
    addForm.wasAboutOpen = aboutActive;
    RenderDeleteConfirmModal(ctx);
}

}  // namespace motrix::ui

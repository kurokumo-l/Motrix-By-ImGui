module;

#include "imgui.h"

module motrix.ui;

import std;
import motrix.core;
import motrix.domain;
import motrix.app;
import :views_internal;
import :theme;
import :icons;
import :scope_guard;

namespace motrix::ui {

namespace {

void DrawMutedLabel(const MotrixPalette& palette, std::string_view text) {
    StyleColorGuard col(ImGuiCol_Text, palette.taskMetaText);
    ImGui::TextUnformatted(text.data(), text.data() + text.size());
}

[[nodiscard]] std::string NullTerm(std::string_view sv) {
    return std::string(sv);
}

void RenderAddTaskModal(UiFrameContext& ctx, AddTaskFormState& form) {
    const bool justInitialized = !form.initialized;
    if (justInitialized) {
        const std::string defaultDir =
            ctx.state.sysConfig.dir.empty() ? "C:/Users/Public/Downloads" : ctx.state.sysConfig.dir;
        std::snprintf(form.dirBuffer.data(), form.dirBuffer.size(), "%s", defaultDir.c_str());
        form.splitCount = ctx.state.sysConfig.split;
        form.initialized = true;
    }
    if (ctx.state.dropSeq != form.seenDropSeq && !ctx.state.pendingDroppedTorrentPath.empty()) {
        form.seenDropSeq = ctx.state.dropSeq;
        form.activeTab = 1;
        LoadTorrentFileIntoForm(form, ctx.state.pendingDroppedTorrentPath);
    }

    const float s = ctx.dpiScale;
    ImGui::SetNextWindowSize(ImVec2(580.0f * s, 0.0f), ImGuiCond_Always);
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    StyleVarGuard winPad(ImGuiStyleVar_WindowPadding, 24.0f * s, 22.0f * s);
    StyleVarGuard winRound(ImGuiStyleVar_WindowRounding, 8.0f * s);
    StyleVarGuard winBorder(ImGuiStyleVar_WindowBorderSize, 1.0f);

    bool open = true;
    PopupModalGuard modal(
        "##add_task_modal", &open, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoTitleBar);
    if (!modal.IsOpen()) {
        return;
    }

    const float fieldW = ImGui::GetContentRegionAvail().x;
    {
        FontGuard titleFont(ctx.fonts, FontRole::Semibold, 18.0f);
        const std::string_view title = ctx.tr.Tr("task.new_task");
        ImGui::TextUnformatted(title.data(), title.data() + title.size());
    }

    ImGui::Dummy(ImVec2(0.0f, 8.0f * s));
    {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const ImVec2 tabStart = ImGui::GetCursorScreenPos();
        const auto tabBtn = [&](const char* id, std::string_view lbl, int tabIdx, float tx) {
            FontGuard tf(
                ctx.fonts, form.activeTab == tabIdx ? FontRole::Medium : FontRole::Regular, 14.0f);
            const ImVec2 sz = ImGui::CalcTextSize(lbl.data(), lbl.data() + lbl.size());
            const float tw = sz.x + 20.0f * s;
            const float th = 30.0f * s;
            ImGui::SetCursorScreenPos(ImVec2(tx, tabStart.y));
            if (ImGui::InvisibleButton(id, ImVec2(tw, th))) {
                form.activeTab = tabIdx;
            }
            const bool active = (form.activeTab == tabIdx);
            dl->AddText(ImVec2(tx + 10.0f * s, tabStart.y + (th - sz.y) * 0.5f),
                        ToAbgrU32(active ? ctx.palette.primary : ctx.palette.subnavItemText),
                        lbl.data(),
                        lbl.data() + lbl.size());
            if (active) {
                dl->AddRectFilled(ImVec2(tx + 6.0f * s, tabStart.y + th - 2.0f * s),
                                  ImVec2(tx + tw - 6.0f * s, tabStart.y + th),
                                  ToAbgrU32(ctx.palette.primary));
            }
            return tw;
        };
        const float w0 = tabBtn("##add_tab_uri", ctx.tr.Tr("task.tab_uri"), 0, tabStart.x);
        tabBtn("##add_tab_torrent", ctx.tr.Tr("task.tab_torrent"), 1, tabStart.x + w0 + 8.0f * s);
        dl->AddLine(ImVec2(tabStart.x, tabStart.y + 30.0f * s),
                    ImVec2(tabStart.x + fieldW, tabStart.y + 30.0f * s),
                    ToAbgrU32(ctx.palette.borderSubtle),
                    1.0f);
        ImGui::SetCursorScreenPos(ImVec2(tabStart.x, tabStart.y + 38.0f * s));
    }

    if (form.activeTab == 0) {
        DrawMutedLabel(ctx.palette, ctx.tr.Tr("task.uri_tips"));
        if (justInitialized || ImGui::IsWindowAppearing()) {
            ImGui::SetKeyboardFocusHere();
        }
        constexpr int kUriFlags = ImGuiInputTextFlags_WordWrap | ImGuiInputTextFlags_NoHorizontalScroll;
        ImGui::InputTextMultiline("##uri_input",
                                  form.uriBuffer.data(),
                                  form.uriBuffer.size(),
                                  ImVec2(fieldW, 104.0f * s),
                                  kUriFlags);

        ImGui::Dummy(ImVec2(0.0f, 6.0f * s));
        DrawMutedLabel(ctx.palette, ctx.tr.Tr("task.rename_label"));
        ImGui::SetNextItemWidth(fieldW);
        const std::string renameHint = NullTerm(ctx.tr.Tr("task.rename_placeholder"));
        ImGui::InputTextWithHint(
            "##name_input", renameHint.c_str(), form.nameBuffer.data(), form.nameBuffer.size());
    } else {
        RenderAddTorrentTab(ctx, form);
    }

    ImGui::Dummy(ImVec2(0.0f, 6.0f * s));
    DrawMutedLabel(ctx.palette, ctx.tr.Tr("task.save_to"));
    const float browseW = 82.0f * s;
    ImGui::SetNextItemWidth(std::max(160.0f * s, fieldW - browseW - 8.0f * s));
    ImGui::InputText("##dir_input", form.dirBuffer.data(), form.dirBuffer.size());
    ImGui::SameLine(0.0f, 8.0f * s);
    {
        StyleColorGuard bg(ImGuiCol_Button, ctx.palette.subnavBg);
        StyleColorGuard fg(ImGuiCol_Text, ctx.palette.panelTitle);
        FontGuard fnt(ctx.fonts, FontRole::Regular, 13.0f);
        if (ImGui::Button(ctx.tr.Tr("task.browse").data(), ImVec2(browseW, 0.0f))) {
            if (auto chosen = ctx.host.PickFolder(form.dirBuffer.data())) {
                std::snprintf(form.dirBuffer.data(), form.dirBuffer.size(), "%s", chosen->c_str());
            }
        }
    }

    ImGui::Dummy(ImVec2(0.0f, 6.0f * s));
    DrawMutedLabel(ctx.palette, ctx.tr.Tr("task.splits"));
    ImGui::SetNextItemWidth(176.0f * s);
    ImGui::SliderInt("##split_slider", &form.splitCount, 1, 64);

    ImGui::Dummy(ImVec2(0.0f, 10.0f * s));
    ImGui::Separator();
    ImGui::Dummy(ImVec2(0.0f, 8.0f * s));

    const float cancelW = 100.0f * s;
    const float submitW = 116.0f * s;
    const float btnH = 36.0f * s;
    const float availW = ImGui::GetContentRegionAvail().x;
    ImGui::SetCursorPosX(ImGui::GetCursorPosX()
                         + std::max(0.0f, availW - cancelW - submitW - 10.0f * s));
    {
        StyleColorGuard secBg(ImGuiCol_Button, ctx.palette.inputBg);
        StyleColorGuard secHov(ImGuiCol_ButtonHovered, ctx.palette.subnavActiveBg);
        StyleColorGuard secAct(ImGuiCol_ButtonActive, ctx.palette.borderSubtle);
        StyleColorGuard secTxt(ImGuiCol_Text, ctx.palette.panelTitle);
        const std::string cancelLabel = NullTerm(ctx.tr.Tr("app.cancel")) + "##add_cancel";
        if (ImGui::Button(cancelLabel.c_str(), ImVec2(cancelW, btnH)) || !open
            || ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
            ctx.sink.Dispatch(motrix::app::CloseModalCmd{});
        }
    }
    ImGui::SameLine(0.0f, 10.0f * s);
    {
        StyleColorGuard priTxt(ImGuiCol_Text, HexColor(0xFFFFFF));
        const std::string submitLabel = NullTerm(ctx.tr.Tr("app.submit")) + "##add_submit";
        if (ImGui::Button(submitLabel.c_str(), ImVec2(submitW, btnH))) {
            if (form.activeTab == 0) {
                auto uris = motrix::domain::SplitAndNormalizeUris(form.uriBuffer.data());
                if (!uris.empty()) {
                    ctx.sink.Dispatch(motrix::app::SubmitAddUriCmd{
                        .uris = std::move(uris),
                        .outName = form.nameBuffer.data(),
                        .dir = form.dirBuffer.data(),
                        .split = form.splitCount,
                    });
                    form.uriBuffer[0] = '\0';
                    form.nameBuffer[0] = '\0';
                }
            } else if (form.torrentMeta.has_value() && !form.torrentBase64.empty()) {
                std::vector<int> indices;
                for (std::size_t i = 0; i < form.torrentMeta->files.size(); ++i) {
                    if (i < form.torrentFileSelected.size() && form.torrentFileSelected[i]) {
                        indices.push_back(form.torrentMeta->files[i].index);
                    }
                }
                if (!indices.empty()) {
                    ctx.sink.Dispatch(motrix::app::SubmitAddTorrentCmd{
                        .torrentBase64 = form.torrentBase64,
                        .selectedIndices = std::move(indices),
                        .totalFiles = form.torrentMeta->files.size(),
                        .outName = form.nameBuffer.data()[0] != '\0' ? form.nameBuffer.data()
                                                                     : form.torrentMeta->name,
                        .dir = form.dirBuffer.data(),
                        .split = form.splitCount,
                    });
                    form.torrentMeta.reset();
                    form.torrentBase64.clear();
                    form.torrentFileSelected.clear();
                    form.nameBuffer[0] = '\0';
                }
            }
        }
    }
}

void RenderAboutModal(UiFrameContext& ctx) {
    const float s = ctx.dpiScale;
    ImGui::SetNextWindowSize(ImVec2(440.0f * s, 0.0f), ImGuiCond_Always);
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    StyleVarGuard winPad(ImGuiStyleVar_WindowPadding, 24.0f * s, 24.0f * s);
    StyleVarGuard winRound(ImGuiStyleVar_WindowRounding, 8.0f * s);
    StyleVarGuard winBorder(ImGuiStyleVar_WindowBorderSize, 1.0f);

    bool open = true;
    PopupModalGuard modal(
        "##about_modal", &open, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoTitleBar);
    if (!modal.IsOpen()) {
        return;
    }

    const ImVec2 winPos = ImGui::GetCursorScreenPos();
    const float availW = ImGui::GetContentRegionAvail().x;
    const float logoW = 88.0f * s;
    const float logoH = 20.0f * s;
    DrawMotrixIcon(MotrixIcon::LogoFull,
                   winPos.x + (availW - logoW) * 0.5f,
                   winPos.y + 4.0f * s,
                   logoW,
                   logoH,
                   ctx.palette.panelTitle,
                   s);
    ImGui::Dummy(ImVec2(0.0f, 34.0f * s));

    {
        FontGuard titleFont(ctx.fonts, FontRole::Semibold, 18.0f);
        const char* heading = "Motrix Native v0.1.0";
        const float hw = ImGui::CalcTextSize(heading).x;
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + std::max(0.0f, (availW - hw) * 0.5f));
        ImGui::TextUnformatted(heading);
    }
    ImGui::Dummy(ImVec2(0.0f, 6.0f * s));
    {
        FontGuard bodyFont(ctx.fonts, FontRole::Regular, 14.0f);
        std::string_view statusKey = "engine.starting";
        switch (ctx.state.engineState) {
            case motrix::app::EngineConnectionState::Starting: statusKey = "engine.starting"; break;
            case motrix::app::EngineConnectionState::Connected: statusKey = "engine.connected"; break;
            case motrix::app::EngineConnectionState::Reconnecting:
                statusKey = "engine.reconnecting";
                break;
            case motrix::app::EngineConnectionState::Offline: statusKey = "engine.offline"; break;
            case motrix::app::EngineConnectionState::PreviewMock: statusKey = "engine.preview"; break;
        }
        const std::string engineText =
            ctx.state.engineInfo.product.empty()
                ? std::format("v1.8.19 Baseline  |  Engine: {}", ctx.tr.Tr(statusKey))
                : std::format("{} {}  |  {}",
                              ctx.state.engineInfo.product,
                              ctx.state.engineInfo.version,
                              ctx.tr.Tr(statusKey));
        const float ew = ImGui::CalcTextSize(engineText.c_str()).x;
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + std::max(0.0f, (availW - ew) * 0.5f));
        ImGui::TextDisabled("%s", engineText.c_str());

        if (!ctx.state.engineError.empty()) {
            const float errW = ImGui::CalcTextSize(ctx.state.engineError.c_str()).x;
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + std::max(0.0f, (availW - errW) * 0.5f));
            StyleColorGuard errCol(ImGuiCol_Text, ctx.palette.progressError);
            ImGui::TextUnformatted(ctx.state.engineError.c_str());
        }
    }
    ImGui::Dummy(ImVec2(0.0f, 16.0f * s));
    const float closeW = 108.0f * s;
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + std::max(0.0f, (availW - closeW) * 0.5f));
    {
        StyleColorGuard secBg(ImGuiCol_Button, ctx.palette.inputBg);
        StyleColorGuard secHov(ImGuiCol_ButtonHovered, ctx.palette.subnavActiveBg);
        StyleColorGuard secAct(ImGuiCol_ButtonActive, ctx.palette.borderSubtle);
        StyleColorGuard secTxt(ImGuiCol_Text, ctx.palette.panelTitle);
        const std::string closeLabel = NullTerm(ctx.tr.Tr("app.close")) + "##about_close";
        if (ImGui::Button(closeLabel.c_str(), ImVec2(closeW, 34.0f * s)) || !open
            || ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
            ctx.sink.Dispatch(motrix::app::CloseModalCmd{});
        }
    }
}

}  // namespace

void RenderModals(UiFrameContext& ctx, AddTaskFormState& addForm) {
    if (ctx.state.activeModal == motrix::app::ActiveModal::AddTask) {
        ImGui::OpenPopup("##add_task_modal");
        RenderAddTaskModal(ctx, addForm);
    } else if (ctx.state.activeModal == motrix::app::ActiveModal::About) {
        ImGui::OpenPopup("##about_modal");
        RenderAboutModal(ctx);
    }
}

}  // namespace motrix::ui

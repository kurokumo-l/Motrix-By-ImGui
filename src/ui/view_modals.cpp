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

    const float s = ctx.dpiScale;
    ImGui::SetNextWindowSize(ImVec2(560.0f * s, 0.0f), ImGuiCond_Always);
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

    ImGui::Dummy(ImVec2(0.0f, 6.0f * s));
    DrawMutedLabel(ctx.palette, ctx.tr.Tr("task.uri_tips"));
    if (justInitialized || ImGui::IsWindowAppearing()) {
        ImGui::SetKeyboardFocusHere();
    }
    constexpr int kUriFlags = ImGuiInputTextFlags_WordWrap | ImGuiInputTextFlags_NoHorizontalScroll;
    ImGui::InputTextMultiline("##uri_input",
                              form.uriBuffer.data(),
                              form.uriBuffer.size(),
                              ImVec2(fieldW, 108.0f * s),
                              kUriFlags);

    ImGui::Dummy(ImVec2(0.0f, 6.0f * s));
    DrawMutedLabel(ctx.palette, ctx.tr.Tr("task.rename_label"));
    ImGui::SetNextItemWidth(fieldW);
    const std::string renameHint = NullTerm(ctx.tr.Tr("task.rename_placeholder"));
    ImGui::InputTextWithHint(
        "##name_input", renameHint.c_str(), form.nameBuffer.data(), form.nameBuffer.size());

    ImGui::Dummy(ImVec2(0.0f, 6.0f * s));
    DrawMutedLabel(ctx.palette, ctx.tr.Tr("task.save_to"));
    ImGui::SetNextItemWidth(fieldW);
    ImGui::InputText("##dir_input", form.dirBuffer.data(), form.dirBuffer.size());

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

void RenderPreferencesPanel(UiFrameContext& ctx, float x, float y, float w, float h, int prefTab) {
    const float s = ctx.dpiScale;
    const float marginX = LayoutMetrics::kPanelMarginWide * s;
    const float headerH = LayoutMetrics::kPanelHeaderHeight * s;

    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(ImVec2(x, y), ImVec2(x + w, y + h), ToAbgrU32(ctx.palette.mainBg));

    const std::string_view tabTitle =
        (prefTab == 0) ? ctx.tr.Tr("prefs.basic")
                       : (prefTab == 1 ? ctx.tr.Tr("prefs.advanced") : ctx.tr.Tr("prefs.lab"));
    {
        FontGuard titleFont(ctx.fonts, FontRole::Regular, 18.0f);
        dl->AddText(ImVec2(x + marginX, y + LayoutMetrics::kPanelHeaderTopPad * s),
                    ToAbgrU32(ctx.palette.panelTitle),
                    tabTitle.data(),
                    tabTitle.data() + tabTitle.size());
    }
    const float borderY = y + headerH - 2.0f * s;
    dl->AddRectFilled(ImVec2(x + marginX, borderY),
                      ImVec2(x + w - marginX, borderY + 2.0f * s),
                      ToAbgrU32(ctx.palette.panelBorder));

    ImGui::SetCursorScreenPos(ImVec2(x + marginX, y + headerH + 24.0f * s));
    ChildGuard prefsBody("##prefs_body", w - marginX * 2.0f, h - headerH - 32.0f * s, 0, 0);
    if (prefsBody.IsVisible()) {
        FontGuard bodyFont(ctx.fonts, FontRole::Regular, 15.0f);
        const std::string_view appLabel = ctx.tr.Tr("prefs.appearance");
        ImGui::TextUnformatted(appLabel.data(), appLabel.data() + appLabel.size());
        const bool isDark = ctx.state.prefs.theme == motrix::domain::ThemePreference::Dark;
        const std::string lightLbl = NullTerm(ctx.tr.Tr("prefs.theme_light")) + "##theme_light";
        const std::string darkLbl = NullTerm(ctx.tr.Tr("prefs.theme_dark")) + "##theme_dark";
        if (ImGui::RadioButton(lightLbl.c_str(), !isDark)) {
            ctx.sink.Dispatch(motrix::app::SetThemeCmd{motrix::domain::ThemePreference::Light});
        }
        ImGui::SameLine();
        if (ImGui::RadioButton(darkLbl.c_str(), isDark)) {
            ctx.sink.Dispatch(motrix::app::SetThemeCmd{motrix::domain::ThemePreference::Dark});
        }
        ImGui::Spacing();
        const std::string_view langLabel = ctx.tr.Tr("prefs.language");
        ImGui::TextUnformatted(langLabel.data(), langLabel.data() + langLabel.size());
        const bool isZh = ctx.state.prefs.locale == motrix::core::Locale::ZhCN;
        if (ImGui::RadioButton("English (en-US)", !isZh)) {
            ctx.sink.Dispatch(motrix::app::SetLocaleCmd{motrix::core::Locale::EnUS});
        }
        ImGui::SameLine();
        if (ImGui::RadioButton("简体中文 (zh-CN)", isZh)) {
            ctx.sink.Dispatch(motrix::app::SetLocaleCmd{motrix::core::Locale::ZhCN});
        }
        ImGui::Spacing();
        const std::string engineDesc =
            ctx.state.engineInfo.product.empty()
                ? std::format("RPC Port: {}", ctx.state.sysConfig.rpcListenPort)
                : std::format("Engine: {} {}  |  RPC Port: {}",
                              ctx.state.engineInfo.product,
                              ctx.state.engineInfo.version,
                              ctx.state.sysConfig.rpcListenPort);
        ImGui::TextDisabled("%s", engineDesc.c_str());
        if (!ctx.state.resolvedEnginePath.empty()) {
            ImGui::TextDisabled("%s", ctx.state.resolvedEnginePath.c_str());
        }
    }
}

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

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

void CopyUtf8ToBuffer(std::string_view src, char* dst, std::size_t cap) noexcept {
    if (cap == 0) {
        return;
    }
    const std::size_t n = std::min(src.size(), cap - 1);
    std::memcpy(dst, src.data(), n);
    dst[n] = '\0';
}

void DrawPrefsFieldLabel(UiFrameContext& ctx, std::string_view text) {
    FontGuard fg(ctx.fonts, FontRole::Medium, 13.5f);
    StyleColorGuard cg(ImGuiCol_Text, ctx.palette.panelTitle);
    ImGui::TextUnformatted(text.data(), text.data() + text.size());
}

namespace {

[[nodiscard]] std::vector<std::string> CollectSelectedTrackerUrls(const PrefsFormState& ps) {
    const auto allOpts = motrix::domain::AllTrackerSourceOptions();
    std::vector<std::string> out;
    for (std::size_t i = 0; i < allOpts.size() && i < ps.trackerSourceSelected.size(); ++i) {
        if (ps.trackerSourceSelected[i]) {
            out.emplace_back(allOpts[i].url);
        }
    }
    return out;
}

void LoadFromSnapshot(PrefsFormState& ps,
                      const motrix::domain::UserPreferences& prefs,
                      const motrix::domain::SystemConfig& sys,
                      std::uint64_t rev) {
    ps.initialized = true;
    ps.loadedRevision = rev;
    ps.themeIndex = static_cast<int>(prefs.theme);
    ps.localeIndex = (prefs.locale == motrix::core::Locale::ZhCN) ? 1 : 0;
    ps.runModeIndex = std::clamp(static_cast<int>(prefs.runMode) - 1, 0, 2);
    CopyUtf8ToBuffer(sys.dir, ps.dirBuffer.data(), ps.dirBuffer.size());
    ps.maxConcurrent = std::clamp(sys.maxConcurrentDownloads, 1, 64);
    ps.maxConnPerServer = std::clamp(sys.maxConnectionPerServer, 1, 64);
    ps.dlLimitKbps = static_cast<int>(sys.maxOverallDownloadLimit / 1024u);
    ps.ulLimitKbps = static_cast<int>(sys.maxOverallUploadLimit / 1024u);
    ps.continueDownloads = sys.continueDownloads;
    ps.newTaskShowDownloading = prefs.newTaskShowDownloading;
    ps.noConfirmBeforeDelete = prefs.noConfirmBeforeDeleteTask;
    ps.followTorrent = sys.followTorrent;
    ps.keepWindowState = prefs.keepWindowState;
    ps.autoHideWindow = prefs.autoHideWindow;
    ps.openAtLogin = prefs.openAtLogin;
    ps.resumeAllWhenAppLaunched = prefs.resumeAllWhenAppLaunched;
    ps.showProgressBar = prefs.showProgressBar;
    ps.taskNotification = prefs.taskNotification;
    ps.traySpeedometer = prefs.traySpeedometer;
    ps.enableUpnp = prefs.enableUpnp;
    ps.autoSyncTracker = prefs.autoSyncTracker;
    ps.useProxy = prefs.useProxy;
    ps.keepSeeding = prefs.keepSeeding;
    ps.protocolMagnet = prefs.protocolMagnet;
    ps.protocolThunder = prefs.protocolThunder;
    ps.protocolMotrix = prefs.protocolMotrix;
    CopyUtf8ToBuffer(sys.allProxy, ps.allProxyBuffer.data(), ps.allProxyBuffer.size());
    CopyUtf8ToBuffer(sys.noProxy, ps.noProxyBuffer.data(), ps.noProxyBuffer.size());
    CopyUtf8ToBuffer(motrix::domain::ConvertTrackerDataToLine(sys.btTracker),
                     ps.btTrackerBuffer.data(),
                     ps.btTrackerBuffer.size());
    ps.loadedSyncTrackerTime = prefs.lastSyncTrackerTime;
    CopyUtf8ToBuffer(sys.userAgent, ps.userAgentBuffer.data(), ps.userAgentBuffer.size());
    CopyUtf8ToBuffer(prefs.engineBinPath, ps.enginePathBuffer.data(), ps.enginePathBuffer.size());
    ps.seedRatio = static_cast<float>(sys.seedRatio);
    ps.seedTime = sys.seedTime;
    ps.rpcPort = static_cast<int>(sys.rpcListenPort);
    CopyUtf8ToBuffer(sys.rpcSecret, ps.rpcSecretBuffer.data(), ps.rpcSecretBuffer.size());
    ps.btPort = sys.listenPort;
    ps.dhtPort = sys.dhtListenPort;

    const auto allOpts = motrix::domain::AllTrackerSourceOptions();
    ps.trackerSourceSelected.assign(allOpts.size(), false);
    for (std::size_t i = 0; i < allOpts.size(); ++i) {
        const auto found =
            std::find(prefs.trackerSource.begin(), prefs.trackerSource.end(), allOpts[i].url);
        if (found != prefs.trackerSource.end()) {
            ps.trackerSourceSelected[i] = true;
        }
    }
}

void RenderBasicTab(UiFrameContext& ctx, PrefsFormState& ps) {
    const float s = ctx.dpiScale;
    const float inputW = std::min(460.0f * s, ImGui::GetContentRegionAvail().x - 16.0f * s);

    DrawPrefsFieldLabel(ctx, ctx.tr.Tr("prefs.appearance"));
    const std::array<std::string_view, 3> themeKeys{
        "prefs.theme_auto",
        "prefs.theme_light",
        "prefs.theme_dark",
    };
    for (int i = 0; i < 3; ++i) {
        if (i > 0) {
            ImGui::SameLine(0.0f, 10.0f * s);
        }
        const bool active = (ps.themeIndex == i);
        StyleColorGuard bg(ImGuiCol_Button, active ? ctx.palette.primary : ctx.palette.subnavBg);
        StyleColorGuard fgCol(ImGuiCol_Text, active ? HexColor(0xFFFFFF) : ctx.palette.panelTitle);
        FontGuard fnt(ctx.fonts, FontRole::Regular, 13.5f);
        if (ImGui::Button(ctx.tr.Tr(themeKeys[static_cast<std::size_t>(i)]).data(),
                          ImVec2(88.0f * s, 30.0f * s))) {
            ps.themeIndex = i;
        }
    }
    ImGui::Dummy(ImVec2(0.0f, 10.0f * s));

    DrawPrefsFieldLabel(ctx, ctx.tr.Tr("prefs.language"));
    ImGui::SetNextItemWidth(200.0f * s);
    static constexpr const char* kLangItems[] = {"English", "简体中文"};
    {
        FontGuard fnt(ctx.fonts, FontRole::Regular, 14.0f);
        ImGui::Combo("##pref_locale", &ps.localeIndex, kLangItems, 2);
    }
    ImGui::Dummy(ImVec2(0.0f, 10.0f * s));

    DrawPrefsFieldLabel(ctx, ctx.tr.Tr("prefs.run_mode"));
    ImGui::SetNextItemWidth(240.0f * s);
    {
        const std::array<const char*, 3> runModeLabels{
            ctx.tr.Tr("prefs.run_mode_standard").data(),
            ctx.tr.Tr("prefs.run_mode_tray").data(),
            ctx.tr.Tr("prefs.run_mode_hide_tray").data(),
        };
        FontGuard fnt(ctx.fonts, FontRole::Regular, 14.0f);
        ImGui::Combo("##pref_run_mode", &ps.runModeIndex, runModeLabels.data(), 3);
    }
    ImGui::Dummy(ImVec2(0.0f, 8.0f * s));

    {
        FontGuard fnt(ctx.fonts, FontRole::Regular, 14.0f);
        ImGui::Checkbox(ctx.tr.Tr("prefs.open_at_login").data(), &ps.openAtLogin);
        ImGui::Checkbox(ctx.tr.Tr("prefs.keep_window_state").data(), &ps.keepWindowState);
        ImGui::Checkbox(ctx.tr.Tr("prefs.auto_hide_window").data(), &ps.autoHideWindow);
        ImGui::Checkbox(ctx.tr.Tr("prefs.resume_all_launch").data(), &ps.resumeAllWhenAppLaunched);
        ImGui::Checkbox(ctx.tr.Tr("prefs.show_progress_bar").data(), &ps.showProgressBar);
        ImGui::Checkbox(ctx.tr.Tr("prefs.task_notification").data(), &ps.taskNotification);
    }
    ImGui::Dummy(ImVec2(0.0f, 10.0f * s));

    DrawPrefsFieldLabel(ctx, ctx.tr.Tr("prefs.default_dir"));
    const float browseW = 84.0f * s;
    ImGui::SetNextItemWidth(std::max(160.0f * s, inputW - browseW - 8.0f * s));
    {
        FontGuard fnt(ctx.fonts, FontRole::Regular, 14.0f);
        ImGui::InputText("##pref_dir", ps.dirBuffer.data(), ps.dirBuffer.size());
    }
    ImGui::SameLine(0.0f, 8.0f * s);
    {
        StyleColorGuard bg(ImGuiCol_Button, ctx.palette.subnavBg);
        StyleColorGuard fgCol(ImGuiCol_Text, ctx.palette.panelTitle);
        FontGuard fnt(ctx.fonts, FontRole::Regular, 13.0f);
        if (ImGui::Button(ctx.tr.Tr("task.browse").data(), ImVec2(browseW, 30.0f * s))) {
            if (auto chosen = ctx.host.PickFolder(ps.dirBuffer.data())) {
                CopyUtf8ToBuffer(*chosen, ps.dirBuffer.data(), ps.dirBuffer.size());
            }
        }
    }
    ImGui::Dummy(ImVec2(0.0f, 10.0f * s));

    DrawPrefsFieldLabel(ctx, ctx.tr.Tr("prefs.concurrency"));
    ImGui::SetNextItemWidth(inputW);
    {
        FontGuard fnt(ctx.fonts, FontRole::Regular, 14.0f);
        ImGui::SliderInt("##pref_conc", &ps.maxConcurrent, 1, 32);
    }
    ImGui::Dummy(ImVec2(0.0f, 8.0f * s));

    DrawPrefsFieldLabel(ctx, ctx.tr.Tr("prefs.split"));
    ImGui::SetNextItemWidth(inputW);
    {
        FontGuard fnt(ctx.fonts, FontRole::Regular, 14.0f);
        ImGui::SliderInt("##pref_conn", &ps.maxConnPerServer, 1, 64);
    }
    ImGui::Dummy(ImVec2(0.0f, 8.0f * s));

    DrawPrefsFieldLabel(ctx, ctx.tr.Tr("prefs.max_dl_limit"));
    ImGui::SetNextItemWidth(200.0f * s);
    {
        FontGuard fnt(ctx.fonts, FontRole::Regular, 14.0f);
        if (ImGui::InputInt("##pref_dl_limit", &ps.dlLimitKbps, 100, 1024)) {
            ps.dlLimitKbps = std::max(0, ps.dlLimitKbps);
        }
    }
    ImGui::Dummy(ImVec2(0.0f, 6.0f * s));

    DrawPrefsFieldLabel(ctx, ctx.tr.Tr("prefs.max_ul_limit"));
    ImGui::SetNextItemWidth(200.0f * s);
    {
        FontGuard fnt(ctx.fonts, FontRole::Regular, 14.0f);
        if (ImGui::InputInt("##pref_ul_limit", &ps.ulLimitKbps, 100, 1024)) {
            ps.ulLimitKbps = std::max(0, ps.ulLimitKbps);
        }
    }
    ImGui::Dummy(ImVec2(0.0f, 10.0f * s));

    FontGuard fnt(ctx.fonts, FontRole::Regular, 14.0f);
    ImGui::Checkbox(ctx.tr.Tr("prefs.continue").data(), &ps.continueDownloads);
    ImGui::Checkbox(ctx.tr.Tr("prefs.new_task_show").data(), &ps.newTaskShowDownloading);
    ImGui::Checkbox(ctx.tr.Tr("prefs.no_confirm_del").data(), &ps.noConfirmBeforeDelete);
    ImGui::Checkbox(ctx.tr.Tr("prefs.follow_torrent").data(), &ps.followTorrent);
}

void ApplyDraftToCommands(const UiFrameContext& ctx, const PrefsFormState& ps) {
    auto newPrefs = ctx.state.prefs;
    newPrefs.theme = static_cast<motrix::domain::ThemePreference>(std::clamp(ps.themeIndex, 0, 2));
    newPrefs.locale = (ps.localeIndex == 1) ? motrix::core::Locale::ZhCN : motrix::core::Locale::EnUS;
    newPrefs.runMode = static_cast<motrix::domain::RunMode>(std::clamp(ps.runModeIndex, 0, 2) + 1);
    newPrefs.keepWindowState = ps.keepWindowState;
    newPrefs.autoHideWindow = ps.autoHideWindow;
    newPrefs.openAtLogin = ps.openAtLogin;
    newPrefs.resumeAllWhenAppLaunched = ps.resumeAllWhenAppLaunched;
    newPrefs.showProgressBar = ps.showProgressBar;
    newPrefs.taskNotification = ps.taskNotification;
    newPrefs.traySpeedometer = ps.traySpeedometer;
    newPrefs.newTaskShowDownloading = ps.newTaskShowDownloading;
    newPrefs.noConfirmBeforeDeleteTask = ps.noConfirmBeforeDelete;
    newPrefs.enableUpnp = ps.enableUpnp;
    newPrefs.autoSyncTracker = ps.autoSyncTracker;
    newPrefs.useProxy = ps.useProxy;
    newPrefs.keepSeeding = ps.keepSeeding;
    newPrefs.protocolMagnet = ps.protocolMagnet;
    newPrefs.protocolThunder = ps.protocolThunder;
    newPrefs.protocolMotrix = ps.protocolMotrix;
    newPrefs.trackerSource = CollectSelectedTrackerUrls(ps);
    newPrefs.engineBinPath = ps.enginePathBuffer.data();

    auto newSys = ctx.state.sysConfig;
    newSys.dir = ps.dirBuffer.data();
    newSys.maxConcurrentDownloads = std::clamp(ps.maxConcurrent, 1, 64);
    newSys.maxConnectionPerServer = std::clamp(ps.maxConnPerServer, 1, 64);
    newSys.split = newSys.maxConnectionPerServer;
    newSys.maxOverallDownloadLimit = static_cast<std::uint64_t>(std::max(0, ps.dlLimitKbps)) * 1024u;
    newSys.maxOverallUploadLimit = static_cast<std::uint64_t>(std::max(0, ps.ulLimitKbps)) * 1024u;
    newSys.continueDownloads = ps.continueDownloads;
    newSys.followTorrent = ps.followTorrent;
    newSys.allProxy = ps.useProxy ? std::string(ps.allProxyBuffer.data()) : std::string{};
    newSys.noProxy = ps.noProxyBuffer.data();
    newSys.btTracker = motrix::domain::ConvertTrackerDataToComma(ps.btTrackerBuffer.data());
    newSys.userAgent = ps.userAgentBuffer.data();
    newSys.seedRatio = static_cast<double>(ps.seedRatio);
    newSys.seedTime = ps.seedTime;
    newSys.rpcListenPort = static_cast<std::uint16_t>(std::clamp(ps.rpcPort, 1024, 65535));
    newSys.rpcSecret = ps.rpcSecretBuffer.data();
    newSys.listenPort = std::clamp(ps.btPort, 1024, 65535);
    newSys.dhtListenPort = std::clamp(ps.dhtPort, 1024, 65535);

    ctx.sink.Dispatch(motrix::app::SavePreferencesCmd{std::move(newPrefs), std::move(newSys), false});
}

}  // namespace

void RenderPreferencesView(
    UiFrameContext& ctx, float x, float y, float w, float h, int prefTab, PrefsFormState& ps) {
    if (!ps.initialized) {
        LoadFromSnapshot(ps, ctx.state.prefs, ctx.state.sysConfig, ctx.state.revision);
    } else if (ctx.state.prefs.lastSyncTrackerTime != ps.loadedSyncTrackerTime) {
        ps.loadedSyncTrackerTime = ctx.state.prefs.lastSyncTrackerTime;
        CopyUtf8ToBuffer(motrix::domain::ConvertTrackerDataToLine(ctx.state.sysConfig.btTracker),
                         ps.btTrackerBuffer.data(),
                         ps.btTrackerBuffer.size());
    }

    const float s = ctx.dpiScale;
    const float marginX = (ctx.viewportW >= LayoutMetrics::kAsideHideThreshold * s)
                              ? LayoutMetrics::kPanelMarginWide * s
                              : LayoutMetrics::kPanelMarginNarrow * s;
    const float headerH = LayoutMetrics::kPanelHeaderHeight * s;
    const float footerH = 60.0f * s;

    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(ImVec2(x, y), ImVec2(x + w, y + h), ToAbgrU32(ctx.palette.mainBg));

    {
        FontGuard tf(ctx.fonts, FontRole::Regular, 18.0f);
        const std::string_view tabTitle =
            (prefTab == 0) ? ctx.tr.Tr("prefs.basic")
                           : ((prefTab == 1) ? ctx.tr.Tr("prefs.advanced") : ctx.tr.Tr("prefs.lab"));
        dl->AddText(ImVec2(x + marginX, y + LayoutMetrics::kPanelHeaderTopPad * s),
                    ToAbgrU32(ctx.palette.panelTitle),
                    tabTitle.data(),
                    tabTitle.data() + tabTitle.size());
    }
    dl->AddLine(ImVec2(x + marginX, y + headerH),
                ImVec2(x + w - marginX, y + headerH),
                ToAbgrU32(ctx.palette.panelBorder),
                1.0f);

    const float bodyY = y + headerH + 16.0f * s;
    const float bodyH = std::max(80.0f * s, h - headerH - footerH - 16.0f * s);
    ImGui::SetCursorScreenPos(ImVec2(x + marginX, bodyY));
    {
        ChildGuard scroll("##prefs_scroll",
                          std::max(100.0f * s, w - marginX * 2.0f),
                          bodyH,
                          ImGuiChildFlags_None,
                          ImGuiWindowFlags_None);
        if (scroll.IsVisible()) {
            if (prefTab == 0) {
                RenderBasicTab(ctx, ps);
            } else if (prefTab == 1) {
                RenderPrefsAdvancedTab(ctx, ps);
            } else {
                RenderPrefsLabTab(ctx, ps);
            }
        }
    }

    const float footY = y + h - footerH;
    dl->AddLine(ImVec2(x + marginX, footY),
                ImVec2(x + w - marginX, footY),
                ToAbgrU32(ctx.palette.borderSubtle),
                1.0f);

    const float btnH = 34.0f * s;
    const float btnY = footY + (footerH - btnH) * 0.5f;
    ImGui::SetCursorScreenPos(ImVec2(x + marginX, btnY));
    {
        StyleColorGuard bg(ImGuiCol_Button, ctx.palette.primary);
        StyleColorGuard fg(ImGuiCol_Text, HexColor(0xFFFFFF));
        FontGuard fnt(ctx.fonts, FontRole::Medium, 14.0f);
        if (ImGui::Button(ctx.tr.Tr("prefs.save_apply").data(), ImVec2(128.0f * s, btnH))) {
            ApplyDraftToCommands(ctx, ps);
        }
    }
    ImGui::SameLine(0.0f, 12.0f * s);
    {
        StyleColorGuard bg(ImGuiCol_Button, ctx.palette.subnavBg);
        StyleColorGuard fg(ImGuiCol_Text, ctx.palette.panelTitle);
        FontGuard fnt(ctx.fonts, FontRole::Regular, 14.0f);
        if (ImGui::Button(ctx.tr.Tr("prefs.reset").data(), ImVec2(96.0f * s, btnH))) {
            LoadFromSnapshot(ps,
                             motrix::domain::DefaultUserPreferences(),
                             motrix::domain::DefaultSystemConfig(),
                             ctx.state.revision);
            if (!ctx.state.sysConfig.dir.empty()) {
                CopyUtf8ToBuffer(ctx.state.sysConfig.dir, ps.dirBuffer.data(), ps.dirBuffer.size());
            }
        }
    }
}

}  // namespace motrix::ui

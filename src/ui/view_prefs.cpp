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

constexpr std::array<std::string_view, 5> kLogLevels{"debug", "info", "notice", "warn", "error"};

int FindLogLevelIndex(std::string_view lvl) noexcept {
    for (int i = 0; i < static_cast<int>(kLogLevels.size()); ++i) {
        if (kLogLevels[static_cast<std::size_t>(i)] == lvl) {
            return i;
        }
    }
    return 3;  // warn
}

void LoadPrefsFromSnapshot(const motrix::app::AppStateSnapshot& st, PrefsFormState& ps) {
    const auto& pr = st.prefs;
    const auto& sys = st.sysConfig;

    ps.themeIndex = static_cast<int>(pr.theme);
    ps.localeIndex = (pr.locale == motrix::core::Locale::ZhCN) ? 1 : 0;
    ps.runModeIndex = std::clamp(static_cast<int>(pr.runMode) - 1, 0, 2);

    ps.hideAppMenu = pr.hideAppMenu;
    ps.keepWindowState = pr.keepWindowState;
    ps.autoHideWindow = pr.autoHideWindow;
    ps.openAtLogin = pr.openAtLogin;
    ps.resumeAllWhenAppLaunched = pr.resumeAllWhenAppLaunched;
    ps.showProgressBar = pr.showProgressBar;
    ps.taskNotification = pr.taskNotification;
    ps.traySpeedometer = pr.traySpeedometer;
    ps.newTaskShowDownloading = pr.newTaskShowDownloading;
    ps.noConfirmBeforeDelete = pr.noConfirmBeforeDeleteTask;
    ps.enableUpnp = pr.enableUpnp;
    ps.autoSyncTracker = pr.autoSyncTracker;
    ps.useProxy = pr.useProxy;
    ps.keepSeeding = pr.keepSeeding;
    ps.autoCheckUpdate = pr.autoCheckUpdate;
    ps.protocolMagnet = pr.protocolMagnet;
    ps.protocolThunder = pr.protocolThunder;
    ps.protocolMotrix = pr.protocolMotrix;
    ps.logLevelIndex = FindLogLevelIndex(pr.logLevel);

    CopyUtf8ToBuffer(pr.engineBinPath, ps.enginePathBuffer.data(), ps.enginePathBuffer.size());

    const auto allSources = motrix::domain::AllTrackerSourceOptions();
    ps.trackerSourceSelected.assign(allSources.size(), false);
    for (const auto& savedUrl : pr.trackerSource) {
        for (std::size_t i = 0; i < allSources.size(); ++i) {
            if (allSources[i].url == savedUrl) {
                ps.trackerSourceSelected[i] = true;
            }
        }
    }
    ps.loadedSyncTrackerTime = pr.lastSyncTrackerTime;

    CopyUtf8ToBuffer(sys.dir, ps.dirBuffer.data(), ps.dirBuffer.size());
    ps.maxConcurrent = std::clamp(sys.maxConcurrentDownloads, 1, 64);
    ps.maxConnPerServer = std::clamp(sys.maxConnectionPerServer, 1, 128);

    const std::uint64_t dlKb = sys.maxOverallDownloadLimit / 1024ULL;
    ps.dlUnitMb = (dlKb > 0 && dlKb % 1024ULL == 0);
    ps.dlLimitKbps = static_cast<int>(ps.dlUnitMb ? (dlKb / 1024ULL) : dlKb);

    const std::uint64_t ulKb = sys.maxOverallUploadLimit / 1024ULL;
    ps.ulUnitMb = (ulKb > 0 && ulKb % 1024ULL == 0);
    ps.ulLimitKbps = static_cast<int>(ps.ulUnitMb ? (ulKb / 1024ULL) : ulKb);

    ps.continueDownloads = sys.continueDownloads;
    ps.followTorrent = sys.followTorrent;
    ps.btSaveMetadata = sys.btSaveMetadata;
    ps.btAutoDownloadContent = !sys.pauseMetadata;
    ps.btForceEncryption = sys.btForceEncryption;
    ps.seedRatio = static_cast<float>(sys.seedRatio);
    ps.seedTime = sys.seedTime;
    ps.rpcPort = static_cast<int>(sys.rpcListenPort);
    CopyUtf8ToBuffer(sys.rpcSecret, ps.rpcSecretBuffer.data(), ps.rpcSecretBuffer.size());
    ps.btPort = sys.listenPort;
    ps.dhtPort = sys.dhtListenPort;

    CopyUtf8ToBuffer(sys.allProxy, ps.allProxyBuffer.data(), ps.allProxyBuffer.size());
    CopyUtf8ToBuffer(sys.noProxy, ps.noProxyBuffer.data(), ps.noProxyBuffer.size());
    const std::string lineTrackers = motrix::domain::ConvertTrackerDataToLine(sys.btTracker);
    CopyUtf8ToBuffer(lineTrackers, ps.btTrackerBuffer.data(), ps.btTrackerBuffer.size());
    CopyUtf8ToBuffer(sys.userAgent, ps.userAgentBuffer.data(), ps.userAgentBuffer.size());

    ps.loadedRevision = st.revision;
    ps.initialized = true;
}

void ApplyPrefsDraft(UiFrameContext& ctx, PrefsFormState& ps) {
    motrix::domain::UserPreferences pr = ctx.state.prefs;
    motrix::domain::SystemConfig sys = ctx.state.sysConfig;

    pr.theme = static_cast<motrix::domain::ThemePreference>(std::clamp(ps.themeIndex, 0, 2));
    pr.locale = (ps.localeIndex == 1) ? motrix::core::Locale::ZhCN : motrix::core::Locale::EnUS;
    pr.runMode = static_cast<motrix::domain::RunMode>(std::clamp(ps.runModeIndex, 0, 2) + 1);
    pr.hideAppMenu = ps.hideAppMenu;
    pr.keepWindowState = ps.keepWindowState;
    pr.autoHideWindow = ps.autoHideWindow;
    pr.openAtLogin = ps.openAtLogin;
    pr.resumeAllWhenAppLaunched = ps.resumeAllWhenAppLaunched;
    pr.showProgressBar = ps.showProgressBar;
    pr.taskNotification = ps.taskNotification;
    pr.traySpeedometer = ps.traySpeedometer;
    pr.newTaskShowDownloading = ps.newTaskShowDownloading;
    pr.noConfirmBeforeDeleteTask = ps.noConfirmBeforeDelete;
    pr.enableUpnp = ps.enableUpnp;
    pr.autoSyncTracker = ps.autoSyncTracker;
    pr.useProxy = ps.useProxy;
    pr.keepSeeding = ps.keepSeeding;
    pr.autoCheckUpdate = ps.autoCheckUpdate;
    pr.protocolMagnet = ps.protocolMagnet;
    pr.protocolThunder = ps.protocolThunder;
    pr.protocolMotrix = ps.protocolMotrix;
    pr.engineBinPath = ps.enginePathBuffer.data();
    pr.logLevel = std::string(kLogLevels[static_cast<std::size_t>(std::clamp(ps.logLevelIndex, 0, 4))]);

    const auto allSources = motrix::domain::AllTrackerSourceOptions();
    pr.trackerSource.clear();
    for (std::size_t i = 0; i < allSources.size() && i < ps.trackerSourceSelected.size(); ++i) {
        if (ps.trackerSourceSelected[i]) {
            pr.trackerSource.emplace_back(allSources[i].url);
        }
    }

    sys.dir = ps.dirBuffer.data();
    sys.maxConcurrentDownloads = std::clamp(ps.maxConcurrent, 1, 64);
    sys.maxConnectionPerServer = std::clamp(ps.maxConnPerServer, 1, 128);
    sys.split = sys.maxConnectionPerServer;

    const std::uint64_t dlMul = ps.dlUnitMb ? (1024ULL * 1024ULL) : 1024ULL;
    sys.maxOverallDownloadLimit = static_cast<std::uint64_t>(std::max(0, ps.dlLimitKbps)) * dlMul;
    const std::uint64_t ulMul = ps.ulUnitMb ? (1024ULL * 1024ULL) : 1024ULL;
    sys.maxOverallUploadLimit = static_cast<std::uint64_t>(std::max(0, ps.ulLimitKbps)) * ulMul;

    sys.continueDownloads = ps.continueDownloads;
    sys.followTorrent = ps.followTorrent;
    sys.btSaveMetadata = ps.btSaveMetadata;
    sys.pauseMetadata = !ps.btAutoDownloadContent;
    sys.btForceEncryption = ps.btForceEncryption;
    sys.seedRatio = static_cast<double>(std::clamp(ps.seedRatio, 0.0f, 100.0f));
    sys.seedTime = std::clamp(ps.seedTime, 0, 525600);
    sys.rpcListenPort = static_cast<std::uint16_t>(std::clamp(ps.rpcPort, 1024, 65535));
    sys.rpcSecret = ps.rpcSecretBuffer.data();
    sys.listenPort = std::clamp(ps.btPort, 1024, 65535);
    sys.dhtListenPort = std::clamp(ps.dhtPort, 1024, 65535);
    sys.allProxy = ps.allProxyBuffer.data();
    sys.noProxy = ps.noProxyBuffer.data();
    sys.btTracker = motrix::domain::ConvertTrackerDataToComma(ps.btTrackerBuffer.data());
    sys.userAgent = ps.userAgentBuffer.data();

    const bool needRestart = (pr.engineBinPath != ctx.state.prefs.engineBinPath)
                             || (pr.enableUpnp != ctx.state.prefs.enableUpnp)
                             || (sys.rpcListenPort != ctx.state.sysConfig.rpcListenPort)
                             || (sys.rpcSecret != ctx.state.sysConfig.rpcSecret)
                             || (sys.listenPort != ctx.state.sysConfig.listenPort)
                             || (sys.dhtListenPort != ctx.state.sysConfig.dhtListenPort)
                             || (sys.btSaveMetadata != ctx.state.sysConfig.btSaveMetadata)
                             || (sys.btForceEncryption != ctx.state.sysConfig.btForceEncryption);

    ctx.sink.Dispatch(motrix::app::SavePreferencesCmd{std::move(pr), std::move(sys), needRestart});
    ps.initialized = false;
}

}  // namespace

void CopyUtf8ToBuffer(std::string_view src, char* dst, std::size_t cap) noexcept {
    if (!dst || cap == 0) {
        return;
    }
    const std::size_t n = std::min(src.size(), cap - 1);
    if (n > 0) {
        std::memcpy(dst, src.data(), n);
    }
    dst[n] = '\0';
}

void DrawPrefsFieldLabel(UiFrameContext& ctx, std::string_view text) {
    FontGuard fg(ctx.fonts, FontRole::Regular, 13.5f);
    StyleColorGuard col(ImGuiCol_Text, ctx.palette.subLabelText);
    ImGui::TextUnformatted(text.data(), text.data() + text.size());
}

void DrawPrefsFormRow(UiFrameContext& ctx,
                      std::string_view label,
                      float alignH,
                      const std::function<void(float, float)>& body) {
    const float s = ctx.dpiScale;
    const ImVec2 startPos = ImGui::GetCursorScreenPos();
    const float availW = ImGui::GetContentRegionAvail().x;
    const float labelColW = std::clamp(availW * 0.22f, 108.0f * s, 146.0f * s);
    const float colGap = 16.0f * s;
    const float bodyX = startPos.x + labelColW + colGap;
    const float bodyW = std::max(140.0f * s, availW - labelColW - colGap);

    if (!label.empty()) {
        FontGuard fg(ctx.fonts, FontRole::Regular, 13.5f);
        const ImVec2 sz = ImGui::CalcTextSize(label.data(), label.data() + label.size());
        ImDrawList* dl = ImGui::GetWindowDrawList();
        if (sz.x <= labelColW) {
            const float lblY = startPos.y + std::max(0.0f, (alignH - sz.y) * 0.5f);
            dl->AddText(ImVec2(startPos.x + labelColW - sz.x, lblY),
                        ToAbgrU32(ctx.palette.panelTitle),
                        label.data(),
                        label.data() + label.size());
        } else {
            dl->AddText(ImGui::GetFont(),
                        ImGui::GetFontSize(),
                        ImVec2(startPos.x, startPos.y + 4.0f * s),
                        ToAbgrU32(ctx.palette.panelTitle),
                        label.data(),
                        label.data() + label.size(),
                        labelColW);
        }
    }

    ImGui::SetCursorScreenPos(ImVec2(bodyX, startPos.y));
    ImGui::BeginGroup();
    if (body) {
        body(bodyX, bodyW);
    }
    ImGui::EndGroup();
    const float nextY = ImGui::GetItemRectMax().y + 20.0f * s;
    ImGui::SetCursorScreenPos(ImVec2(startPos.x, nextY));
    ImGui::Dummy(ImVec2(0.0f, 0.0f));
}

void RenderPreferencesView(
    UiFrameContext& ctx, float x, float y, float w, float h, int prefTab, PrefsFormState& prefsState) {
    if (!prefsState.initialized) {
        LoadPrefsFromSnapshot(ctx.state, prefsState);
    } else if (ctx.state.prefs.lastSyncTrackerTime != prefsState.loadedSyncTrackerTime) {
        prefsState.loadedSyncTrackerTime = ctx.state.prefs.lastSyncTrackerTime;
        const std::string lineTrackers =
            motrix::domain::ConvertTrackerDataToLine(ctx.state.sysConfig.btTracker);
        CopyUtf8ToBuffer(
            lineTrackers, prefsState.btTrackerBuffer.data(), prefsState.btTrackerBuffer.size());
    }

    const float s = ctx.dpiScale;
    const bool wide = ctx.viewportW >= LayoutMetrics::kSubnavHideThreshold * s;
    const float marginX =
        (wide ? LayoutMetrics::kPanelMarginWide : LayoutMetrics::kPanelMarginNarrow) * s;
    const float headerH = LayoutMetrics::kPanelHeaderHeight * s;

    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(ImVec2(x, y), ImVec2(x + w, y + h), ToAbgrU32(ctx.palette.mainBg));

    const std::string_view titleKey =
        (prefTab == 0) ? "prefs.basic" : ((prefTab == 1) ? "prefs.advanced" : "prefs.lab");
    {
        FontGuard titleFont(ctx.fonts, FontRole::Regular, 18.0f);
        const std::string_view title = ctx.tr.Tr(titleKey);
        dl->AddText(ImVec2(x + marginX, y + LayoutMetrics::kPanelHeaderTopPad * s),
                    ToAbgrU32(ctx.palette.panelTitle),
                    title.data(),
                    title.data() + title.size());
    }
    const float borderY = y + headerH - 2.0f * s;
    dl->AddRectFilled(ImVec2(x + marginX, borderY),
                      ImVec2(x + w - marginX, borderY + 2.0f * s),
                      ToAbgrU32(ctx.palette.panelBorder));

    const bool hasActionsBar = (prefTab == 0 || prefTab == 1);
    const float actionsH = hasActionsBar ? 68.0f * s : 0.0f;
    const float scrollY = y + headerH;
    const float scrollH = std::max(1.0f, h - headerH - actionsH);

    ImGui::SetCursorScreenPos(ImVec2(x, scrollY));
    {
        StyleVarGuard childPad(ImGuiStyleVar_WindowPadding, marginX, 22.0f * s);
        ChildGuard scrollChild("##prefs_scroll_area",
                               w,
                               scrollH,
                               ImGuiChildFlags_AlwaysUseWindowPadding,
                               ImGuiWindowFlags_None);
        if (scrollChild.IsVisible()) {
            if (prefTab == 0) {
                RenderPrefsBasicTab(ctx, prefsState);
            } else if (prefTab == 1) {
                RenderPrefsAdvancedTab(ctx, prefsState);
            } else {
                RenderPrefsLabTab(ctx, prefsState);
            }
            ImGui::Dummy(ImVec2(0.0f, 18.0f * s));
        }
    }

    if (hasActionsBar) {
        const float barY = y + h - actionsH;
        dl->AddRectFilled(ImVec2(x, barY), ImVec2(x + w, y + h), ToAbgrU32(ctx.palette.mainBg));
        const float btnH = 32.0f * s;
        const float btnY = barY + (actionsH - btnH) * 0.5f;
        float saveW = 96.0f * s;
        float discW = 66.0f * s;
        {
            FontGuard bf(ctx.fonts, FontRole::Medium, 13.5f);
            const std::string_view sl = ctx.tr.Tr("prefs.save_apply");
            const std::string_view dsl = ctx.tr.Tr("common.discard");
            saveW =
                std::max(90.0f * s, ImGui::CalcTextSize(sl.data(), sl.data() + sl.size()).x + 28.0f * s);
            discW = std::max(64.0f * s,
                             ImGui::CalcTextSize(dsl.data(), dsl.data() + dsl.size()).x + 28.0f * s);
        }
        ImGui::SetCursorScreenPos(ImVec2(x + marginX, btnY));
        if (ElPrimaryButton(
                "##prefs_save", ctx.tr.Tr("prefs.save_apply"), saveW, btnH, ctx.palette, ctx.fonts, s)) {
            ApplyPrefsDraft(ctx, prefsState);
        }
        ImGui::SetCursorScreenPos(ImVec2(x + marginX + saveW + 12.0f * s, btnY));
        if (ElSecondaryButton("##prefs_discard",
                              ctx.tr.Tr("common.discard"),
                              discW,
                              btnH,
                              ctx.palette,
                              ctx.fonts,
                              s)) {
            LoadPrefsFromSnapshot(ctx.state, prefsState);
        }
    }

    ImGui::SetCursorScreenPos(ImVec2(x, y));
    ImGui::Dummy(ImVec2(0.0f, 0.0f));
}

}  // namespace motrix::ui

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

constexpr std::array<std::string_view, 4> kLogLevels{"debug", "info", "warn", "error"};
constexpr std::array<std::string_view, 3> kBtEncryptionOpts{"preferred", "required", "disabled"};
constexpr std::array<std::string_view, 3> kBtTransportOpts{"both", "tcp", "utp"};
constexpr std::array<std::string_view, 4> kFileAllocOpts{"none", "prealloc", "falloc", "trunc"};
constexpr std::array<std::string_view, 2> kMediaModeOpts{"auto", "file"};
constexpr std::array<std::string_view, 2> kMediaFormatOpts{"mp4", "mkv"};

int FindOptIndex(std::span<const std::string_view> opts,
                 std::string_view val,
                 int fallback = 0) noexcept {
    for (int i = 0; i < static_cast<int>(opts.size()); ++i) {
        if (opts[static_cast<std::size_t>(i)] == val) {
            return i;
        }
    }
    return fallback;
}

void LoadSpeedLimit(std::uint64_t bytesPerSec, bool& unitMb, int& val) noexcept {
    const std::uint64_t kb = bytesPerSec / 1024ULL;
    unitMb = (kb > 0 && kb % 1024ULL == 0);
    val = static_cast<int>(unitMb ? (kb / 1024ULL) : kb);
}

void LoadSysPrefsFromSnapshot(const motrix::domain::SystemConfig& sys, PrefsFormState& ps) {
    CopyUtf8ToBuffer(sys.dir, ps.dirBuffer.data(), ps.dirBuffer.size());
    ps.maxConcurrent = std::clamp(sys.maxConcurrentDownloads, 1, 64);
    ps.maxConnPerServer = std::clamp(sys.maxConnectionPerServer, 1, 128);
    LoadSpeedLimit(sys.maxOverallDownloadLimit, ps.dlUnitMb, ps.dlLimitKbps);
    LoadSpeedLimit(sys.maxOverallUploadLimit, ps.ulUnitMb, ps.ulLimitKbps);

    ps.continueDownloads = sys.continueDownloads;
    ps.followTorrent = sys.followTorrent;
    ps.rpcSaveUploadMetadata = sys.rpcSaveUploadMetadata;
    ps.btAutoDownloadContent = !sys.pauseMetadata;
    ps.btEncryptionIdx = FindOptIndex(kBtEncryptionOpts, sys.btEncryption, 0);
    ps.btTransportIdx = FindOptIndex(kBtTransportOpts, sys.btTransport, 0);
    ps.fileAllocIdx = FindOptIndex(kFileAllocOpts, sys.fileAllocation, 0);
    ps.autoFileRenaming = sys.autoFileRenaming;
    ps.allowOverwrite = sys.allowOverwrite;
    ps.checkIntegrity = sys.checkIntegrity;
    ps.seedRatio = static_cast<float>(sys.seedRatio);
    ps.seedTime = sys.seedTime;
    ps.rpcPort = static_cast<int>(sys.rpcListenPort);
    CopyUtf8ToBuffer(sys.rpcSecret, ps.rpcSecretBuffer.data(), ps.rpcSecretBuffer.size());
    ps.btPort = sys.listenPort;
    ps.dhtPort = sys.dhtListenPort;
    ps.enableDht = sys.enableDht;
    ps.enablePeerExchange = sys.enablePeerExchange;
    ps.btEnableLpd = sys.btEnableLpd;
    ps.btAnonymousMode = sys.btAnonymousMode;
    ps.btMaxPeers = std::clamp(sys.btMaxPeers, 0, 1000);
    ps.checkCertificate = sys.checkCertificate;
    ps.disableIpv6 = sys.disableIpv6;
    ps.remoteTime = sys.remoteTime;
    ps.mediaModeIdx = FindOptIndex(kMediaModeOpts, sys.mediaMode, 0);
    ps.mediaFormatIdx = FindOptIndex(kMediaFormatOpts, sys.mediaFormat, 0);
    ps.btFirstLastPieceFirst = sys.btFirstLastPieceFirst;
    ps.detachShareOnly = sys.detachShareOnly;

    CopyUtf8ToBuffer(sys.allProxy, ps.allProxyBuffer.data(), ps.allProxyBuffer.size());
    CopyUtf8ToBuffer(sys.noProxy, ps.noProxyBuffer.data(), ps.noProxyBuffer.size());
    const std::string lineTrackers = motrix::domain::ConvertTrackerDataToLine(sys.btTracker);
    CopyUtf8ToBuffer(lineTrackers, ps.btTrackerBuffer.data(), ps.btTrackerBuffer.size());
    CopyUtf8ToBuffer(sys.userAgent, ps.userAgentBuffer.data(), ps.userAgentBuffer.size());
}

void ApplySysPrefsDraft(const PrefsFormState& ps, motrix::domain::SystemConfig& sys) {
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
    sys.rpcSaveUploadMetadata = ps.rpcSaveUploadMetadata;
    sys.btSaveMetadata = ps.rpcSaveUploadMetadata;
    sys.pauseMetadata = !ps.btAutoDownloadContent;
    sys.btEncryption =
        std::string(kBtEncryptionOpts[static_cast<std::size_t>(std::clamp(ps.btEncryptionIdx, 0, 2))]);
    sys.btForceEncryption = (sys.btEncryption == "required");
    sys.btTransport =
        std::string(kBtTransportOpts[static_cast<std::size_t>(std::clamp(ps.btTransportIdx, 0, 2))]);
    sys.fileAllocation =
        std::string(kFileAllocOpts[static_cast<std::size_t>(std::clamp(ps.fileAllocIdx, 0, 3))]);
    sys.autoFileRenaming = ps.autoFileRenaming;
    sys.allowOverwrite = ps.allowOverwrite;
    sys.checkIntegrity = ps.checkIntegrity;
    sys.seedRatio = static_cast<double>(std::clamp(ps.seedRatio, 0.0f, 100.0f));
    sys.seedTime = std::clamp(ps.seedTime, 0, 525600);
    sys.rpcListenPort = static_cast<std::uint16_t>(std::clamp(ps.rpcPort, 1024, 65535));
    sys.rpcSecret = ps.rpcSecretBuffer.data();
    sys.listenPort = std::clamp(ps.btPort, 1024, 65535);
    sys.dhtListenPort = std::clamp(ps.dhtPort, 1024, 65535);
    sys.enableDht = ps.enableDht;
    sys.enablePeerExchange = ps.enablePeerExchange;
    sys.btEnableLpd = ps.btEnableLpd;
    sys.btAnonymousMode = ps.btAnonymousMode;
    sys.btMaxPeers = std::clamp(ps.btMaxPeers, 0, 1000);
    sys.checkCertificate = ps.checkCertificate;
    sys.disableIpv6 = ps.disableIpv6;
    sys.remoteTime = ps.remoteTime;
    sys.mediaMode =
        std::string(kMediaModeOpts[static_cast<std::size_t>(std::clamp(ps.mediaModeIdx, 0, 1))]);
    sys.mediaFormat =
        std::string(kMediaFormatOpts[static_cast<std::size_t>(std::clamp(ps.mediaFormatIdx, 0, 1))]);
    sys.btFirstLastPieceFirst = ps.btFirstLastPieceFirst;
    sys.detachShareOnly = ps.detachShareOnly;
    sys.allProxy = ps.allProxyBuffer.data();
    sys.noProxy = ps.noProxyBuffer.data();
    sys.btTracker = motrix::domain::ConvertTrackerDataToComma(ps.btTrackerBuffer.data());
    sys.userAgent = ps.userAgentBuffer.data();
}

void SyncPrefsFormState(const UiFrameContext& ctx, PrefsFormState& ps) {
    const auto& pr = ctx.state.prefs;
    const auto& sys = ctx.state.sysConfig;
    if (!ps.initialized || sys.rpcSecret != ps.lastSnapRpcSecret) {
        LoadPrefsFromSnapshot(ctx.state, ps);
        return;
    }
    auto syncInt = [](int snapVal, int& lastSnap, int& formVal) {
        if (snapVal != lastSnap) {
            lastSnap = snapVal;
            formVal = snapVal;
        }
    };
    syncInt(std::clamp(static_cast<int>(pr.theme), 0, 2), ps.lastSnapTheme, ps.themeIndex);
    syncInt((pr.locale == motrix::core::Locale::ZhCN) ? 1 : 0, ps.lastSnapLocale, ps.localeIndex);
    if (pr.engineBinPath != ps.lastSnapEnginePath) {
        ps.lastSnapEnginePath = pr.engineBinPath;
        CopyUtf8ToBuffer(pr.engineBinPath, ps.enginePathBuffer.data(), ps.enginePathBuffer.size());
    }
    if (pr.fontFamily != ps.lastSnapFontFamily) {
        ps.lastSnapFontFamily = pr.fontFamily;
        CopyUtf8ToBuffer(pr.fontFamily, ps.fontFamilyBuffer.data(), ps.fontFamilyBuffer.size());
    }
    if (pr.lastSyncTrackerTime != ps.loadedSyncTrackerTime) {
        ps.loadedSyncTrackerTime = pr.lastSyncTrackerTime;
        const std::string lineTrackers = motrix::domain::ConvertTrackerDataToLine(sys.btTracker);
        CopyUtf8ToBuffer(lineTrackers, ps.btTrackerBuffer.data(), ps.btTrackerBuffer.size());
    }
}

}  // namespace

void LoadPrefsFromSnapshot(const motrix::app::AppStateSnapshot& st, PrefsFormState& ps) {
    const auto& pr = st.prefs;
    ps.themeIndex = static_cast<int>(pr.theme);
    ps.localeIndex = (pr.locale == motrix::core::Locale::ZhCN) ? 1 : 0;
    ps.runModeIndex = std::clamp(static_cast<int>(pr.runMode) - 1, 0, 2);

    ps.keepWindowState = pr.keepWindowState;
    ps.autoHideWindow = pr.autoHideWindow;
    ps.openAtLogin = pr.openAtLogin;
    ps.resumeAllWhenAppLaunched = pr.resumeAllWhenAppLaunched;
    ps.showProgressBar = pr.showProgressBar;
    ps.taskNotification = pr.taskNotification;
    ps.traySpeedometer = pr.traySpeedometer;
    ps.showTooltips = pr.showTooltips;
    ps.newTaskShowDownloading = pr.newTaskShowDownloading;
    ps.noConfirmBeforeDelete = pr.noConfirmBeforeDeleteTask;
    ps.deleteFilesWhenDeleteTask = pr.deleteFilesWhenDeleteTask;
    ps.enableUpnp = pr.enableUpnp;
    ps.autoSyncTracker = pr.autoSyncTracker;
    ps.useProxy = pr.useProxy;
    ps.keepSeeding = pr.keepSeeding;
    ps.protocolMagnet = pr.protocolMagnet;
    ps.protocolThunder = pr.protocolThunder;
    ps.protocolMotrix = pr.protocolMotrix;
    ps.protocolTorrent = pr.protocolTorrent;
    ps.logLevelIndex = FindOptIndex(kLogLevels, (pr.logLevel == "notice") ? "warn" : pr.logLevel, 2);
    CopyUtf8ToBuffer(pr.engineBinPath, ps.enginePathBuffer.data(), ps.enginePathBuffer.size());
    CopyUtf8ToBuffer(pr.aria2NextUpdateUrl, ps.aria2NextUrlBuffer.data(), ps.aria2NextUrlBuffer.size());
    CopyUtf8ToBuffer(
        pr.aria2NextMirrorPrefix, ps.aria2NextMirrorBuffer.data(), ps.aria2NextMirrorBuffer.size());
    CopyUtf8ToBuffer(pr.fontFamily, ps.fontFamilyBuffer.data(), ps.fontFamilyBuffer.size());

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
    ps.lastSnapTheme = ps.themeIndex;
    ps.lastSnapLocale = ps.localeIndex;
    ps.lastSnapEnginePath = pr.engineBinPath;
    ps.lastSnapFontFamily = pr.fontFamily;
    ps.lastSnapRpcSecret = st.sysConfig.rpcSecret;
    LoadSysPrefsFromSnapshot(st.sysConfig, ps);
    ps.loadedRevision = st.revision;
    ps.initialized = true;
}

bool ApplyPrefsDraft(const motrix::app::AppStateSnapshot& st,
                     motrix::app::ICommandSink& sink,
                     PrefsFormState& ps) {
    motrix::domain::UserPreferences pr = st.prefs;
    motrix::domain::SystemConfig sys = st.sysConfig;

    pr.theme = static_cast<motrix::domain::ThemePreference>(std::clamp(ps.themeIndex, 0, 2));
    pr.locale = (ps.localeIndex == 1) ? motrix::core::Locale::ZhCN : motrix::core::Locale::EnUS;
    pr.runMode = static_cast<motrix::domain::RunMode>(std::clamp(ps.runModeIndex, 0, 2) + 1);
    pr.keepWindowState = ps.keepWindowState;
    pr.autoHideWindow = ps.autoHideWindow;
    pr.openAtLogin = ps.openAtLogin;
    pr.resumeAllWhenAppLaunched = ps.resumeAllWhenAppLaunched;
    pr.showProgressBar = ps.showProgressBar;
    pr.taskNotification = ps.taskNotification;
    pr.traySpeedometer = ps.traySpeedometer;
    pr.showTooltips = ps.showTooltips;
    pr.newTaskShowDownloading = ps.newTaskShowDownloading;
    pr.noConfirmBeforeDeleteTask = ps.noConfirmBeforeDelete;
    pr.deleteFilesWhenDeleteTask = ps.deleteFilesWhenDeleteTask;
    pr.enableUpnp = ps.enableUpnp;
    pr.autoSyncTracker = ps.autoSyncTracker;
    pr.useProxy = ps.useProxy;
    pr.keepSeeding = ps.keepSeeding;
    pr.protocolMagnet = ps.protocolMagnet;
    pr.protocolThunder = ps.protocolThunder;
    pr.protocolMotrix = ps.protocolMotrix;
    pr.protocolTorrent = ps.protocolTorrent;
    pr.engineBinPath = ps.enginePathBuffer.data();
    pr.aria2NextUpdateUrl = ps.aria2NextUrlBuffer.data();
    pr.aria2NextMirrorPrefix = ps.aria2NextMirrorBuffer.data();
    const std::string_view trimmedFont = motrix::core::TrimAscii(ps.fontFamilyBuffer.data());
    pr.fontFamily = trimmedFont.empty() ? std::string("Microsoft YaHei UI") : std::string(trimmedFont);
    pr.logLevel = std::string(kLogLevels[static_cast<std::size_t>(std::clamp(ps.logLevelIndex, 0, 3))]);

    const auto allSources = motrix::domain::AllTrackerSourceOptions();
    pr.trackerSource.clear();
    for (std::size_t i = 0; i < allSources.size() && i < ps.trackerSourceSelected.size(); ++i) {
        if (ps.trackerSourceSelected[i]) {
            pr.trackerSource.emplace_back(allSources[i].url);
        }
    }

    ApplySysPrefsDraft(ps, sys);

    const bool needRestart =
        (pr.engineBinPath != st.prefs.engineBinPath) || (pr.enableUpnp != st.prefs.enableUpnp)
        || (pr.logLevel != st.prefs.logLevel) || (sys.rpcListenPort != st.sysConfig.rpcListenPort)
        || (sys.rpcSecret != st.sysConfig.rpcSecret) || (sys.listenPort != st.sysConfig.listenPort)
        || (sys.dhtListenPort != st.sysConfig.dhtListenPort)
        || (sys.continueDownloads != st.sysConfig.continueDownloads)
        || (sys.followTorrent != st.sysConfig.followTorrent)
        || (sys.disableIpv6 != st.sysConfig.disableIpv6);

    sink.Dispatch(motrix::app::SavePreferencesCmd{std::move(pr), std::move(sys), needRestart});
    ps.initialized = false;
    return needRestart;
}

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
    FontGuard fg(ctx.fonts, FontRole::Regular, 14.5f);
    StyleColorGuard col(ImGuiCol_Text, ctx.palette.subLabelText);
    ImGui::TextUnformatted(text.data(), text.data() + text.size());
}

void DrawPrefsFormRow(UiFrameContext& ctx, std::string_view label, float alignH, PrefsRowBodyFn body) {
    const float s = ctx.dpiScale;
    const ImVec2 startPos = ImGui::GetCursorScreenPos();
    const float availW = ImGui::GetContentRegionAvail().x;
    const float labelColW = std::clamp(availW * 0.24f, 124.0f * s, 166.0f * s);
    const float colGap = 18.0f * s;
    const float bodyX = startPos.x + labelColW + colGap;
    const float bodyW = std::max(140.0f * s, availW - labelColW - colGap);

    if (!label.empty()) {
        FontGuard fg(ctx.fonts, FontRole::Regular, 15.0f);
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
    {
        GroupGuard grp;
        if (body) {
            body(bodyX, bodyW);
        }
    }
    const float nextY = ImGui::GetItemRectMax().y + 18.0f * s;
    ImGui::SetCursorScreenPos(ImVec2(startPos.x, nextY));
    ImGui::Dummy(ImVec2(0.0f, 0.0f));
}

void RenderPreferencesView(UiFrameContext& ctx,
                           const PanelRect& rect,
                           int prefTab,
                           PrefsFormState& prefsState) {
    SyncPrefsFormState(ctx, prefsState);

    const float s = ctx.dpiScale;
    const bool wide = ctx.viewportW >= LayoutMetrics::kSubnavHideThreshold * s;
    const float marginX =
        (wide ? LayoutMetrics::kPanelMarginWide : LayoutMetrics::kPanelMarginNarrow) * s;
    const float headerH = LayoutMetrics::kPanelHeaderHeight * s;

    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(
        ImVec2(rect.x, rect.y), ImVec2(rect.x + rect.w, rect.y + rect.h), ToAbgrU32(ctx.palette.mainBg));

    const std::string_view titleKey =
        (prefTab == 0) ? "prefs.basic" : ((prefTab == 1) ? "prefs.advanced" : "prefs.lab");
    {
        FontGuard titleFont(ctx.fonts, FontRole::Medium, 20.0f);
        const std::string_view title = ctx.tr.Tr(titleKey);
        dl->AddText(ImVec2(rect.x + marginX, rect.y + LayoutMetrics::kPanelHeaderTopPad * s - 2.0f * s),
                    ToAbgrU32(ctx.palette.panelTitle),
                    title.data(),
                    title.data() + title.size());
    }
    const float borderY = rect.y + headerH - 2.0f * s;
    dl->AddRectFilled(ImVec2(rect.x + marginX, borderY),
                      ImVec2(rect.x + rect.w - marginX, borderY + 2.0f * s),
                      ToAbgrU32(ctx.palette.panelBorder));

    const float actionsH = kPrefsActionsBarHeight * s;
    const float scrollY = rect.y + headerH;
    const float scrollH = std::max(1.0f, rect.h - headerH - actionsH);

    ImGui::SetCursorScreenPos(ImVec2(rect.x, scrollY));
    {
        StyleVarGuard childPad(ImGuiStyleVar_WindowPadding, marginX, 22.0f * s);
        ChildGuard scrollChild(
            "##prefs_scroll_area",
            ChildWindowSpec{
                rect.w, scrollH, ImGuiChildFlags_AlwaysUseWindowPadding, ImGuiWindowFlags_None});
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

    RenderPrefsActionsBar(ctx, rect, marginX, prefsState);

    ImGui::SetCursorScreenPos(ImVec2(rect.x, rect.y));
    ImGui::Dummy(ImVec2(0.0f, 0.0f));
}

}  // namespace motrix::ui

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

constexpr float kPrefsActionsBarHeight = 70.0f;
constexpr std::array<std::string_view, 4> kLogLevels{"debug", "info", "warn", "error"};
constexpr std::array<std::string_view, 3> kBtEncryptionOpts{"preferred", "required", "disabled"};
constexpr std::array<std::string_view, 3> kBtTransportOpts{"both", "tcp", "utp"};
constexpr std::array<std::string_view, 4> kFileAllocOpts{"none", "prealloc", "falloc", "trunc"};
constexpr std::array<std::string_view, 2> kMediaModeOpts{"auto", "file"};
constexpr std::array<std::string_view, 2> kMediaFormatOpts{"mp4", "mkv"};

enum class FeedbackTone {
    Success,
    Info,
    Neutral,
    Warning,
};

struct PrefsBannerSpec {
    std::string_view text;
    FeedbackTone tone = FeedbackTone::Neutral;
    float alpha = 1.0f;
};

struct PillColorSet {
    RgbaColor fg;
    RgbaColor bg;
    RgbaColor border;
};

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

[[nodiscard]] std::string_view NormalizeDraftFontFamily(std::string_view raw) noexcept {
    const std::string_view trimmed = motrix::core::TrimAscii(raw);
    return trimmed.empty() ? std::string_view("Microsoft YaHei UI") : trimmed;
}

void LoadSpeedLimit(std::uint64_t bytesPerSec, bool& unitMb, int& val) noexcept {
    const std::uint64_t kb = bytesPerSec / 1024ULL;
    unitMb = (kb > 0 && kb % 1024ULL == 0);
    val = static_cast<int>(unitMb ? (kb / 1024ULL) : kb);
}

void LoadTrackerBufferAndBaseline(std::string_view btTrackerRaw, PrefsFormState& ps) {
    const std::string lineTrackers = motrix::domain::ConvertTrackerDataToLine(btTrackerRaw);
    CopyUtf8ToBuffer(lineTrackers, ps.btTrackerBuffer.data(), ps.btTrackerBuffer.size());
    ps.lastSnapBtTrackerRaw = std::string(btTrackerRaw);
    ps.lastSnapBtTrackerLoadedComma =
        motrix::domain::ConvertTrackerDataToComma(ps.btTrackerBuffer.data());
}

[[nodiscard]] bool IsPortFieldDirty(std::string_view buf, int draftPort, int snapPort) {
    const int clampedDraft = std::clamp(draftPort, 1024, 65535);
    const int clampedSnap = std::clamp(snapPort, 1024, 65535);
    if (clampedDraft != clampedSnap) {
        return true;
    }
    return buf != std::to_string(snapPort);
}

[[nodiscard]] std::size_t Utf8SequenceLen(unsigned char lead) noexcept {
    if (lead < 0x80u) {
        return 1;
    }
    if ((lead & 0xE0u) == 0xC0u) {
        return 2;
    }
    if ((lead & 0xF0u) == 0xE0u) {
        return 3;
    }
    if ((lead & 0xF8u) == 0xF0u) {
        return 4;
    }
    return 0;
}

[[nodiscard]] std::size_t TruncateUtf8Length(std::string_view src, std::size_t maxCopy) noexcept {
    if (src.size() <= maxCopy) {
        return src.size();
    }
    if (maxCopy == 0) {
        return 0;
    }
    std::size_t lead = maxCopy;
    while (lead > 0 && (static_cast<unsigned char>(src[lead - 1]) & 0xC0u) == 0x80u) {
        --lead;
    }
    if (lead == 0) {
        return 0;
    }
    const std::size_t seqLen = Utf8SequenceLen(static_cast<unsigned char>(src[lead - 1]));
    if (seqLen == 0) {
        return lead - 1;
    }
    if (seqLen == 1) {
        return lead;
    }
    const std::size_t copied = maxCopy - (lead - 1);
    return (copied < seqLen) ? (lead - 1) : ((lead - 1) + seqLen);
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
    CopyUtf8ToBuffer(std::to_string(ps.rpcPort), ps.rpcPortBuf.data(), ps.rpcPortBuf.size());
    CopyUtf8ToBuffer(sys.rpcSecret, ps.rpcSecretBuffer.data(), ps.rpcSecretBuffer.size());
    ps.btPort = sys.listenPort;
    CopyUtf8ToBuffer(std::to_string(ps.btPort), ps.btPortBuf.data(), ps.btPortBuf.size());
    ps.dhtPort = sys.dhtListenPort;
    CopyUtf8ToBuffer(std::to_string(ps.dhtPort), ps.dhtPortBuf.data(), ps.dhtPortBuf.size());
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
    LoadTrackerBufferAndBaseline(sys.btTracker, ps);
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
    const std::string draftTrackerComma =
        motrix::domain::ConvertTrackerDataToComma(ps.btTrackerBuffer.data());
    if (ps.lastSnapBtTrackerRaw != sys.btTracker
        || draftTrackerComma != ps.lastSnapBtTrackerLoadedComma) {
        sys.btTracker = draftTrackerComma;
    }
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
        LoadTrackerBufferAndBaseline(sys.btTracker, ps);
    }
}

bool IsTrackerSourcesDirty(const motrix::domain::UserPreferences& pr, const PrefsFormState& ps) {
    const auto allSources = motrix::domain::AllTrackerSourceOptions();
    for (std::size_t i = 0; i < allSources.size(); ++i) {
        const bool inSnap =
            std::ranges::find(pr.trackerSource, allSources[i].url) != pr.trackerSource.end();
        const bool inDraft = (i < ps.trackerSourceSelected.size()) && ps.trackerSourceSelected[i];
        if (inSnap != inDraft) {
            return true;
        }
    }
    return false;
}

bool IsUserPrefsDraftDirty(const motrix::domain::UserPreferences& pr, const PrefsFormState& ps) {
    const int snapTheme = std::clamp(static_cast<int>(pr.theme), 0, 2);
    const int snapLocale = (pr.locale == motrix::core::Locale::ZhCN) ? 1 : 0;
    const int snapRunMode = std::clamp(static_cast<int>(pr.runMode) - 1, 0, 2);
    const int snapLogIdx = FindOptIndex(kLogLevels, (pr.logLevel == "notice") ? "warn" : pr.logLevel, 2);

    if (std::clamp(ps.themeIndex, 0, 2) != snapTheme || ps.localeIndex != snapLocale
        || std::clamp(ps.runModeIndex, 0, 2) != snapRunMode
        || std::clamp(ps.logLevelIndex, 0, 3) != snapLogIdx) {
        return true;
    }
    if (ps.keepWindowState != pr.keepWindowState || ps.autoHideWindow != pr.autoHideWindow
        || ps.openAtLogin != pr.openAtLogin || ps.resumeAllWhenAppLaunched != pr.resumeAllWhenAppLaunched
        || ps.showProgressBar != pr.showProgressBar || ps.taskNotification != pr.taskNotification
        || ps.traySpeedometer != pr.traySpeedometer || ps.showTooltips != pr.showTooltips
        || ps.newTaskShowDownloading != pr.newTaskShowDownloading
        || ps.noConfirmBeforeDelete != pr.noConfirmBeforeDeleteTask
        || ps.deleteFilesWhenDeleteTask != pr.deleteFilesWhenDeleteTask || ps.enableUpnp != pr.enableUpnp
        || ps.autoSyncTracker != pr.autoSyncTracker || ps.useProxy != pr.useProxy
        || ps.keepSeeding != pr.keepSeeding || ps.protocolMagnet != pr.protocolMagnet
        || ps.protocolThunder != pr.protocolThunder || ps.protocolMotrix != pr.protocolMotrix
        || ps.protocolTorrent != pr.protocolTorrent) {
        return true;
    }
    if (std::string_view(ps.enginePathBuffer.data()) != pr.engineBinPath
        || std::string_view(ps.aria2NextUrlBuffer.data()) != pr.aria2NextUpdateUrl
        || std::string_view(ps.aria2NextMirrorBuffer.data()) != pr.aria2NextMirrorPrefix
        || NormalizeDraftFontFamily(ps.fontFamilyBuffer.data())
               != NormalizeDraftFontFamily(pr.fontFamily)) {
        return true;
    }
    return IsTrackerSourcesDirty(pr, ps);
}

bool IsSysConfigDraftDirty(const motrix::domain::SystemConfig& sys, const PrefsFormState& ps) {
    const std::uint64_t draftDl =
        static_cast<std::uint64_t>(std::max(0, ps.dlLimitKbps)) * (ps.dlUnitMb ? 1048576ULL : 1024ULL);
    const std::uint64_t snapDl = (sys.maxOverallDownloadLimit / 1024ULL) * 1024ULL;
    const std::uint64_t draftUl =
        static_cast<std::uint64_t>(std::max(0, ps.ulLimitKbps)) * (ps.ulUnitMb ? 1048576ULL : 1024ULL);
    const std::uint64_t snapUl = (sys.maxOverallUploadLimit / 1024ULL) * 1024ULL;

    if (std::clamp(ps.maxConcurrent, 1, 64) != std::clamp(sys.maxConcurrentDownloads, 1, 64)
        || std::clamp(ps.maxConnPerServer, 1, 128) != std::clamp(sys.maxConnectionPerServer, 1, 128)
        || draftDl != snapDl || draftUl != snapUl) {
        return true;
    }
    if (ps.continueDownloads != sys.continueDownloads || ps.followTorrent != sys.followTorrent
        || ps.rpcSaveUploadMetadata != sys.rpcSaveUploadMetadata
        || ps.btAutoDownloadContent != !sys.pauseMetadata || ps.autoFileRenaming != sys.autoFileRenaming
        || ps.allowOverwrite != sys.allowOverwrite || ps.checkIntegrity != sys.checkIntegrity
        || ps.enableDht != sys.enableDht || ps.enablePeerExchange != sys.enablePeerExchange
        || ps.btEnableLpd != sys.btEnableLpd || ps.btAnonymousMode != sys.btAnonymousMode
        || ps.checkCertificate != sys.checkCertificate || ps.disableIpv6 != sys.disableIpv6
        || ps.remoteTime != sys.remoteTime || ps.btFirstLastPieceFirst != sys.btFirstLastPieceFirst
        || ps.detachShareOnly != sys.detachShareOnly) {
        return true;
    }
    if (std::clamp(ps.btEncryptionIdx, 0, 2) != FindOptIndex(kBtEncryptionOpts, sys.btEncryption, 0)
        || std::clamp(ps.btTransportIdx, 0, 2) != FindOptIndex(kBtTransportOpts, sys.btTransport, 0)
        || std::clamp(ps.fileAllocIdx, 0, 3) != FindOptIndex(kFileAllocOpts, sys.fileAllocation, 0)
        || std::clamp(ps.mediaModeIdx, 0, 1) != FindOptIndex(kMediaModeOpts, sys.mediaMode, 0)
        || std::clamp(ps.mediaFormatIdx, 0, 1) != FindOptIndex(kMediaFormatOpts, sys.mediaFormat, 0)) {
        return true;
    }
    if (std::abs(ps.seedRatio - static_cast<float>(sys.seedRatio)) > 0.001f
        || ps.seedTime != sys.seedTime
        || IsPortFieldDirty(ps.rpcPortBuf.data(), ps.rpcPort, static_cast<int>(sys.rpcListenPort))
        || IsPortFieldDirty(ps.btPortBuf.data(), ps.btPort, sys.listenPort)
        || IsPortFieldDirty(ps.dhtPortBuf.data(), ps.dhtPort, sys.dhtListenPort)
        || std::clamp(ps.btMaxPeers, 0, 1000) != std::clamp(sys.btMaxPeers, 0, 1000)) {
        return true;
    }
    if (std::string_view(ps.dirBuffer.data()) != sys.dir
        || std::string_view(ps.rpcSecretBuffer.data()) != sys.rpcSecret
        || std::string_view(ps.allProxyBuffer.data()) != sys.allProxy
        || std::string_view(ps.noProxyBuffer.data()) != sys.noProxy
        || std::string_view(ps.userAgentBuffer.data()) != sys.userAgent) {
        return true;
    }
    std::string baselineComma;
    if (ps.lastSnapBtTrackerRaw == sys.btTracker) {
        baselineComma = ps.lastSnapBtTrackerLoadedComma;
    } else {
        const std::string line = motrix::domain::ConvertTrackerDataToLine(sys.btTracker);
        std::vector<char> tmp(ps.btTrackerBuffer.size());
        CopyUtf8ToBuffer(line, tmp.data(), tmp.size());
        baselineComma = motrix::domain::ConvertTrackerDataToComma(tmp.data());
    }
    return motrix::domain::ConvertTrackerDataToComma(ps.btTrackerBuffer.data()) != baselineComma;
}

PillColorSet MakePillColors(const MotrixPalette& p, FeedbackTone tone, float alpha) noexcept {
    switch (tone) {
        case FeedbackTone::Success: {
            const RgbaColor base = HexColor(0x67C23A);
            const RgbaColor fg = p.isDark ? HexColor(0x85CE61) : HexColor(0x2E9E47);
            return {WithAlpha(fg, alpha),
                    WithAlpha(base, (p.isDark ? 0.16f : 0.12f) * alpha),
                    WithAlpha(base, (p.isDark ? 0.42f : 0.38f) * alpha)};
        }
        case FeedbackTone::Info:
            return {WithAlpha(p.primary, alpha),
                    WithAlpha(p.primary, (p.isDark ? 0.16f : 0.10f) * alpha),
                    WithAlpha(p.primary, 0.36f * alpha)};
        case FeedbackTone::Warning: {
            const RgbaColor base = HexColor(0xE6A23C);
            const RgbaColor fg = p.isDark ? HexColor(0xF0B65B) : HexColor(0xB87314);
            return {WithAlpha(fg, alpha),
                    WithAlpha(base, (p.isDark ? 0.16f : 0.12f) * alpha),
                    WithAlpha(base, (p.isDark ? 0.42f : 0.38f) * alpha)};
        }
        case FeedbackTone::Neutral:
        default:
            return {WithAlpha(p.subLabelText, alpha),
                    WithAlpha(p.subLabelText, (p.isDark ? 0.14f : 0.08f) * alpha),
                    WithAlpha(p.subLabelText, 0.28f * alpha)};
    }
}

std::optional<PrefsBannerSpec>
MapTimedFeedbackBanner(UiFrameContext& ctx, const PrefsFormState& ps, float alpha) {
    switch (ps.feedbackKind) {
        case 1:
            return ps.feedbackHadChanges
                       ? PrefsBannerSpec{ctx.tr.Tr("prefs.feedback_saved"), FeedbackTone::Success, alpha}
                       : PrefsBannerSpec{
                             ctx.tr.Tr("prefs.feedback_no_changes"), FeedbackTone::Neutral, alpha};
        case 2:
            return ps.feedbackHadChanges ? PrefsBannerSpec{ctx.tr.Tr("prefs.feedback_discarded"),
                                                           FeedbackTone::Warning,
                                                           alpha}
                                         : PrefsBannerSpec{ctx.tr.Tr("prefs.feedback_no_changes"),
                                                           FeedbackTone::Neutral,
                                                           alpha};
        case 3:
            return PrefsBannerSpec{ctx.tr.Tr("prefs.feedback_session_reset"), FeedbackTone::Info, alpha};
        case 4:
            return PrefsBannerSpec{ctx.tr.Tr("prefs.feedback_factory_reset"), FeedbackTone::Info, alpha};
        case 5:
            return ps.feedbackHadChanges ? PrefsBannerSpec{ctx.tr.Tr("prefs.feedback_saved_reloaded"),
                                                           FeedbackTone::Info,
                                                           alpha}
                                         : PrefsBannerSpec{ctx.tr.Tr("prefs.feedback_no_changes"),
                                                           FeedbackTone::Neutral,
                                                           alpha};
        default: return std::nullopt;
    }
}

std::optional<PrefsBannerSpec>
ResolvePrefsFeedbackBanner(UiFrameContext& ctx, PrefsFormState& ps, bool dirty) {
    if (ps.feedbackKind != 0) {
        const auto remMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                               ps.feedbackUntil - std::chrono::steady_clock::now())
                               .count();
        if (remMs > 0) {
            ctx.host.NotifyAnimationActive();
            const float alpha =
                (remMs < 500) ? std::clamp(static_cast<float>(remMs) / 500.0f, 0.0f, 1.0f) : 1.0f;
            return MapTimedFeedbackBanner(ctx, ps, alpha);
        }
        ps.feedbackKind = 0;
    }
    if (dirty) {
        return PrefsBannerSpec{ctx.tr.Tr("prefs.unsaved_hint"), FeedbackTone::Warning, 1.0f};
    }
    return std::nullopt;
}

void DrawPrefsFeedbackPill(
    UiFrameContext& ctx, float x, float centerY, float maxRight, const PrefsBannerSpec& banner) {
    const float s = ctx.dpiScale;
    const float availW = maxRight - x;
    if (availW < 48.0f * s || banner.text.empty()) {
        return;
    }

    FontGuard fnt(ctx.fonts, FontRole::Regular, 13.0f);
    const ImVec2 tSz = ImGui::CalcTextSize(banner.text.data(), banner.text.data() + banner.text.size());
    const float padX = 12.0f * s;
    const float pillH = 28.0f * s;
    const float pillW = std::min(availW, tSz.x + 2.0f * padX);
    const float pillY = centerY - pillH * 0.5f;
    const float radius = pillH * 0.5f;

    const PillColorSet cols = MakePillColors(ctx.palette, banner.tone, banner.alpha);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(ImVec2(x, pillY), ImVec2(x + pillW, pillY + pillH), ToAbgrU32(cols.bg), radius);
    dl->AddRect(
        ImVec2(x, pillY), ImVec2(x + pillW, pillY + pillH), ToAbgrU32(cols.border), radius, 0, 1.0f);

    ClipRectGuard clip(
        dl, ClipRectSpec{x + padX * 0.5f, pillY, x + pillW - padX * 0.5f, pillY + pillH, true});
    const float textX = x + std::max(padX * 0.5f, (pillW - tSz.x) * 0.5f);
    const float textY = pillY + (pillH - tSz.y) * 0.5f;
    dl->AddText(ImVec2(textX, textY),
                ToAbgrU32(cols.fg),
                banner.text.data(),
                banner.text.data() + banner.text.size());
}

float MeasureButtonWidth(UiFrameContext& ctx, FontRole role, std::string_view label, float minW) {
    const float s = ctx.dpiScale;
    FontGuard bf(ctx.fonts, role, 14.5f);
    const ImVec2 sz = ImGui::CalcTextSize(label.data(), label.data() + label.size());
    const float padX = (role == FontRole::Medium) ? 36.0f : 32.0f;
    return std::max(minW * s, sz.x + padX * s);
}

void RenderPrefsActionsBar(UiFrameContext& ctx,
                           const PanelRect& rect,
                           float marginX,
                           PrefsFormState& ps) {
    const float s = ctx.dpiScale;
    const float actionsH = kPrefsActionsBarHeight * s;
    const float barY = rect.y + rect.h - actionsH;

    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(
        ImVec2(rect.x, barY), ImVec2(rect.x + rect.w, barY + actionsH), ToAbgrU32(ctx.palette.mainBg));

    const bool dirty = IsPrefsDraftDirty(ctx.state, ps);
    if (dirty && (ps.feedbackKind == 1 || ps.feedbackKind == 2 || ps.feedbackKind == 5)) {
        ps.feedbackKind = 0;
    }

    const ElStyle elSt{ctx.palette, ctx.fonts, s, &ctx.anim};
    const std::string_view saveLbl = ctx.tr.Tr("prefs.save_apply");
    const std::string_view discardLbl = ctx.tr.Tr("common.discard");
    const float saveW = MeasureButtonWidth(ctx, FontRole::Medium, saveLbl, 92.0f);
    const float discardW = MeasureButtonWidth(ctx, FontRole::Regular, discardLbl, 72.0f);
    const float btnH = 34.0f * s;
    const float btnY = barY + (actionsH - btnH) * 0.5f;
    const float saveX = rect.x + marginX;
    const float discardX = saveX + saveW + 12.0f * s;

    ImGui::SetCursorScreenPos(ImVec2(saveX, btnY));
    if (ElPrimaryButton(elSt, ElButtonSpec{"##prefs_save", saveLbl, saveW, btnH})) {
        if (dirty) {
            const bool needRestart = ApplyPrefsDraft(ctx.state, ctx.sink, ps);
            SetPrefsFeedback(ps, needRestart ? 5 : 1, true);
        } else {
            SetPrefsFeedback(ps, 1, false);
        }
        ctx.host.NotifyAnimationActive();
    }

    ImGui::SetCursorScreenPos(ImVec2(discardX, btnY));
    if (ElSecondaryButton(elSt, ElButtonSpec{"##prefs_discard", discardLbl, discardW, btnH})) {
        LoadPrefsFromSnapshot(ctx.state, ps);
        SetPrefsFeedback(ps, 2, dirty);
        ctx.host.NotifyAnimationActive();
    }

    if (const auto banner = ResolvePrefsFeedbackBanner(ctx, ps, dirty)) {
        const float pillX = discardX + discardW + 14.0f * s;
        const float maxRight = rect.x + rect.w - marginX - 54.0f * s;
        DrawPrefsFeedbackPill(ctx, pillX, btnY + btnH * 0.5f, maxRight, *banner);
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
    pr.fontFamily = std::string(NormalizeDraftFontFamily(ps.fontFamilyBuffer.data()));
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

bool IsPrefsDraftDirty(const motrix::app::AppStateSnapshot& st, const PrefsFormState& ps) {
    if (!ps.initialized) {
        return false;
    }
    return IsUserPrefsDraftDirty(st.prefs, ps) || IsSysConfigDraftDirty(st.sysConfig, ps);
}

void SetPrefsFeedback(PrefsFormState& ps, int kind, bool hadChanges) {
    ps.feedbackKind = kind;
    ps.feedbackHadChanges = hadChanges;
    ps.feedbackUntil = std::chrono::steady_clock::now() + std::chrono::milliseconds(3000);
}

void CopyUtf8ToBuffer(std::string_view src, char* dst, std::size_t cap) noexcept {
    if (!dst || cap == 0) {
        return;
    }
    const std::size_t n = TruncateUtf8Length(src, cap - 1);
    if (n > 0) {
        std::memcpy(dst, src.data(), n);
    }
    dst[n] = '\0';
}

void DrawSubCaption(const UiFrameContext& ctx, float x, float y, std::string_view text) {
    FontGuard fg(ctx.fonts, FontRole::Regular, 14.5f);
    ImGui::GetWindowDrawList()->AddText(
        ImVec2(x, y), ToAbgrU32(ctx.palette.subLabelText), text.data(), text.data() + text.size());
}

void DrawAlignedCaption(
    const UiFrameContext& ctx, float x, float y, float rowH, std::string_view text, float fontSize) {
    FontGuard fg(ctx.fonts, FontRole::Regular, fontSize);
    const ImVec2 sz = ImGui::CalcTextSize(text.data(), text.data() + text.size());
    ImGui::GetWindowDrawList()->AddText(ImVec2(x, y + (rowH - sz.y) * 0.5f),
                                        ToAbgrU32(ctx.palette.subLabelText),
                                        text.data(),
                                        text.data() + text.size());
}

void DrawPrefsCheckboxStack(UiFrameContext& ctx,
                            float x,
                            float& y,
                            std::span<const PrefsCheckboxItem> items) {
    const float s = ctx.dpiScale;
    const ElStyle elSt{ctx.palette, ctx.fonts, s, &ctx.anim};
    for (const auto& item : items) {
        ImGui::SetCursorScreenPos(ImVec2(x, y));
        ElCheckbox(elSt, item.id, ctx.tr.Tr(item.key), item.val);
        y += 30.0f * s;
    }
}

bool MatchesMediaFilter(std::string_view path, int filter) noexcept {
    if (filter == 1) {
        return motrix::domain::IsVideoExtension(path);
    }
    if (filter == 2) {
        return motrix::domain::IsAudioExtension(path);
    }
    if (filter == 3) {
        return motrix::domain::IsImageExtension(path);
    }
    return true;
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

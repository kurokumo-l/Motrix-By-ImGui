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
import :scope_guard;

namespace motrix::ui {

namespace {

constexpr std::array<std::string_view, 4> kActionLogLevels{"debug", "info", "warn", "error"};
constexpr std::array<std::string_view, 3> kActionBtEncOpts{"preferred", "required", "disabled"};
constexpr std::array<std::string_view, 3> kActionBtTransOpts{"both", "tcp", "utp"};
constexpr std::array<std::string_view, 4> kActionFileAllocOpts{"none", "prealloc", "falloc", "trunc"};
constexpr std::array<std::string_view, 2> kActionMediaModeOpts{"auto", "file"};
constexpr std::array<std::string_view, 2> kActionMediaFormatOpts{"mp4", "mkv"};

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

struct PrefsPillBounds {
    float x = 0.0f;
    float centerY = 0.0f;
    float maxRight = 0.0f;
};

struct PillColorSet {
    RgbaColor fg;
    RgbaColor bg;
    RgbaColor border;
};

int FindOptIdx(std::span<const std::string_view> opts, std::string_view val, int fallback = 0) noexcept {
    for (int i = 0; i < static_cast<int>(opts.size()); ++i) {
        if (opts[static_cast<std::size_t>(i)] == val) {
            return i;
        }
    }
    return fallback;
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
    const int snapLogIdx =
        FindOptIdx(kActionLogLevels, (pr.logLevel == "notice") ? "warn" : pr.logLevel, 2);

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
        || ps.protocolThunder != pr.protocolThunder || ps.protocolMotrix != pr.protocolMotrix) {
        return true;
    }
    if (std::string_view(ps.enginePathBuffer.data()) != pr.engineBinPath
        || std::string_view(ps.aria2NextUrlBuffer.data()) != pr.aria2NextUpdateUrl
        || std::string_view(ps.aria2NextMirrorBuffer.data()) != pr.aria2NextMirrorPrefix
        || std::string_view(ps.fontFamilyBuffer.data()) != pr.fontFamily) {
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
    if (std::clamp(ps.btEncryptionIdx, 0, 2) != FindOptIdx(kActionBtEncOpts, sys.btEncryption, 0)
        || std::clamp(ps.btTransportIdx, 0, 2) != FindOptIdx(kActionBtTransOpts, sys.btTransport, 0)
        || std::clamp(ps.fileAllocIdx, 0, 3) != FindOptIdx(kActionFileAllocOpts, sys.fileAllocation, 0)
        || std::clamp(ps.mediaModeIdx, 0, 1) != FindOptIdx(kActionMediaModeOpts, sys.mediaMode, 0)
        || std::clamp(ps.mediaFormatIdx, 0, 1)
               != FindOptIdx(kActionMediaFormatOpts, sys.mediaFormat, 0)) {
        return true;
    }
    if (std::abs(ps.seedRatio - static_cast<float>(sys.seedRatio)) > 0.001f
        || ps.seedTime != sys.seedTime
        || std::clamp(ps.rpcPort, 1024, 65535)
               != std::clamp(static_cast<int>(sys.rpcListenPort), 1024, 65535)
        || std::clamp(ps.btPort, 1024, 65535) != std::clamp(sys.listenPort, 1024, 65535)
        || std::clamp(ps.dhtPort, 1024, 65535) != std::clamp(sys.dhtListenPort, 1024, 65535)
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
    return motrix::domain::ConvertTrackerDataToComma(ps.btTrackerBuffer.data())
           != motrix::domain::ConvertTrackerDataToComma(sys.btTracker);
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

void DrawPrefsFeedbackPill(UiFrameContext& ctx,
                           const PrefsPillBounds& bounds,
                           const PrefsBannerSpec& banner) {
    const float s = ctx.dpiScale;
    const float availW = bounds.maxRight - bounds.x;
    if (availW < 48.0f * s || banner.text.empty()) {
        return;
    }

    FontGuard fnt(ctx.fonts, FontRole::Regular, 13.0f);
    const ImVec2 tSz = ImGui::CalcTextSize(banner.text.data(), banner.text.data() + banner.text.size());
    const float padX = 12.0f * s;
    const float pillH = 28.0f * s;
    const float pillW = std::min(availW, tSz.x + 2.0f * padX);
    const float pillY = bounds.centerY - pillH * 0.5f;
    const float radius = pillH * 0.5f;

    const PillColorSet cols = MakePillColors(ctx.palette, banner.tone, banner.alpha);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(
        ImVec2(bounds.x, pillY), ImVec2(bounds.x + pillW, pillY + pillH), ToAbgrU32(cols.bg), radius);
    dl->AddRect(ImVec2(bounds.x, pillY),
                ImVec2(bounds.x + pillW, pillY + pillH),
                ToAbgrU32(cols.border),
                radius,
                0,
                1.0f);

    ClipRectGuard clip(
        dl,
        ClipRectSpec{
            bounds.x + padX * 0.5f, pillY, bounds.x + pillW - padX * 0.5f, pillY + pillH, true});
    const float textX = bounds.x + std::max(padX * 0.5f, (pillW - tSz.x) * 0.5f);
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

}  // namespace

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
        DrawPrefsFeedbackPill(ctx, PrefsPillBounds{pillX, btnY + btnH * 0.5f, maxRight}, *banner);
    }
}

}  // namespace motrix::ui

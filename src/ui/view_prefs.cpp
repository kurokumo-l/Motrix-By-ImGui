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

void CopyStringToBuf(std::string_view src, char* dst, std::size_t cap) noexcept {
    if (cap == 0) {
        return;
    }
    const std::size_t n = std::min(src.size(), cap - 1);
    std::memcpy(dst, src.data(), n);
    dst[n] = '\0';
}

void LoadFromSnapshot(PrefsFormState& ps,
                      const motrix::domain::UserPreferences& prefs,
                      const motrix::domain::SystemConfig& sys,
                      std::uint64_t rev) {
    ps.initialized = true;
    ps.loadedRevision = rev;
    ps.themeIndex = static_cast<int>(prefs.theme);
    ps.localeIndex = (prefs.locale == motrix::core::Locale::ZhCN) ? 1 : 0;
    CopyStringToBuf(sys.dir, ps.dirBuffer.data(), ps.dirBuffer.size());
    ps.maxConcurrent = std::clamp(sys.maxConcurrentDownloads, 1, 64);
    ps.maxConnPerServer = std::clamp(sys.maxConnectionPerServer, 1, 64);
    ps.dlLimitKbps = static_cast<int>(sys.maxOverallDownloadLimit / 1024u);
    ps.ulLimitKbps = static_cast<int>(sys.maxOverallUploadLimit / 1024u);
    ps.continueDownloads = sys.continueDownloads;
    ps.newTaskShowDownloading = prefs.newTaskShowDownloading;
    ps.noConfirmBeforeDelete = prefs.noConfirmBeforeDeleteTask;
    ps.followTorrent = sys.followTorrent;
    ps.enableUpnp = prefs.enableUpnp;
    ps.useProxy = prefs.useProxy;
    ps.keepSeeding = prefs.keepSeeding;
    CopyStringToBuf(sys.allProxy, ps.allProxyBuffer.data(), ps.allProxyBuffer.size());
    CopyStringToBuf(sys.noProxy, ps.noProxyBuffer.data(), ps.noProxyBuffer.size());
    CopyStringToBuf(sys.btTracker, ps.btTrackerBuffer.data(), ps.btTrackerBuffer.size());
    CopyStringToBuf(sys.userAgent, ps.userAgentBuffer.data(), ps.userAgentBuffer.size());
    CopyStringToBuf(prefs.engineBinPath, ps.enginePathBuffer.data(), ps.enginePathBuffer.size());
    ps.seedRatio = static_cast<float>(sys.seedRatio);
    ps.seedTime = sys.seedTime;
    ps.rpcPort = static_cast<int>(sys.rpcListenPort);
    CopyStringToBuf(sys.rpcSecret, ps.rpcSecretBuffer.data(), ps.rpcSecretBuffer.size());
    ps.btPort = sys.listenPort;
    ps.dhtPort = sys.dhtListenPort;
}

void FieldLabel(UiFrameContext& ctx, std::string_view text) {
    FontGuard fg(ctx.fonts, FontRole::Medium, 13.5f);
    StyleColorGuard cg(ImGuiCol_Text, ctx.palette.panelTitle);
    ImGui::TextUnformatted(text.data(), text.data() + text.size());
}

void RenderBasicTab(UiFrameContext& ctx, PrefsFormState& ps) {
    const float s = ctx.dpiScale;
    const float inputW = std::min(460.0f * s, ImGui::GetContentRegionAvail().x - 16.0f * s);

    FieldLabel(ctx, ctx.tr.Tr("prefs.appearance"));
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

    FieldLabel(ctx, ctx.tr.Tr("prefs.language"));
    ImGui::SetNextItemWidth(200.0f * s);
    static constexpr const char* kLangItems[] = {"English", "简体中文"};
    {
        FontGuard fnt(ctx.fonts, FontRole::Regular, 14.0f);
        ImGui::Combo("##pref_locale", &ps.localeIndex, kLangItems, 2);
    }
    ImGui::Dummy(ImVec2(0.0f, 10.0f * s));

    FieldLabel(ctx, ctx.tr.Tr("prefs.default_dir"));
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
                CopyStringToBuf(*chosen, ps.dirBuffer.data(), ps.dirBuffer.size());
            }
        }
    }
    ImGui::Dummy(ImVec2(0.0f, 10.0f * s));

    FieldLabel(ctx, ctx.tr.Tr("prefs.concurrency"));
    ImGui::SetNextItemWidth(inputW);
    {
        FontGuard fnt(ctx.fonts, FontRole::Regular, 14.0f);
        ImGui::SliderInt("##pref_conc", &ps.maxConcurrent, 1, 32);
    }
    ImGui::Dummy(ImVec2(0.0f, 8.0f * s));

    FieldLabel(ctx, ctx.tr.Tr("prefs.split"));
    ImGui::SetNextItemWidth(inputW);
    {
        FontGuard fnt(ctx.fonts, FontRole::Regular, 14.0f);
        ImGui::SliderInt("##pref_conn", &ps.maxConnPerServer, 1, 64);
    }
    ImGui::Dummy(ImVec2(0.0f, 8.0f * s));

    FieldLabel(ctx, ctx.tr.Tr("prefs.max_dl_limit"));
    ImGui::SetNextItemWidth(200.0f * s);
    {
        FontGuard fnt(ctx.fonts, FontRole::Regular, 14.0f);
        if (ImGui::InputInt("##pref_dl_limit", &ps.dlLimitKbps, 100, 1024)) {
            ps.dlLimitKbps = std::max(0, ps.dlLimitKbps);
        }
    }
    ImGui::Dummy(ImVec2(0.0f, 6.0f * s));

    FieldLabel(ctx, ctx.tr.Tr("prefs.max_ul_limit"));
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

void RenderAdvancedTab(UiFrameContext& ctx, PrefsFormState& ps) {
    const float s = ctx.dpiScale;
    const float inputW = std::min(460.0f * s, ImGui::GetContentRegionAvail().x - 16.0f * s);
    FontGuard fnt(ctx.fonts, FontRole::Regular, 14.0f);

    ImGui::Checkbox(ctx.tr.Tr("prefs.use_proxy").data(), &ps.useProxy);
    if (ps.useProxy) {
        FieldLabel(ctx, ctx.tr.Tr("prefs.all_proxy"));
        ImGui::SetNextItemWidth(inputW);
        ImGui::InputText("##pref_proxy", ps.allProxyBuffer.data(), ps.allProxyBuffer.size());

        FieldLabel(ctx, ctx.tr.Tr("prefs.no_proxy"));
        ImGui::SetNextItemWidth(inputW);
        ImGui::InputText("##pref_noproxy", ps.noProxyBuffer.data(), ps.noProxyBuffer.size());
    }
    ImGui::Dummy(ImVec2(0.0f, 8.0f * s));

    FieldLabel(ctx, ctx.tr.Tr("prefs.seed_ratio"));
    ImGui::SetNextItemWidth(200.0f * s);
    if (ImGui::InputFloat("##pref_seed_ratio", &ps.seedRatio, 0.1f, 0.5f, "%.2f")) {
        ps.seedRatio = std::clamp(ps.seedRatio, 0.0f, 100.0f);
    }

    FieldLabel(ctx, ctx.tr.Tr("prefs.seed_time"));
    ImGui::SetNextItemWidth(200.0f * s);
    if (ImGui::InputInt("##pref_seed_time", &ps.seedTime, 10, 60)) {
        ps.seedTime = std::max(0, ps.seedTime);
    }
    ImGui::Dummy(ImVec2(0.0f, 8.0f * s));

    FieldLabel(ctx, ctx.tr.Tr("prefs.bt_trackers"));
    ImGui::InputTextMultiline("##pref_trackers",
                              ps.btTrackerBuffer.data(),
                              ps.btTrackerBuffer.size(),
                              ImVec2(inputW, 92.0f * s));
    ImGui::Dummy(ImVec2(0.0f, 8.0f * s));

    FieldLabel(ctx, ctx.tr.Tr("prefs.user_agent"));
    ImGui::SetNextItemWidth(inputW);
    ImGui::InputText("##pref_ua", ps.userAgentBuffer.data(), ps.userAgentBuffer.size());
    ImGui::Dummy(ImVec2(0.0f, 10.0f * s));

    FieldLabel(ctx, ctx.tr.Tr("prefs.rpc_port"));
    ImGui::SetNextItemWidth(160.0f * s);
    if (ImGui::InputInt("##pref_rpc_port", &ps.rpcPort, 1, 10)) {
        ps.rpcPort = std::clamp(ps.rpcPort, 1024, 65535);
    }
    FieldLabel(ctx, ctx.tr.Tr("prefs.rpc_secret"));
    ImGui::SetNextItemWidth(inputW);
    ImGui::InputText("##pref_rpc_secret",
                     ps.rpcSecretBuffer.data(),
                     ps.rpcSecretBuffer.size(),
                     ImGuiInputTextFlags_Password);

    FieldLabel(ctx, ctx.tr.Tr("prefs.bt_port"));
    ImGui::SetNextItemWidth(160.0f * s);
    if (ImGui::InputInt("##pref_bt_port", &ps.btPort, 1, 10)) {
        ps.btPort = std::clamp(ps.btPort, 1024, 65535);
    }
    FieldLabel(ctx, ctx.tr.Tr("prefs.dht_port"));
    ImGui::SetNextItemWidth(160.0f * s);
    if (ImGui::InputInt("##pref_dht_port", &ps.dhtPort, 1, 10)) {
        ps.dhtPort = std::clamp(ps.dhtPort, 1024, 65535);
    }
}

void RenderLabTab(UiFrameContext& ctx, PrefsFormState& ps) {
    const float s = ctx.dpiScale;
    const float inputW = std::min(460.0f * s, ImGui::GetContentRegionAvail().x - 16.0f * s);
    FontGuard fnt(ctx.fonts, FontRole::Regular, 14.0f);

    FieldLabel(ctx, ctx.tr.Tr("prefs.engine_path"));
    ImGui::SetNextItemWidth(inputW);
    ImGui::InputText("##pref_engine_bin", ps.enginePathBuffer.data(), ps.enginePathBuffer.size());

    if (!ctx.state.resolvedEnginePath.empty()) {
        StyleColorGuard cg(ImGuiCol_Text, ctx.palette.taskMetaText);
        const std::string engineBanner =
            std::format("Active Engine: {} {} ({})",
                        ctx.state.engineInfo.product.empty() ? "aria2" : ctx.state.engineInfo.product,
                        ctx.state.engineInfo.version,
                        ctx.state.resolvedEnginePath);
        ImGui::TextWrapped("%s", engineBanner.c_str());
    }
    ImGui::Dummy(ImVec2(0.0f, 10.0f * s));

    ImGui::Checkbox("UPnP / NAT-PMP", &ps.enableUpnp);
    ImGui::Checkbox(ctx.tr.Tr("prefs.keep_seeding").data(), &ps.keepSeeding);
}

void ApplyDraftToCommands(const UiFrameContext& ctx, const PrefsFormState& ps) {
    auto newPrefs = ctx.state.prefs;
    newPrefs.theme = static_cast<motrix::domain::ThemePreference>(std::clamp(ps.themeIndex, 0, 2));
    newPrefs.locale = (ps.localeIndex == 1) ? motrix::core::Locale::ZhCN : motrix::core::Locale::EnUS;
    newPrefs.newTaskShowDownloading = ps.newTaskShowDownloading;
    newPrefs.noConfirmBeforeDeleteTask = ps.noConfirmBeforeDelete;
    newPrefs.enableUpnp = ps.enableUpnp;
    newPrefs.useProxy = ps.useProxy;
    newPrefs.keepSeeding = ps.keepSeeding;
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
    newSys.btTracker = ps.btTrackerBuffer.data();
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
                RenderAdvancedTab(ctx, ps);
            } else {
                RenderLabTab(ctx, ps);
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
                CopyStringToBuf(ctx.state.sysConfig.dir, ps.dirBuffer.data(), ps.dirBuffer.size());
            }
        }
    }
}

}  // namespace motrix::ui

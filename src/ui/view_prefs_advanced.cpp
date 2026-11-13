module;

#include "imgui.h"

module motrix.ui;

import std;
import motrix.core;
import motrix.domain;
import motrix.app;
import :views_internal;
import :theme;
import :scope_guard;

namespace motrix::ui {

namespace {

[[nodiscard]] std::string FormatSyncTimestamp(std::int64_t epochSec, std::string_view neverText) {
    if (epochSec <= 0) {
        return std::string(neverText);
    }
    try {
        const auto sysTp = std::chrono::sys_seconds{std::chrono::seconds{epochSec}};
        const auto localTp = std::chrono::current_zone()->to_local(sysTp);
        return std::format("{:%Y-%m-%d %H:%M}", std::chrono::floor<std::chrono::seconds>(localTp));
    } catch (...) {
        const auto sysTp = std::chrono::sys_seconds{std::chrono::seconds{epochSec}};
        return std::format("{:%Y-%m-%d %H:%M} UTC", sysTp);
    }
}

}  // namespace

void RenderPrefsAdvancedTab(UiFrameContext& ctx, PrefsFormState& ps) {
    const float s = ctx.dpiScale;
    const float inputW = std::min(460.0f * s, ImGui::GetContentRegionAvail().x - 16.0f * s);
    FontGuard fnt(ctx.fonts, FontRole::Regular, 14.0f);

    ImGui::Checkbox(ctx.tr.Tr("prefs.use_proxy").data(), &ps.useProxy);
    if (ps.useProxy) {
        DrawPrefsFieldLabel(ctx, ctx.tr.Tr("prefs.all_proxy"));
        ImGui::SetNextItemWidth(inputW);
        ImGui::InputText("##pref_proxy", ps.allProxyBuffer.data(), ps.allProxyBuffer.size());

        DrawPrefsFieldLabel(ctx, ctx.tr.Tr("prefs.no_proxy"));
        ImGui::SetNextItemWidth(inputW);
        ImGui::InputText("##pref_noproxy", ps.noProxyBuffer.data(), ps.noProxyBuffer.size());
    }
    ImGui::Dummy(ImVec2(0.0f, 8.0f * s));

    DrawPrefsFieldLabel(ctx, ctx.tr.Tr("prefs.seed_ratio"));
    ImGui::SetNextItemWidth(200.0f * s);
    if (ImGui::InputFloat("##pref_seed_ratio", &ps.seedRatio, 0.1f, 0.5f, "%.2f")) {
        ps.seedRatio = std::clamp(ps.seedRatio, 0.0f, 100.0f);
    }

    DrawPrefsFieldLabel(ctx, ctx.tr.Tr("prefs.seed_time"));
    ImGui::SetNextItemWidth(200.0f * s);
    if (ImGui::InputInt("##pref_seed_time", &ps.seedTime, 10, 60)) {
        ps.seedTime = std::max(0, ps.seedTime);
    }
    ImGui::Checkbox(ctx.tr.Tr("prefs.keep_seeding").data(), &ps.keepSeeding);
    ImGui::Dummy(ImVec2(0.0f, 10.0f * s));

    DrawPrefsFieldLabel(ctx, ctx.tr.Tr("prefs.tracker_sources"));
    const auto allSources = motrix::domain::AllTrackerSourceOptions();
    if (ps.trackerSourceSelected.size() != allSources.size()) {
        ps.trackerSourceSelected.assign(allSources.size(), false);
    }
    {
        ChildGuard trackerList(
            "##pref_tracker_srcs", inputW, 130.0f * s, ImGuiChildFlags_Borders, ImGuiWindowFlags_None);
        if (trackerList.IsVisible()) {
            for (std::size_t i = 0; i < allSources.size(); ++i) {
                bool checked = ps.trackerSourceSelected[i];
                const std::string cbLabel = std::string(allSources[i].label);
                if (ImGui::Checkbox(cbLabel.c_str(), &checked)) {
                    ps.trackerSourceSelected[i] = checked;
                }
            }
        }
    }
    ImGui::Dummy(ImVec2(0.0f, 6.0f * s));

    {
        const bool syncing = ctx.state.isSyncingTrackers;
        if (syncing) {
            ImGui::BeginDisabled();
        }
        StyleColorGuard bg(ImGuiCol_Button, ctx.palette.primary);
        StyleColorGuard fg(ImGuiCol_Text, HexColor(0xFFFFFF));
        const std::string_view syncLbl =
            syncing ? ctx.tr.Tr("prefs.syncing") : ctx.tr.Tr("prefs.sync_trackers");
        if (ImGui::Button(syncLbl.data(), ImVec2(118.0f * s, 30.0f * s))) {
            std::vector<std::string> urls;
            for (std::size_t i = 0; i < allSources.size(); ++i) {
                if (ps.trackerSourceSelected[i]) {
                    urls.emplace_back(allSources[i].url);
                }
            }
            ctx.sink.Dispatch(motrix::app::SyncTrackersCmd{std::move(urls)});
        }
        if (syncing) {
            ImGui::EndDisabled();
        }
        ImGui::SameLine(0.0f, 12.0f * s);
        const std::string stampText = std::format(
            "{} {}",
            ctx.tr.Tr("prefs.last_sync"),
            FormatSyncTimestamp(ctx.state.prefs.lastSyncTrackerTime, ctx.tr.Tr("prefs.never_synced")));
        StyleColorGuard mg(ImGuiCol_Text, ctx.palette.taskMetaText);
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(stampText.data(), stampText.data() + stampText.size());
    }
    ImGui::Checkbox(ctx.tr.Tr("prefs.auto_sync_tracker").data(), &ps.autoSyncTracker);

    DrawPrefsFieldLabel(ctx, ctx.tr.Tr("prefs.bt_trackers"));
    ImGui::InputTextMultiline("##pref_trackers",
                              ps.btTrackerBuffer.data(),
                              ps.btTrackerBuffer.size(),
                              ImVec2(inputW, 96.0f * s));
    ImGui::Dummy(ImVec2(0.0f, 10.0f * s));

    DrawPrefsFieldLabel(ctx, ctx.tr.Tr("prefs.user_agent"));
    ImGui::SetNextItemWidth(inputW);
    ImGui::InputText("##pref_ua", ps.userAgentBuffer.data(), ps.userAgentBuffer.size());
    ImGui::Dummy(ImVec2(0.0f, 8.0f * s));

    ImGui::Checkbox(ctx.tr.Tr("prefs.enable_upnp").data(), &ps.enableUpnp);
    DrawPrefsFieldLabel(ctx, ctx.tr.Tr("prefs.rpc_port"));
    ImGui::SetNextItemWidth(160.0f * s);
    if (ImGui::InputInt("##pref_rpc_port", &ps.rpcPort, 1, 10)) {
        ps.rpcPort = std::clamp(ps.rpcPort, 1024, 65535);
    }
    DrawPrefsFieldLabel(ctx, ctx.tr.Tr("prefs.rpc_secret"));
    ImGui::SetNextItemWidth(inputW);
    ImGui::InputText("##pref_rpc_secret",
                     ps.rpcSecretBuffer.data(),
                     ps.rpcSecretBuffer.size(),
                     ImGuiInputTextFlags_Password);

    DrawPrefsFieldLabel(ctx, ctx.tr.Tr("prefs.bt_port"));
    ImGui::SetNextItemWidth(160.0f * s);
    if (ImGui::InputInt("##pref_bt_port", &ps.btPort, 1, 10)) {
        ps.btPort = std::clamp(ps.btPort, 1024, 65535);
    }
    DrawPrefsFieldLabel(ctx, ctx.tr.Tr("prefs.dht_port"));
    ImGui::SetNextItemWidth(160.0f * s);
    if (ImGui::InputInt("##pref_dht_port", &ps.dhtPort, 1, 10)) {
        ps.dhtPort = std::clamp(ps.dhtPort, 1024, 65535);
    }

    ImGui::Dummy(ImVec2(0.0f, 10.0f * s));
    DrawPrefsFieldLabel(ctx, ctx.tr.Tr("prefs.protocols"));
    ImGui::Checkbox(ctx.tr.Tr("prefs.proto_motrix").data(), &ps.protocolMotrix);
    ImGui::Checkbox(ctx.tr.Tr("prefs.proto_magnet").data(), &ps.protocolMagnet);
    ImGui::Checkbox(ctx.tr.Tr("prefs.proto_thunder").data(), &ps.protocolThunder);
}

void RenderPrefsLabTab(UiFrameContext& ctx, PrefsFormState& ps) {
    const float s = ctx.dpiScale;
    const float inputW = std::min(460.0f * s, ImGui::GetContentRegionAvail().x - 16.0f * s);
    FontGuard fnt(ctx.fonts, FontRole::Regular, 14.0f);

    DrawPrefsFieldLabel(ctx, ctx.tr.Tr("prefs.engine_path"));
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
}

}  // namespace motrix::ui

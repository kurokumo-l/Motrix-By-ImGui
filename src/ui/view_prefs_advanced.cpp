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

constexpr std::string_view kNgosangTrackerUrl = "https://github.com/ngosang/trackerslist";
constexpr std::string_view kXiu2TrackerUrl = "https://github.com/XIU2/TrackersListCollection";
constexpr std::string_view kRpcDocUrl =
    "https://aria2.github.io/manual/en/html/aria2c.html#rpc-interface";

int RandomPortInRange(int low, int high) {
    std::mt19937 rng{std::random_device{}()};
    std::uniform_int_distribution<int> dist(low, high);
    return dist(rng);
}

std::string GenerateRandomSecret(std::size_t len) {
    static constexpr std::string_view kChars =
        "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";
    std::mt19937 rng{std::random_device{}()};
    std::uniform_int_distribution<std::size_t> dist(0, kChars.size() - 1);
    std::string out;
    out.reserve(len);
    for (std::size_t i = 0; i < len; ++i) {
        out.push_back(kChars[dist(rng)]);
    }
    return out;
}

void DrawSubCaption(const UiFrameContext& ctx, float x, float y, std::string_view txt) {
    FontGuard fg(ctx.fonts, FontRole::Regular, 14.5f);
    ImGui::GetWindowDrawList()->AddText(
        ImVec2(x, y), ToAbgrU32(ctx.palette.subLabelText), txt.data(), txt.data() + txt.size());
}

struct AdvChkItem {
    const char* id = nullptr;
    std::string_view key{};
    bool* val = nullptr;
};

void DrawAdvCheckboxStack(UiFrameContext& ctx, float x, float& y, std::span<const AdvChkItem> items) {
    const float s = ctx.dpiScale;
    const ElStyle elSt{ctx.palette, ctx.fonts, s, &ctx.anim};
    for (const auto& item : items) {
        ImGui::SetCursorScreenPos(ImVec2(x, y));
        ElCheckbox(elSt, item.id, ctx.tr.Tr(item.key), item.val);
        y += 30.0f * s;
    }
}

struct PortFieldSpec {
    const char* id = nullptr;
    int& portVal;
    float x = 0.0f;
    float y = 0.0f;
    float width = 0.0f;
    int randLow = 10000;
    int randHigh = 60000;
};

struct PortDraftEntry {
    std::array<char, 16> buf{};
    bool active = false;
};

PortDraftEntry LoadPortDraft(const char* id, int portVal) {
    ImGuiStorage* store = ImGui::GetStateStorage();
    const ImGuiID baseId = ImGui::GetID(id);
    PortDraftEntry entry{};
    entry.active = store->GetBool(baseId ^ 0x5041u, false);
    if (!entry.active) {
        CopyUtf8ToBuffer(std::to_string(portVal), entry.buf.data(), entry.buf.size());
        return entry;
    }
    for (int i = 0; i < 4; ++i) {
        const int word = store->GetInt(baseId ^ static_cast<ImGuiID>(i + 1), 0);
        std::memcpy(entry.buf.data() + static_cast<std::size_t>(i) * 4, &word, sizeof(int));
    }
    entry.buf.back() = '\0';
    return entry;
}

void SavePortDraft(const char* id, const PortDraftEntry& entry) {
    ImGuiStorage* store = ImGui::GetStateStorage();
    const ImGuiID baseId = ImGui::GetID(id);
    store->SetBool(baseId ^ 0x5041u, entry.active);
    for (int i = 0; i < 4; ++i) {
        int word = 0;
        std::memcpy(&word, entry.buf.data() + static_cast<std::size_t>(i) * 4, sizeof(int));
        store->SetInt(baseId ^ static_cast<ImGuiID>(i + 1), word);
    }
}

void DrawPortCompositeField(UiFrameContext& ctx, const PortFieldSpec& spec) {
    const float s = ctx.dpiScale;
    PortDraftEntry entry = LoadPortDraft(spec.id, spec.portVal);
    ImGui::SetCursorScreenPos(ImVec2(spec.x, spec.y));
    const ElStyle elSt{ctx.palette, ctx.fonts, s, &ctx.anim};
    const ElCompositeResult res = ElCompositeInput(
        elSt,
        ElCompositeSpec{
            spec.id, entry.buf.data(), entry.buf.size(), spec.width, std::nullopt, MotrixIcon::Dice});
    if (res.active) {
        entry.active = true;
    }
    if (res.deactivated) {
        entry.active = false;
        int parsed = spec.portVal;
        const std::string_view sv(entry.buf.data());
        auto [ptr, ec] = std::from_chars(sv.data(), sv.data() + sv.size(), parsed);
        if (ec == std::errc{} && ptr == sv.data() + sv.size()) {
            spec.portVal = std::clamp(parsed, 1024, 65535);
        }
        CopyUtf8ToBuffer(std::to_string(spec.portVal), entry.buf.data(), entry.buf.size());
    }
    if (res.rightClicked) {
        entry.active = false;
        spec.portVal = RandomPortInRange(spec.randLow, spec.randHigh);
        CopyUtf8ToBuffer(std::to_string(spec.portVal), entry.buf.data(), entry.buf.size());
    }
    SavePortDraft(spec.id, entry);
}

struct SyncBtnSpec {
    const char* id = nullptr;
    float x = 0.0f;
    float y = 0.0f;
    bool syncing = false;
    std::function<void()> onClick{};
};

void DrawSyncSquareButton(UiFrameContext& ctx, const SyncBtnSpec& spec) {
    const float s = ctx.dpiScale;
    const float w = 36.0f * s;
    const float h = 32.0f * s;
    ImGui::SetCursorScreenPos(ImVec2(spec.x, spec.y));
    const bool clicked = ImGui::InvisibleButton(spec.id, ImVec2(w, h));
    const bool hov = !spec.syncing && ImGui::IsItemHovered();
    if (hov) {
        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    }
    const float hovT = ctx.anim.Float(HashId(spec.id), hov ? 1.0f : 0.0f, 22.0f, 0.0f);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(ImVec2(spec.x, spec.y),
                      ImVec2(spec.x + w, spec.y + h),
                      ToAbgrU32(ctx.palette.inputBg),
                      4.0f * s);
    dl->AddRect(ImVec2(spec.x, spec.y),
                ImVec2(spec.x + w, spec.y + h),
                ToAbgrU32(LerpColor(ctx.palette.inputBorder, ctx.palette.primary, hovT)),
                4.0f * s,
                0,
                1.0f);
    const float iconSz = 14.0f * s;
    DrawMotrixIcon(IconSpec{
        MotrixIcon::Sync,
        spec.x + (w - iconSz) * 0.5f,
        spec.y + (h - iconSz) * 0.5f,
        iconSz,
        iconSz,
        LerpColor(ctx.palette.subLabelText, ctx.palette.primary, hovT),
        s,
    });
    if (clicked && !spec.syncing && spec.onClick) {
        spec.onClick();
    }
}

[[nodiscard]] std::vector<std::string> BuildSelectedTrackerUrls(const std::vector<bool>& selected) {
    const auto allOpts = motrix::domain::AllTrackerSourceOptions();
    std::vector<std::string> urls;
    for (std::size_t i = 0; i < allOpts.size() && i < selected.size(); ++i) {
        if (selected[i]) {
            urls.emplace_back(allOpts[i].url);
        }
    }
    return urls;
}

void RenderAdvProxyRow(UiFrameContext& ctx, PrefsFormState& ps) {
    const float s = ctx.dpiScale;
    const ElStyle elSt{ctx.palette, ctx.fonts, s, &ctx.anim};
    constexpr std::string_view kProxyHint = "[http://][USER:PASSWORD@]HOST[:PORT]";

    DrawPrefsFormRow(ctx, ctx.tr.Tr("prefs.proxy"), 26.0f * s, [&](float bx, float bw) {
        float cy = ImGui::GetCursorScreenPos().y;
        const float fieldW = std::min(bw, 460.0f * s);
        ImGui::SetCursorScreenPos(ImVec2(bx, cy));
        ElSwitch(elSt, "##adv_proxy_en", ctx.tr.Tr("prefs.use_proxy"), &ps.useProxy);
        cy += 36.0f * s;
        if (ps.useProxy) {
            DrawSubCaption(ctx, bx, cy, ctx.tr.Tr("prefs.all_proxy"));
            cy += 24.0f * s;
            ImGui::SetCursorScreenPos(ImVec2(bx, cy));
            ElInputText(elSt,
                        ElTextInputSpec{"##adv_all_proxy",
                                        ps.allProxyBuffer.data(),
                                        ps.allProxyBuffer.size(),
                                        fieldW,
                                        kProxyHint});
            cy += 42.0f * s;
            DrawSubCaption(ctx, bx, cy, ctx.tr.Tr("prefs.no_proxy"));
            cy += 24.0f * s;
            ImGui::SetCursorScreenPos(ImVec2(bx, cy));
            ElInputText(elSt,
                        ElTextInputSpec{"##adv_no_proxy",
                                        ps.noProxyBuffer.data(),
                                        ps.noProxyBuffer.size(),
                                        fieldW,
                                        "localhost,127.0.0.1"});
            cy += 36.0f * s;
        }
        ImGui::SetCursorScreenPos(ImVec2(bx, cy - 6.0f * s));
        ImGui::Dummy(ImVec2(0.0f, 0.0f));
    });
}

void RenderAdvTrackerRow(UiFrameContext& ctx, PrefsFormState& ps) {
    const float s = ctx.dpiScale;
    const ElStyle elSt{ctx.palette, ctx.fonts, s, &ctx.anim};

    DrawPrefsFormRow(ctx, ctx.tr.Tr("prefs.tracker_sources"), 32.0f * s, [&](float bx, float bw) {
        float cy = ImGui::GetCursorScreenPos().y;
        const float fieldW = std::min(bw, 490.0f * s);
        const float selW = std::max(140.0f * s, fieldW - 44.0f * s);

        ImGui::SetCursorScreenPos(ImVec2(bx, cy));
        ElTrackerTagSelect(elSt, "##adv_trk_sel", ps.trackerSourceSelected, selW);
        const float afterSelY = ImGui::GetItemRectMax().y;

        DrawSyncSquareButton(
            ctx,
            SyncBtnSpec{"##adv_trk_sync", bx + selW + 8.0f * s, cy, ctx.state.isSyncingTrackers, [&]() {
                            ctx.sink.Dispatch(motrix::app::SyncTrackersCmd{
                                BuildSelectedTrackerUrls(ps.trackerSourceSelected)});
                        }});
        cy = afterSelY + 12.0f * s;

        ImGui::SetCursorScreenPos(ImVec2(bx, cy));
        ElInputTextMultiline(elSt,
                             ElTextInputSpec{"##adv_bt_trk",
                                             ps.btTrackerBuffer.data(),
                                             ps.btTrackerBuffer.size(),
                                             fieldW,
                                             ctx.tr.Tr("prefs.bt_tracker_tips"),
                                             0,
                                             92.0f * s});
        cy += 102.0f * s;

        {
            FontGuard fg(ctx.fonts, FontRole::Regular, 13.5f);
            const std::string_view rec = ctx.tr.Tr("prefs.recommended");
            const ImVec2 recSz = ImGui::CalcTextSize(rec.data(), rec.data() + rec.size());
            ImGui::GetWindowDrawList()->AddText(ImVec2(bx, cy),
                                                ToAbgrU32(ctx.palette.taskMetaText),
                                                rec.data(),
                                                rec.data() + rec.size());
            float lx = bx + recSz.x + 8.0f * s;
            ImGui::SetCursorScreenPos(ImVec2(lx, cy));
            IdGuard l1("##trk_ngo");
            ElDrawLinkText(elSt,
                           ElLinkSpec{"ngosang/trackerslist",
                                      [&ctx]() { ctx.host.OpenPathInExplorer(kNgosangTrackerUrl); },
                                      13.5f});
            lx = ImGui::GetItemRectMax().x + 14.0f * s;
            ImGui::SetCursorScreenPos(ImVec2(lx, cy));
            IdGuard l2("##trk_xiu2");
            ElDrawLinkText(elSt,
                           ElLinkSpec{"XIU2/TrackersListCollection",
                                      [&ctx]() { ctx.host.OpenPathInExplorer(kXiu2TrackerUrl); },
                                      13.5f});
        }
        cy += 28.0f * s;

        ImGui::SetCursorScreenPos(ImVec2(bx, cy));
        ElCheckbox(elSt, "##adv_auto_sync", ctx.tr.Tr("prefs.auto_sync_tracker"), &ps.autoSyncTracker);
        cy += 28.0f * s;

        FontGuard fg(ctx.fonts, FontRole::Regular, 13.5f);
        const std::string syncStr =
            (ctx.state.prefs.lastSyncTrackerTime > 0)
                ? motrix::domain::FormatUnixTimestampUtc(ctx.state.prefs.lastSyncTrackerTime)
                : std::string(ctx.tr.Tr("prefs.never_synced"));
        ImGui::GetWindowDrawList()->AddText(
            ImVec2(bx, cy), ToAbgrU32(ctx.palette.taskMetaText), syncStr.c_str());
        ImGui::SetCursorScreenPos(ImVec2(bx, cy + 20.0f * s));
        ImGui::Dummy(ImVec2(0.0f, 0.0f));
    });
}

void RenderAdvRpcRow(UiFrameContext& ctx, PrefsFormState& ps) {
    const float s = ctx.dpiScale;
    const ElStyle elSt{ctx.palette, ctx.fonts, s, &ctx.anim};

    DrawPrefsFormRow(ctx, ctx.tr.Tr("prefs.rpc"), 24.0f * s, [&](float bx, float bw) {
        float cy = ImGui::GetCursorScreenPos().y;
        const float portW = std::min(bw, 220.0f * s);
        const float fullW = std::min(bw, 460.0f * s);
        DrawSubCaption(ctx, bx, cy, ctx.tr.Tr("prefs.rpc_port"));
        cy += 24.0f * s;
        DrawPortCompositeField(ctx,
                               PortFieldSpec{"##adv_rpc_port", ps.rpcPort, bx, cy, portW, 10000, 60000});
        cy += 44.0f * s;

        DrawSubCaption(ctx, bx, cy, ctx.tr.Tr("prefs.rpc_secret"));
        cy += 24.0f * s;
        ImGui::SetCursorScreenPos(ImVec2(bx, cy));
        const ElCompositeResult secRes = ElCompositeInput(elSt,
                                                          ElCompositeSpec{"##adv_rpc_sec",
                                                                          ps.rpcSecretBuffer.data(),
                                                                          ps.rpcSecretBuffer.size(),
                                                                          fullW,
                                                                          std::nullopt,
                                                                          MotrixIcon::Dice,
                                                                          "RPC Secret"});
        if (secRes.rightClicked) {
            CopyUtf8ToBuffer(
                GenerateRandomSecret(12), ps.rpcSecretBuffer.data(), ps.rpcSecretBuffer.size());
        }
        cy += 38.0f * s;
        ImGui::SetCursorScreenPos(ImVec2(bx, cy));
        IdGuard rpcDoc("##adv_rpc_doc");
        ElDrawLinkText(elSt,
                       ElLinkSpec{ctx.tr.Tr("prefs.rpc_secret_tips"),
                                  [&ctx]() { ctx.host.OpenPathInExplorer(kRpcDocUrl); },
                                  13.5f});
        ImGui::SetCursorScreenPos(ImVec2(bx, cy + 22.0f * s));
        ImGui::Dummy(ImVec2(0.0f, 0.0f));
    });
}

void RenderAdvBtNetworkRow(UiFrameContext& ctx, PrefsFormState& ps) {
    const float s = ctx.dpiScale;
    const ElStyle elSt{ctx.palette, ctx.fonts, s, &ctx.anim};

    DrawPrefsFormRow(ctx, ctx.tr.Tr("prefs.bt_network"), 26.0f * s, [&](float bx, float bw) {
        float cy = ImGui::GetCursorScreenPos().y;
        const float portW = std::min(bw, 220.0f * s);
        ImGui::SetCursorScreenPos(ImVec2(bx, cy));
        ElSwitch(elSt, "##adv_upnp", ctx.tr.Tr("prefs.enable_upnp"), &ps.enableUpnp);
        cy += 38.0f * s;

        DrawSubCaption(ctx, bx, cy, ctx.tr.Tr("prefs.bt_port"));
        cy += 24.0f * s;
        DrawPortCompositeField(ctx,
                               PortFieldSpec{"##adv_bt_port", ps.btPort, bx, cy, portW, 20000, 29999});
        cy += 44.0f * s;

        DrawSubCaption(ctx, bx, cy, ctx.tr.Tr("prefs.dht_port"));
        cy += 24.0f * s;
        DrawPortCompositeField(ctx,
                               PortFieldSpec{"##adv_dht_port", ps.dhtPort, bx, cy, portW, 20000, 29999});
        cy += 44.0f * s;

        const std::array<AdvChkItem, 4> netItems{{
            {"##adv_dht", "prefs.enable_dht", &ps.enableDht},
            {"##adv_pex", "prefs.enable_pex", &ps.enablePeerExchange},
            {"##adv_lpd", "prefs.enable_lpd", &ps.btEnableLpd},
            {"##adv_anon", "prefs.bt_anonymous", &ps.btAnonymousMode},
        }};
        DrawAdvCheckboxStack(ctx, bx, cy, netItems);
        ImGui::SetCursorScreenPos(ImVec2(bx, cy - 6.0f * s));
        ImGui::Dummy(ImVec2(0.0f, 0.0f));
    });
}

void RenderAdvSecurityRow(UiFrameContext& ctx, PrefsFormState& ps) {
    const float s = ctx.dpiScale;

    DrawPrefsFormRow(ctx, ctx.tr.Tr("prefs.security_network"), 24.0f * s, [&](float bx, float) {
        float cy = ImGui::GetCursorScreenPos().y;
        const std::array<AdvChkItem, 3> secItems{{
            {"##adv_cert", "prefs.check_cert", &ps.checkCertificate},
            {"##adv_ipv6", "prefs.disable_ipv6", &ps.disableIpv6},
            {"##adv_rtime", "prefs.remote_time", &ps.remoteTime},
        }};
        DrawAdvCheckboxStack(ctx, bx, cy, secItems);
        ImGui::SetCursorScreenPos(ImVec2(bx, cy - 6.0f * s));
        ImGui::Dummy(ImVec2(0.0f, 0.0f));
    });
}

}  // namespace

void RenderPrefsAdvancedTab(UiFrameContext& ctx, PrefsFormState& ps) {
    RenderAdvProxyRow(ctx, ps);
    RenderAdvTrackerRow(ctx, ps);
    RenderAdvRpcRow(ctx, ps);
    RenderAdvBtNetworkRow(ctx, ps);
    RenderAdvSecurityRow(ctx, ps);
    RenderPrefsAdvancedTail(ctx, ps);
}

}  // namespace motrix::ui

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

struct PortFieldSpec {
    const char* id = nullptr;
    int& portVal;
    std::array<char, 16>& portBuf;
    float x = 0.0f;
    float y = 0.0f;
    float width = 0.0f;
    int randLow = 10000;
    int randHigh = 60000;
};

void DrawPortCompositeField(UiFrameContext& ctx, const PortFieldSpec& spec) {
    const float s = ctx.dpiScale;
    ImGui::SetCursorScreenPos(ImVec2(spec.x, spec.y));
    const ElStyle elSt{ctx.palette, ctx.fonts, s, &ctx.anim};
    const ElCompositeResult res = ElCompositeInput(elSt,
                                                   ElCompositeSpec{spec.id,
                                                                   spec.portBuf.data(),
                                                                   spec.portBuf.size(),
                                                                   spec.width,
                                                                   std::nullopt,
                                                                   MotrixIcon::Dice});
    if (res.textChanged) {
        if (const auto parsed = motrix::core::ParseStrictInt(spec.portBuf.data())) {
            if (*parsed >= 1024 && *parsed <= 65535) {
                spec.portVal = *parsed;
            }
        }
    }
    if (res.deactivated) {
        if (const auto parsed = motrix::core::ParseStrictInt(spec.portBuf.data())) {
            spec.portVal = std::clamp(*parsed, 1024, 65535);
        }
        CopyUtf8ToBuffer(std::to_string(spec.portVal), spec.portBuf.data(), spec.portBuf.size());
    }
    if (res.rightClicked) {
        spec.portVal = RandomPortInRange(spec.randLow, spec.randHigh);
        CopyUtf8ToBuffer(std::to_string(spec.portVal), spec.portBuf.data(), spec.portBuf.size());
    }
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
        const std::string timePart =
            (ctx.state.prefs.lastSyncTrackerTime > 0)
                ? motrix::domain::FormatUnixTimestampUtc(ctx.state.prefs.lastSyncTrackerTime)
                : std::string(ctx.tr.Tr("prefs.never_synced"));
        const std::string syncStr = std::format("{} {}", ctx.tr.Tr("prefs.last_sync"), timePart);
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
        DrawPortCompositeField(
            ctx,
            PortFieldSpec{"##adv_rpc_port", ps.rpcPort, ps.rpcPortBuf, bx, cy, portW, 10000, 60000});
        cy += 44.0f * s;

        DrawSubCaption(ctx, bx, cy, ctx.tr.Tr("prefs.rpc_secret"));
        cy += 24.0f * s;
        ImGui::SetCursorScreenPos(ImVec2(bx, cy));
        const ElCompositeResult secRes =
            ElCompositeInput(elSt,
                             ElCompositeSpec{"##adv_rpc_sec",
                                             ps.rpcSecretBuffer.data(),
                                             ps.rpcSecretBuffer.size(),
                                             fullW,
                                             std::nullopt,
                                             MotrixIcon::Dice,
                                             ctx.tr.Tr("prefs.rpc_secret")});
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
        DrawPortCompositeField(
            ctx, PortFieldSpec{"##adv_bt_port", ps.btPort, ps.btPortBuf, bx, cy, portW, 20000, 29999});
        cy += 44.0f * s;

        DrawSubCaption(ctx, bx, cy, ctx.tr.Tr("prefs.dht_port"));
        cy += 24.0f * s;
        DrawPortCompositeField(
            ctx,
            PortFieldSpec{"##adv_dht_port", ps.dhtPort, ps.dhtPortBuf, bx, cy, portW, 20000, 29999});
        cy += 44.0f * s;

        const std::array<PrefsCheckboxItem, 4> netItems{{
            {"##adv_dht", "prefs.enable_dht", &ps.enableDht},
            {"##adv_pex", "prefs.enable_pex", &ps.enablePeerExchange},
            {"##adv_lpd", "prefs.enable_lpd", &ps.btEnableLpd},
            {"##adv_anon", "prefs.bt_anonymous", &ps.btAnonymousMode},
        }};
        DrawPrefsCheckboxStack(ctx, bx, cy, netItems);
        ImGui::SetCursorScreenPos(ImVec2(bx, cy - 6.0f * s));
        ImGui::Dummy(ImVec2(0.0f, 0.0f));
    });
}

void RenderAdvSecurityRow(UiFrameContext& ctx, PrefsFormState& ps) {
    const float s = ctx.dpiScale;

    DrawPrefsFormRow(ctx, ctx.tr.Tr("prefs.security_network"), 24.0f * s, [&](float bx, float) {
        float cy = ImGui::GetCursorScreenPos().y;
        const std::array<PrefsCheckboxItem, 3> secItems{{
            {"##adv_cert", "prefs.check_cert", &ps.checkCertificate},
            {"##adv_ipv6", "prefs.disable_ipv6", &ps.disableIpv6},
            {"##adv_rtime", "prefs.remote_time", &ps.remoteTime},
        }};
        DrawPrefsCheckboxStack(ctx, bx, cy, secItems);
        ImGui::SetCursorScreenPos(ImVec2(bx, cy - 6.0f * s));
        ImGui::Dummy(ImVec2(0.0f, 0.0f));
    });
}

void RenderUserAgentButtons(const UiFrameContext& ctx, PrefsFormState& ps, float x, float y) {
    struct Preset {
        std::string_view label;
        std::string_view ua;
    };
    static constexpr std::array<Preset, 4> kPresets{{
        {"Aria2", "aria2/1.36.0"},
        {"Transmission", "Transmission/3.00"},
        {"Chrome",
         "Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) AppleWebKit/537.36 "
         "(KHTML, like Gecko) Chrome/111.0.0.0 Safari/537.36"},
        {"du", "netdisk;6.0.0.12;PC;PC-Windows;10.0.16299;WindowsBaiduYunGuanJia"},
    }};

    const float s = ctx.dpiScale;
    const float h = 32.0f * s;
    FontGuard fg(ctx.fonts, FontRole::Medium, 13.5f);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    float curX = x;

    for (std::size_t i = 0; i < kPresets.size(); ++i) {
        const auto lbl = kPresets[i].label;
        const ImVec2 tSz = ImGui::CalcTextSize(lbl.data(), lbl.data() + lbl.size());
        const float w = tSz.x + 28.0f * s;
        const ImVec2 p0(curX, y);
        const ImVec2 p1(curX + w, y + h);
        ImGui::SetCursorScreenPos(p0);
        if (ImGui::InvisibleButton(std::format("##ua_preset_{}", i).c_str(), ImVec2(w, h))) {
            CopyUtf8ToBuffer(kPresets[i].ua, ps.userAgentBuffer.data(), ps.userAgentBuffer.size());
        }
        const bool hov = ImGui::IsItemHovered();
        if (hov) {
            ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
        }
        ImDrawFlags roundFlags = ImDrawFlags_RoundCornersNone;
        if (i == 0) {
            roundFlags = ImDrawFlags_RoundCornersLeft;
        } else if (i + 1 == kPresets.size()) {
            roundFlags = ImDrawFlags_RoundCornersRight;
        }
        dl->AddRectFilled(
            p0,
            p1,
            ToAbgrU32(hov ? WithAlpha(ctx.palette.primary, ctx.palette.isDark ? 0.14f : 0.08f)
                          : ctx.palette.inputBg),
            4.0f * s,
            roundFlags);
        dl->AddRect(p0,
                    p1,
                    ToAbgrU32(hov ? ctx.palette.primary : ctx.palette.inputBorder),
                    4.0f * s,
                    roundFlags,
                    1.0f);
        dl->AddText(ImVec2(p0.x + (w - tSz.x) * 0.5f, p0.y + (h - tSz.y) * 0.5f),
                    ToAbgrU32(hov ? ctx.palette.primary : ctx.palette.panelTitle),
                    lbl.data(),
                    lbl.data() + lbl.size());
        curX += w - 1.0f;
    }
}

float DrawReadonlyPathRow(UiFrameContext& ctx,
                          const char* id,
                          std::string_view caption,
                          std::string_view path,
                          float x,
                          float y,
                          float width) {
    const float s = ctx.dpiScale;
    DrawSubCaption(ctx, x, y, caption);
    const float fieldY = y + 24.0f * s;
    std::array<char, 512> buf{};
    CopyUtf8ToBuffer(path, buf.data(), buf.size());
    ImGui::SetCursorScreenPos(ImVec2(x, fieldY));
    const ElStyle elSt{ctx.palette, ctx.fonts, s, &ctx.anim};
    const ElCompositeResult res = ElCompositeInput(
        elSt,
        ElCompositeSpec{id, buf.data(), buf.size(), width, std::nullopt, MotrixIcon::Folder, {}, true});
    if (res.rightClicked && !path.empty()) {
        ctx.host.OpenPathInExplorer(path);
    }
    return fieldY + 42.0f * s;
}

void RenderAdvProtocolRow(UiFrameContext& ctx, PrefsFormState& ps) {
    const float s = ctx.dpiScale;
    const ElStyle elSt{ctx.palette, ctx.fonts, s, &ctx.anim};

    DrawPrefsFormRow(ctx, ctx.tr.Tr("prefs.download_protocol"), 24.0f * s, [&](float bx, float) {
        float cy = ImGui::GetCursorScreenPos().y;
        DrawSubCaption(ctx, bx, cy, ctx.tr.Tr("prefs.protocols_default_client"));
        cy += 28.0f * s;
        ImGui::SetCursorScreenPos(ImVec2(bx, cy));
        ElSwitch(elSt, "##proto_mag", ctx.tr.Tr("prefs.proto_magnet"), &ps.protocolMagnet);
        cy += 34.0f * s;
        ImGui::SetCursorScreenPos(ImVec2(bx, cy));
        ElSwitch(elSt, "##proto_th", ctx.tr.Tr("prefs.proto_thunder"), &ps.protocolThunder);
        cy += 34.0f * s;
        ImGui::SetCursorScreenPos(ImVec2(bx, cy));
        ElSwitch(elSt, "##proto_mx", ctx.tr.Tr("prefs.proto_motrix"), &ps.protocolMotrix);
        cy += 34.0f * s;
        ImGui::SetCursorScreenPos(ImVec2(bx, cy));
        ElSwitch(elSt, "##proto_tor", ctx.tr.Tr("prefs.proto_torrent"), &ps.protocolTorrent);
        ImGui::SetCursorScreenPos(ImVec2(bx, cy + 28.0f * s));
        ImGui::Dummy(ImVec2(0.0f, 0.0f));
    });
}

void RenderAdvUserAgentRow(UiFrameContext& ctx, PrefsFormState& ps) {
    const float s = ctx.dpiScale;
    const ElStyle elSt{ctx.palette, ctx.fonts, s, &ctx.anim};

    DrawPrefsFormRow(ctx, ctx.tr.Tr("prefs.user_agent"), 24.0f * s, [&](float bx, float bw) {
        float cy = ImGui::GetCursorScreenPos().y;
        const float fieldW = std::min(bw, 490.0f * s);
        DrawSubCaption(ctx, bx, cy, ctx.tr.Tr("prefs.mock_user_agent"));
        cy += 26.0f * s;
        ImGui::SetCursorScreenPos(ImVec2(bx, cy));
        ElInputTextMultiline(elSt,
                             ElTextInputSpec{"##adv_ua",
                                             ps.userAgentBuffer.data(),
                                             ps.userAgentBuffer.size(),
                                             fieldW,
                                             {},
                                             0,
                                             60.0f * s});
        cy += 68.0f * s;
        RenderUserAgentButtons(ctx, ps, bx, cy);
        ImGui::SetCursorScreenPos(ImVec2(bx, cy + 34.0f * s));
        ImGui::Dummy(ImVec2(0.0f, 0.0f));
    });
}

void RenderAdvDeveloperRow(UiFrameContext& ctx, PrefsFormState& ps) {
    const float s = ctx.dpiScale;
    const ElStyle elSt{ctx.palette, ctx.fonts, s, &ctx.anim};

    DrawPrefsFormRow(ctx, ctx.tr.Tr("prefs.developer"), 24.0f * s, [&](float bx, float bw) {
        float cy = ImGui::GetCursorScreenPos().y;
        const float fieldW = std::min(bw, 460.0f * s);

        DrawSubCaption(ctx, bx, cy, ctx.tr.Tr("prefs.log_level"));
        cy += 24.0f * s;
        const std::array<const char*, 4> logItems{
            ctx.tr.Tr("prefs.log_debug").data(),
            ctx.tr.Tr("prefs.log_info").data(),
            ctx.tr.Tr("prefs.log_warn").data(),
            ctx.tr.Tr("prefs.log_error").data(),
        };
        ImGui::SetCursorScreenPos(ImVec2(bx, cy));
        ElSelectCombo(
            elSt,
            ElSelectSpec{"##dev_log_level", &ps.logLevelIndex, logItems, std::min(bw, 220.0f * s)});
        cy += 44.0f * s;

        cy = DrawReadonlyPathRow(ctx,
                                 "##dev_conf",
                                 ctx.tr.Tr("prefs.aria2_conf_path"),
                                 ctx.state.engineConfPath,
                                 bx,
                                 cy,
                                 fieldW);
        cy = DrawReadonlyPathRow(ctx,
                                 "##dev_sess",
                                 ctx.tr.Tr("prefs.session_path"),
                                 ctx.state.sessionFilePath,
                                 bx,
                                 cy,
                                 fieldW);
        cy = DrawReadonlyPathRow(
            ctx, "##dev_log", ctx.tr.Tr("prefs.log_path"), ctx.state.appLogPath, bx, cy, fieldW);

        cy += 6.0f * s;
        float btn1W = 148.0f * s;
        float btn2W = 128.0f * s;
        {
            FontGuard bf(ctx.fonts, FontRole::Regular, 14.5f);
            const std::string_view l1 = ctx.tr.Tr("prefs.session_reset");
            const std::string_view l2 = ctx.tr.Tr("prefs.factory_reset");
            btn1W = ImGui::CalcTextSize(l1.data(), l1.data() + l1.size()).x + 30.0f * s;
            btn2W = ImGui::CalcTextSize(l2.data(), l2.data() + l2.size()).x + 30.0f * s;
        }
        ImGui::SetCursorScreenPos(ImVec2(bx, cy));
        if (ElSecondaryButton(
                elSt,
                ElButtonSpec{"##dev_rst_sess", ctx.tr.Tr("prefs.session_reset"), btn1W, 34.0f * s})) {
            ctx.sink.Dispatch(motrix::app::ResetSessionCmd{});
            SetPrefsFeedback(ps, 3, true);
            ctx.host.NotifyAnimationActive();
        }
        ImGui::SetCursorScreenPos(ImVec2(bx + btn1W + 12.0f * s, cy));
        if (ElSecondaryButton(
                elSt,
                ElButtonSpec{"##dev_fact_rst", ctx.tr.Tr("prefs.factory_reset"), btn2W, 34.0f * s})) {
            ctx.sink.Dispatch(motrix::app::FactoryResetCmd{});
            ps.initialized = false;
            SetPrefsFeedback(ps, 4, true);
            ctx.host.NotifyAnimationActive();
        }
        ImGui::SetCursorScreenPos(ImVec2(bx, cy + 36.0f * s));
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
    RenderAdvProtocolRow(ctx, ps);
    RenderAdvUserAgentRow(ctx, ps);
    RenderAdvDeveloperRow(ctx, ps);
}

}  // namespace motrix::ui

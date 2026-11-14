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

int RandomPortInRange(int low, int high) {
    static std::mt19937 rng{std::random_device{}()};
    std::uniform_int_distribution<int> dist(low, high);
    return dist(rng);
}

std::string GenerateRandomSecret(std::size_t len) {
    static constexpr std::string_view kChars =
        "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";
    static std::mt19937 rng{std::random_device{}()};
    std::uniform_int_distribution<std::size_t> dist(0, kChars.size() - 1);
    std::string out;
    out.reserve(len);
    for (std::size_t i = 0; i < len; ++i) {
        out.push_back(kChars[dist(rng)]);
    }
    return out;
}

void DrawSubCaption(const UiFrameContext& ctx, float x, float y, std::string_view txt) {
    FontGuard fg(ctx.fonts, FontRole::Regular, 13.0f);
    ImGui::GetWindowDrawList()->AddText(
        ImVec2(x, y), ToAbgrU32(ctx.palette.subLabelText), txt.data(), txt.data() + txt.size());
}

void DrawPortCompositeField(const UiFrameContext& ctx,
                            const char* id,
                            int& portVal,
                            float x,
                            float y,
                            float width,
                            int randLow,
                            int randHigh) {
    const float s = ctx.dpiScale;
    std::array<char, 16> buf{};
    std::snprintf(buf.data(), buf.size(), "%d", portVal);
    ImGui::SetCursorScreenPos(ImVec2(x, y));
    const ElCompositeResult res = ElCompositeInput(
        id, buf.data(), buf.size(), width, ctx.palette, ctx.fonts, s, std::nullopt, MotrixIcon::Dice);
    if (res.textChanged) {
        int parsed = portVal;
        const std::string_view sv(buf.data());
        auto [ptr, ec] = std::from_chars(sv.data(), sv.data() + sv.size(), parsed);
        if (ec == std::errc{}) {
            portVal = std::clamp(parsed, 1, 65535);
        }
    }
    if (res.rightClicked) {
        portVal = RandomPortInRange(randLow, randHigh);
    }
}

void DrawSyncSquareButton(const UiFrameContext& ctx,
                          const char* id,
                          float x,
                          float y,
                          bool syncing,
                          const std::function<void()>& onClick) {
    const float s = ctx.dpiScale;
    const float w = 36.0f * s;
    const float h = 30.0f * s;
    ImGui::SetCursorScreenPos(ImVec2(x, y));
    const bool clicked = ImGui::InvisibleButton(id, ImVec2(w, h));
    const bool hov = !syncing && ImGui::IsItemHovered();
    if (hov) {
        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    }
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(ImVec2(x, y), ImVec2(x + w, y + h), ToAbgrU32(ctx.palette.inputBg), 4.0f * s);
    dl->AddRect(ImVec2(x, y),
                ImVec2(x + w, y + h),
                ToAbgrU32(hov ? ctx.palette.primary : ctx.palette.inputBorder),
                4.0f * s,
                0,
                1.0f);
    const float iconSz = 14.0f * s;
    DrawMotrixIcon(MotrixIcon::Sync,
                   x + (w - iconSz) * 0.5f,
                   y + (h - iconSz) * 0.5f,
                   iconSz,
                   iconSz,
                   hov ? ctx.palette.primary : ctx.palette.subLabelText,
                   s);
    if (clicked && !syncing && onClick) {
        onClick();
    }
}

}  // namespace

void RenderPrefsAdvancedTab(UiFrameContext& ctx, PrefsFormState& ps) {
    const float s = ctx.dpiScale;

    DrawPrefsFormRow(ctx, ctx.tr.Tr("prefs.auto_update"), 20.0f * s, [&](float bx, float) {
        float cy = ImGui::GetCursorScreenPos().y;
        ImGui::SetCursorScreenPos(ImVec2(bx, cy));
        ElCheckbox("##adv_auto_upd",
                   ctx.tr.Tr("prefs.auto_check_update"),
                   &ps.autoCheckUpdate,
                   ctx.palette,
                   ctx.fonts,
                   s);
        cy += 26.0f * s;

        const std::string timeStr =
            (ctx.state.prefs.lastCheckUpdateTime > 0)
                ? motrix::domain::FormatUnixTimestampUtc(ctx.state.prefs.lastCheckUpdateTime)
                : "--";
        const std::string tip = std::format("{} {}", ctx.tr.Tr("prefs.last_check_update"), timeStr);
        FontGuard fg(ctx.fonts, FontRole::Regular, 12.5f);
        const ImVec2 sz = ImGui::CalcTextSize(tip.c_str());
        ImGui::GetWindowDrawList()->AddText(
            ImVec2(bx, cy), ToAbgrU32(ctx.palette.taskMetaText), tip.c_str());
        ImGui::SetCursorScreenPos(ImVec2(bx + sz.x + 10.0f * s, cy));
        IdGuard chkId("##adv_chk_now");
        ElDrawLinkText(
            ctx.palette,
            ctx.fonts,
            s,
            ctx.tr.Tr("prefs.check_now"),
            [&ctx]() { ctx.host.OpenPathInExplorer("https://motrix.app/release"); },
            12.5f,
            true);
        ImGui::SetCursorScreenPos(ImVec2(bx, cy + 20.0f * s));
        ImGui::Dummy(ImVec2(0.0f, 0.0f));
    });

    DrawPrefsFormRow(ctx, ctx.tr.Tr("prefs.proxy"), 24.0f * s, [&](float bx, float bw) {
        float cy = ImGui::GetCursorScreenPos().y;
        const float fieldW = std::min(bw, 450.0f * s);
        ImGui::SetCursorScreenPos(ImVec2(bx, cy));
        ElSwitch(
            "##adv_proxy_en", ctx.tr.Tr("prefs.use_proxy"), &ps.useProxy, ctx.palette, ctx.fonts, s);
        cy += 34.0f * s;
        if (ps.useProxy) {
            DrawSubCaption(ctx, bx, cy, ctx.tr.Tr("prefs.all_proxy"));
            cy += 22.0f * s;
            ImGui::SetCursorScreenPos(ImVec2(bx, cy));
            ElInputText("##adv_all_proxy",
                        ps.allProxyBuffer.data(),
                        ps.allProxyBuffer.size(),
                        fieldW,
                        ctx.palette,
                        ctx.fonts,
                        s,
                        "[http://][USER:PASSWORD@]HOST[:PORT]");
            cy += 40.0f * s;
            DrawSubCaption(ctx, bx, cy, ctx.tr.Tr("prefs.no_proxy"));
            cy += 22.0f * s;
            ImGui::SetCursorScreenPos(ImVec2(bx, cy));
            ElInputText("##adv_no_proxy",
                        ps.noProxyBuffer.data(),
                        ps.noProxyBuffer.size(),
                        fieldW,
                        ctx.palette,
                        ctx.fonts,
                        s,
                        "localhost,127.0.0.1");
            cy += 36.0f * s;
        }
        ImGui::SetCursorScreenPos(ImVec2(bx, cy - 6.0f * s));
        ImGui::Dummy(ImVec2(0.0f, 0.0f));
    });

    DrawPrefsFormRow(ctx, ctx.tr.Tr("prefs.tracker_sources"), 30.0f * s, [&](float bx, float bw) {
        float cy = ImGui::GetCursorScreenPos().y;
        const float fieldW = std::min(bw, 480.0f * s);
        const float selW = std::max(140.0f * s, fieldW - 44.0f * s);

        ImGui::SetCursorScreenPos(ImVec2(bx, cy));
        ElTrackerTagSelect("##adv_trk_sel", ps.trackerSourceSelected, selW, ctx.palette, ctx.fonts, s);
        const float afterSelY = ImGui::GetItemRectMax().y;

        DrawSyncSquareButton(
            ctx, "##adv_trk_sync", bx + selW + 8.0f * s, cy, ctx.state.isSyncingTrackers, [&]() {
                const auto allOpts = motrix::domain::AllTrackerSourceOptions();
                std::vector<std::string> urls;
                for (std::size_t i = 0; i < allOpts.size() && i < ps.trackerSourceSelected.size(); ++i) {
                    if (ps.trackerSourceSelected[i]) {
                        urls.emplace_back(allOpts[i].url);
                    }
                }
                ctx.sink.Dispatch(motrix::app::SyncTrackersCmd{std::move(urls)});
            });
        cy = afterSelY + 10.0f * s;

        ImGui::SetCursorScreenPos(ImVec2(bx, cy));
        ElInputTextMultiline("##adv_bt_trk",
                             ps.btTrackerBuffer.data(),
                             ps.btTrackerBuffer.size(),
                             fieldW,
                             88.0f * s,
                             ctx.palette,
                             ctx.fonts,
                             s,
                             ctx.tr.Tr("prefs.bt_tracker_tips"));
        cy += 96.0f * s;

        {
            FontGuard fg(ctx.fonts, FontRole::Regular, 12.0f);
            const std::string_view rec = ctx.tr.Tr("prefs.recommended");
            const ImVec2 recSz = ImGui::CalcTextSize(rec.data(), rec.data() + rec.size());
            ImGui::GetWindowDrawList()->AddText(ImVec2(bx, cy),
                                                ToAbgrU32(ctx.palette.taskMetaText),
                                                rec.data(),
                                                rec.data() + rec.size());
            float lx = bx + recSz.x + 8.0f * s;
            ImGui::SetCursorScreenPos(ImVec2(lx, cy));
            IdGuard l1("##trk_ngo");
            ElDrawLinkText(
                ctx.palette,
                ctx.fonts,
                s,
                "ngosang/trackerslist",
                [&ctx]() { ctx.host.OpenPathInExplorer("https://github.com/ngosang/trackerslist"); },
                12.0f);
            lx = ImGui::GetItemRectMax().x + 14.0f * s;
            ImGui::SetCursorScreenPos(ImVec2(lx, cy));
            IdGuard l2("##trk_xiu2");
            ElDrawLinkText(
                ctx.palette,
                ctx.fonts,
                s,
                "XIU2/TrackersListCollection",
                [&ctx]() {
                    ctx.host.OpenPathInExplorer("https://github.com/XIU2/TrackersListCollection");
                },
                12.0f);
        }
        cy += 26.0f * s;

        ImGui::SetCursorScreenPos(ImVec2(bx, cy));
        ElCheckbox("##adv_auto_sync",
                   ctx.tr.Tr("prefs.auto_sync_tracker"),
                   &ps.autoSyncTracker,
                   ctx.palette,
                   ctx.fonts,
                   s);
        cy += 24.0f * s;

        FontGuard fg(ctx.fonts, FontRole::Regular, 12.0f);
        const std::string syncStr =
            (ctx.state.prefs.lastSyncTrackerTime > 0)
                ? motrix::domain::FormatUnixTimestampUtc(ctx.state.prefs.lastSyncTrackerTime)
                : std::string(ctx.tr.Tr("prefs.never_synced"));
        ImGui::GetWindowDrawList()->AddText(
            ImVec2(bx, cy), ToAbgrU32(ctx.palette.taskMetaText), syncStr.c_str());
        ImGui::SetCursorScreenPos(ImVec2(bx, cy + 18.0f * s));
        ImGui::Dummy(ImVec2(0.0f, 0.0f));
    });

    DrawPrefsFormRow(ctx, ctx.tr.Tr("prefs.rpc"), 20.0f * s, [&](float bx, float bw) {
        float cy = ImGui::GetCursorScreenPos().y;
        const float portW = std::min(bw, 220.0f * s);
        const float fullW = std::min(bw, 450.0f * s);
        DrawSubCaption(ctx, bx, cy, ctx.tr.Tr("prefs.rpc_port"));
        cy += 22.0f * s;
        DrawPortCompositeField(ctx, "##adv_rpc_port", ps.rpcPort, bx, cy, portW, 10000, 60000);
        cy += 42.0f * s;

        DrawSubCaption(ctx, bx, cy, ctx.tr.Tr("prefs.rpc_secret"));
        cy += 22.0f * s;
        ImGui::SetCursorScreenPos(ImVec2(bx, cy));
        const ElCompositeResult secRes = ElCompositeInput("##adv_rpc_sec",
                                                          ps.rpcSecretBuffer.data(),
                                                          ps.rpcSecretBuffer.size(),
                                                          fullW,
                                                          ctx.palette,
                                                          ctx.fonts,
                                                          s,
                                                          std::nullopt,
                                                          MotrixIcon::Dice,
                                                          "RPC Secret");
        if (secRes.rightClicked) {
            CopyUtf8ToBuffer(
                GenerateRandomSecret(12), ps.rpcSecretBuffer.data(), ps.rpcSecretBuffer.size());
        }
        cy += 36.0f * s;
        ImGui::SetCursorScreenPos(ImVec2(bx, cy));
        IdGuard rpcDoc("##adv_rpc_doc");
        ElDrawLinkText(
            ctx.palette,
            ctx.fonts,
            s,
            ctx.tr.Tr("prefs.rpc_secret_tips"),
            [&ctx]() { ctx.host.OpenPathInExplorer("https://motrix.app/release"); },
            12.0f);
        ImGui::SetCursorScreenPos(ImVec2(bx, cy + 20.0f * s));
        ImGui::Dummy(ImVec2(0.0f, 0.0f));
    });

    DrawPrefsFormRow(ctx, ctx.tr.Tr("prefs.listen_ports"), 24.0f * s, [&](float bx, float bw) {
        float cy = ImGui::GetCursorScreenPos().y;
        const float portW = std::min(bw, 220.0f * s);
        ImGui::SetCursorScreenPos(ImVec2(bx, cy));
        ElSwitch(
            "##adv_upnp", ctx.tr.Tr("prefs.enable_upnp"), &ps.enableUpnp, ctx.palette, ctx.fonts, s);
        cy += 36.0f * s;

        DrawSubCaption(ctx, bx, cy, ctx.tr.Tr("prefs.bt_port"));
        cy += 22.0f * s;
        DrawPortCompositeField(ctx, "##adv_bt_port", ps.btPort, bx, cy, portW, 20000, 29999);
        cy += 42.0f * s;

        DrawSubCaption(ctx, bx, cy, ctx.tr.Tr("prefs.dht_port"));
        cy += 22.0f * s;
        DrawPortCompositeField(ctx, "##adv_dht_port", ps.dhtPort, bx, cy, portW, 20000, 29999);
        ImGui::SetCursorScreenPos(ImVec2(bx, cy + 32.0f * s));
        ImGui::Dummy(ImVec2(0.0f, 0.0f));
    });

    RenderPrefsAdvancedTail(ctx, ps);
}

}  // namespace motrix::ui

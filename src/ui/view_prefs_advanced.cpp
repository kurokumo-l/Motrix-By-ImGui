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
    FontGuard fg(ctx.fonts, FontRole::Regular, 13.0f);
    ImGui::GetWindowDrawList()->AddText(
        ImVec2(x, y), ToAbgrU32(ctx.palette.subLabelText), txt.data(), txt.data() + txt.size());
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

void DrawPortCompositeField(const UiFrameContext& ctx, const PortFieldSpec& spec) {
    const float s = ctx.dpiScale;
    std::array<char, 16> buf{};
    CopyUtf8ToBuffer(std::to_string(spec.portVal), buf.data(), buf.size());
    ImGui::SetCursorScreenPos(ImVec2(spec.x, spec.y));
    const ElStyle elSt{ctx.palette, ctx.fonts, s};
    const ElCompositeResult res = ElCompositeInput(
        elSt,
        ElCompositeSpec{spec.id, buf.data(), buf.size(), spec.width, std::nullopt, MotrixIcon::Dice});
    if (res.textChanged) {
        int parsed = spec.portVal;
        const std::string_view sv(buf.data());
        auto [ptr, ec] = std::from_chars(sv.data(), sv.data() + sv.size(), parsed);
        if (ec == std::errc{}) {
            spec.portVal = std::clamp(parsed, 1, 65535);
        }
    }
    if (res.rightClicked) {
        spec.portVal = RandomPortInRange(spec.randLow, spec.randHigh);
    }
}

struct SyncBtnSpec {
    const char* id = nullptr;
    float x = 0.0f;
    float y = 0.0f;
    bool syncing = false;
    std::function<void()> onClick{};
};

void DrawSyncSquareButton(const UiFrameContext& ctx, const SyncBtnSpec& spec) {
    const float s = ctx.dpiScale;
    const float w = 36.0f * s;
    const float h = 30.0f * s;
    ImGui::SetCursorScreenPos(ImVec2(spec.x, spec.y));
    const bool clicked = ImGui::InvisibleButton(spec.id, ImVec2(w, h));
    const bool hov = !spec.syncing && ImGui::IsItemHovered();
    if (hov) {
        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    }
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(ImVec2(spec.x, spec.y),
                      ImVec2(spec.x + w, spec.y + h),
                      ToAbgrU32(ctx.palette.inputBg),
                      4.0f * s);
    dl->AddRect(ImVec2(spec.x, spec.y),
                ImVec2(spec.x + w, spec.y + h),
                ToAbgrU32(hov ? ctx.palette.primary : ctx.palette.inputBorder),
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
        hov ? ctx.palette.primary : ctx.palette.subLabelText,
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

void RenderAdvAutoUpdateRow(UiFrameContext& ctx, PrefsFormState& ps) {
    const float s = ctx.dpiScale;
    const ElStyle elSt{ctx.palette, ctx.fonts, s};

    DrawPrefsFormRow(ctx, ctx.tr.Tr("prefs.auto_update"), 20.0f * s, [&](float bx, float) {
        float cy = ImGui::GetCursorScreenPos().y;
        ImGui::SetCursorScreenPos(ImVec2(bx, cy));
        ElCheckbox(elSt, "##adv_auto_upd", ctx.tr.Tr("prefs.auto_check_update"), &ps.autoCheckUpdate);
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
            elSt,
            ElLinkSpec{ctx.tr.Tr("prefs.check_now"),
                       [&ctx]() { ctx.host.OpenPathInExplorer("https://motrix.app/release"); },
                       12.5f,
                       true});
        ImGui::SetCursorScreenPos(ImVec2(bx, cy + 20.0f * s));
        ImGui::Dummy(ImVec2(0.0f, 0.0f));
    });
}

void RenderAdvProxyRow(UiFrameContext& ctx, PrefsFormState& ps) {
    const float s = ctx.dpiScale;
    const ElStyle elSt{ctx.palette, ctx.fonts, s};

    DrawPrefsFormRow(ctx, ctx.tr.Tr("prefs.proxy"), 24.0f * s, [&](float bx, float bw) {
        float cy = ImGui::GetCursorScreenPos().y;
        const float fieldW = std::min(bw, 450.0f * s);
        ImGui::SetCursorScreenPos(ImVec2(bx, cy));
        ElSwitch(elSt, "##adv_proxy_en", ctx.tr.Tr("prefs.use_proxy"), &ps.useProxy);
        cy += 34.0f * s;
        if (ps.useProxy) {
            DrawSubCaption(ctx, bx, cy, ctx.tr.Tr("prefs.all_proxy"));
            cy += 22.0f * s;
            ImGui::SetCursorScreenPos(ImVec2(bx, cy));
            ElInputText(elSt,
                        ElTextInputSpec{"##adv_all_proxy",
                                        ps.allProxyBuffer.data(),
                                        ps.allProxyBuffer.size(),
                                        fieldW,
                                        "[http://][USER:PASSWORD@]HOST[:PORT]"});
            cy += 40.0f * s;
            DrawSubCaption(ctx, bx, cy, ctx.tr.Tr("prefs.no_proxy"));
            cy += 22.0f * s;
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
    const ElStyle elSt{ctx.palette, ctx.fonts, s};

    DrawPrefsFormRow(ctx, ctx.tr.Tr("prefs.tracker_sources"), 30.0f * s, [&](float bx, float bw) {
        float cy = ImGui::GetCursorScreenPos().y;
        const float fieldW = std::min(bw, 480.0f * s);
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
        cy = afterSelY + 10.0f * s;

        ImGui::SetCursorScreenPos(ImVec2(bx, cy));
        ElInputTextMultiline(elSt,
                             ElTextInputSpec{"##adv_bt_trk",
                                             ps.btTrackerBuffer.data(),
                                             ps.btTrackerBuffer.size(),
                                             fieldW,
                                             ctx.tr.Tr("prefs.bt_tracker_tips"),
                                             0,
                                             88.0f * s});
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
                elSt,
                ElLinkSpec{
                    "ngosang/trackerslist",
                    [&ctx]() { ctx.host.OpenPathInExplorer("https://github.com/ngosang/trackerslist"); },
                    12.0f});
            lx = ImGui::GetItemRectMax().x + 14.0f * s;
            ImGui::SetCursorScreenPos(ImVec2(lx, cy));
            IdGuard l2("##trk_xiu2");
            ElDrawLinkText(elSt,
                           ElLinkSpec{"XIU2/TrackersListCollection",
                                      [&ctx]() {
                                          ctx.host.OpenPathInExplorer(
                                              "https://github.com/XIU2/TrackersListCollection");
                                      },
                                      12.0f});
        }
        cy += 26.0f * s;

        ImGui::SetCursorScreenPos(ImVec2(bx, cy));
        ElCheckbox(elSt, "##adv_auto_sync", ctx.tr.Tr("prefs.auto_sync_tracker"), &ps.autoSyncTracker);
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
}

void RenderAdvRpcRow(UiFrameContext& ctx, PrefsFormState& ps) {
    const float s = ctx.dpiScale;
    const ElStyle elSt{ctx.palette, ctx.fonts, s};

    DrawPrefsFormRow(ctx, ctx.tr.Tr("prefs.rpc"), 20.0f * s, [&](float bx, float bw) {
        float cy = ImGui::GetCursorScreenPos().y;
        const float portW = std::min(bw, 220.0f * s);
        const float fullW = std::min(bw, 450.0f * s);
        DrawSubCaption(ctx, bx, cy, ctx.tr.Tr("prefs.rpc_port"));
        cy += 22.0f * s;
        DrawPortCompositeField(ctx,
                               PortFieldSpec{"##adv_rpc_port", ps.rpcPort, bx, cy, portW, 10000, 60000});
        cy += 42.0f * s;

        DrawSubCaption(ctx, bx, cy, ctx.tr.Tr("prefs.rpc_secret"));
        cy += 22.0f * s;
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
        cy += 36.0f * s;
        ImGui::SetCursorScreenPos(ImVec2(bx, cy));
        IdGuard rpcDoc("##adv_rpc_doc");
        ElDrawLinkText(
            elSt,
            ElLinkSpec{ctx.tr.Tr("prefs.rpc_secret_tips"),
                       [&ctx]() { ctx.host.OpenPathInExplorer("https://motrix.app/release"); },
                       12.0f});
        ImGui::SetCursorScreenPos(ImVec2(bx, cy + 20.0f * s));
        ImGui::Dummy(ImVec2(0.0f, 0.0f));
    });
}

void RenderAdvListenPortsRow(UiFrameContext& ctx, PrefsFormState& ps) {
    const float s = ctx.dpiScale;
    const ElStyle elSt{ctx.palette, ctx.fonts, s};

    DrawPrefsFormRow(ctx, ctx.tr.Tr("prefs.listen_ports"), 24.0f * s, [&](float bx, float bw) {
        float cy = ImGui::GetCursorScreenPos().y;
        const float portW = std::min(bw, 220.0f * s);
        ImGui::SetCursorScreenPos(ImVec2(bx, cy));
        ElSwitch(elSt, "##adv_upnp", ctx.tr.Tr("prefs.enable_upnp"), &ps.enableUpnp);
        cy += 36.0f * s;

        DrawSubCaption(ctx, bx, cy, ctx.tr.Tr("prefs.bt_port"));
        cy += 22.0f * s;
        DrawPortCompositeField(ctx,
                               PortFieldSpec{"##adv_bt_port", ps.btPort, bx, cy, portW, 20000, 29999});
        cy += 42.0f * s;

        DrawSubCaption(ctx, bx, cy, ctx.tr.Tr("prefs.dht_port"));
        cy += 22.0f * s;
        DrawPortCompositeField(ctx,
                               PortFieldSpec{"##adv_dht_port", ps.dhtPort, bx, cy, portW, 20000, 29999});
        ImGui::SetCursorScreenPos(ImVec2(bx, cy + 32.0f * s));
        ImGui::Dummy(ImVec2(0.0f, 0.0f));
    });
}

}  // namespace

void RenderPrefsAdvancedTab(UiFrameContext& ctx, PrefsFormState& ps) {
    RenderAdvAutoUpdateRow(ctx, ps);
    RenderAdvProxyRow(ctx, ps);
    RenderAdvTrackerRow(ctx, ps);
    RenderAdvRpcRow(ctx, ps);
    RenderAdvListenPortsRow(ctx, ps);
    RenderPrefsAdvancedTail(ctx, ps);
}

}  // namespace motrix::ui

module;

#include "imgui.h"

module motrix.ui;

import std;
import motrix.platform;
import :views_internal;
import :theme;
import :anim;
import :icons;
import :scope_guard;

namespace motrix::ui {

void RenderTitlebarOverlay(UiFrameContext& ctx) {
    const float s = ctx.dpiScale;
    const float btnW = LayoutMetrics::kWinActionBtnWidth * s;
    const float btnH = LayoutMetrics::kWinActionBtnHeight * s;
    const float totalW = btnW * 3.0f;

    const ImGuiViewport* vp = ImGui::GetMainViewport();
    const float startX = vp->Pos.x + vp->Size.x - totalW;
    const float startY = vp->Pos.y;
    ctx.host.SetTitlebarHeight(LayoutMetrics::kTitlebarHeight);

    const ImVec2 stripMin(startX, startY);
    const ImVec2 stripMax(startX + totalW, startY + btnH);
    const bool groupHovered = ImGui::IsMouseHoveringRect(stripMin, stripMax, false);
    const float groupAlpha =
        ctx.anim.Float(HashId("titlebar.group_alpha"), groupHovered ? 1.0f : 0.40f, 16.0f, 0.40f);

    ImDrawList* dl = ImGui::GetForegroundDrawList();
    const auto drawBtn = [&](int idx, const char* id, MotrixIcon icon, bool isClose) -> bool {
        const ImVec2 bMin(startX + static_cast<float>(idx) * btnW, startY);
        const ImVec2 bMax(bMin.x + btnW, bMin.y + btnH);
        ImGui::SetCursorScreenPos(bMin);
        const bool clicked = ImGui::InvisibleButton(id, ImVec2(btnW, btnH));
        ctx.host.RegisterTitlebarInteractiveRect(motrix::platform::TitlebarRect{
            .x0 = bMin.x - vp->Pos.x,
            .y0 = bMin.y - vp->Pos.y,
            .x1 = bMax.x - vp->Pos.x,
            .y1 = bMax.y - vp->Pos.y,
        });

        const bool hov = ImGui::IsItemHovered();
        const float hovT = ctx.anim.Float(HashId(id), hov ? 1.0f : 0.0f, 22.0f, 0.0f);
        if (hovT > 0.01f) {
            const RgbaColor bg = isClose ? WithAlpha(ctx.palette.titlebarCloseHoverBg, hovT)
                                         : WithAlpha(ctx.palette.titlebarActionHoverBg, hovT);
            dl->AddRectFilled(bMin, bMax, ToAbgrU32(bg));
        }

        const RgbaColor fg = (isClose && hovT > 0.35f)
                                 ? ctx.palette.titlebarCloseHoverFg
                                 : WithAlpha(ctx.palette.titlebarActionFg, groupAlpha);
        const float iconSz = 12.0f * s;
        const float ix = bMin.x + (btnW - iconSz) * 0.5f;
        const float iy = bMin.y + (btnH - iconSz) * 0.5f;
        DrawMotrixIcon(IconSpec{icon, ix, iy, iconSz, iconSz, fg, s, true});
        return clicked;
    };

    if (drawBtn(0, "##win_min", MotrixIcon::WinMinimize, false)) {
        ctx.host.Minimize();
    }
    const MotrixIcon maxIcon = ctx.host.IsMaximized() ? MotrixIcon::WinRestore : MotrixIcon::WinMaximize;
    if (drawBtn(1, "##win_max", maxIcon, false)) {
        ctx.host.ToggleMaximize();
    }
    if (drawBtn(2, "##win_close", MotrixIcon::WinClose, true)) {
        ctx.host.Close();
    }

    ImGui::SetCursorScreenPos(vp->Pos);
    ImGui::Dummy(ImVec2(0.0f, 0.0f));
}

}  // namespace motrix::ui

export module motrix.ui:views_internal;

import std;
import motrix.core;
import motrix.domain;
import motrix.app;
import motrix.platform;
import :theme;
import :anim;
import :fonts;

namespace motrix::ui {

export struct AddTaskFormState {
    std::array<char, 4096> uriBuffer{};
    std::array<char, 256> nameBuffer{};
    std::array<char, 512> dirBuffer{};
    int splitCount = 64;
    bool initialized = false;
};

export struct UiFrameContext {
    const motrix::app::AppStateSnapshot& state;
    motrix::app::ICommandSink& sink;
    motrix::platform::IWindowHost& host;
    const motrix::core::Translator& tr;
    const FontRegistry& fonts;
    const MotrixPalette& palette;
    AnimState& anim;
    float dpiScale = 1.0f;
    float viewportW = 1024.0f;
    float viewportH = 768.0f;
};

export void RenderTitlebarOverlay(UiFrameContext& ctx);
export void RenderAsideRail(UiFrameContext& ctx, float x, float y, float h);
export void RenderSubnav(UiFrameContext& ctx, float x, float y, float h, int& prefTab);
export void RenderTaskPanel(UiFrameContext& ctx, float x, float y, float w, float h);
export void
RenderTaskCard(UiFrameContext& ctx, const motrix::domain::DownloadTaskView& task, float width);
export void RenderSpeedometer(UiFrameContext& ctx, float rightEdgeX, float bottomEdgeY);
export void RenderPreferencesPanel(UiFrameContext& ctx, float x, float y, float w, float h, int prefTab);
export void RenderModals(UiFrameContext& ctx, AddTaskFormState& addForm);

}  // namespace motrix::ui

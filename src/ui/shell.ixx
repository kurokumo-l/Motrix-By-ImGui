export module motrix.ui:shell;

import std;
import motrix.app;
import motrix.platform;

namespace motrix::ui {

export class MotrixUiShell {
public:
    explicit MotrixUiShell(bool initialDark = false);
    ~MotrixUiShell();

    MotrixUiShell(const MotrixUiShell&) = delete;
    MotrixUiShell& operator=(const MotrixUiShell&) = delete;

    void InitializeImGui(motrix::platform::IWindowHost& host, bool initialDark);
    void PrefillAddTaskUri(std::string_view uri);
    void RenderFrame(const motrix::app::AppStateSnapshot& state,
                     motrix::app::ICommandSink& sink,
                     motrix::platform::IWindowHost& host);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace motrix::ui

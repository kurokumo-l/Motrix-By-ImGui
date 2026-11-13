// clang-format off
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <shellapi.h>
// clang-format on

import std;
import motrix.core;
import motrix.domain;
import motrix.ipc;
import motrix.app;
import motrix.platform;
import motrix.ui;

namespace {

struct CliOptions {
    motrix::platform::WindowHostConfig host;
    motrix::app::CoordinatorOptions app;
    std::string prefillUri;
    int prefTab = 0;
    bool forceEngine = false;
    bool disableEngine = false;
    bool disableSingleInstance = false;
    bool openedAtLogin = false;
    std::vector<std::string> positionalArgs;
};

[[nodiscard]] std::vector<std::string> ReadProcessArgsUtf8() {
    int wargc = 0;
    LPWSTR* rawArgv = ::CommandLineToArgvW(::GetCommandLineW(), &wargc);
    struct LocalFreeDeleter {
        void operator()(LPWSTR* p) const noexcept {
            if (p) {
                ::LocalFree(p);
            }
        }
    };
    const std::unique_ptr<LPWSTR[], LocalFreeDeleter> guard(rawArgv);
    std::vector<std::string> args;
    if (!guard || wargc <= 0) {
        return args;
    }
    args.reserve(static_cast<std::size_t>(wargc));
    for (int i = 0; i < wargc; ++i) {
        args.push_back(motrix::core::WideToUtf8(guard[i]));
    }
    return args;
}

[[nodiscard]] CliOptions ParseArgs(const std::vector<std::string>& argv) {
    CliOptions opts;
    opts.host.title = "Motrix";
    opts.host.width = 1024;
    opts.host.height = 768;
    opts.host.minWidth = 478;
    opts.host.minHeight = 420;
    opts.host.screenshotFrames = 12;

    const std::size_t argc = argv.size();
    for (std::size_t i = 1; i < argc; ++i) {
        const std::string& arg = argv[i];
        if (arg == "--theme" && i + 1 < argc) {
            const std::string val = motrix::core::AsciiLower(argv[++i]);
            const bool dark = (val == "dark");
            opts.host.initialDark = dark;
            opts.app.initialTheme =
                dark ? motrix::domain::ThemePreference::Dark : motrix::domain::ThemePreference::Light;
            opts.app.themeOverride = true;
        } else if (arg == "--locale" && i + 1 < argc) {
            const std::string val = motrix::core::AsciiLower(argv[++i]);
            opts.app.initialLocale = (val == "zh" || val == "zh-cn") ? motrix::core::Locale::ZhCN
                                                                     : motrix::core::Locale::EnUS;
            opts.app.localeOverride = true;
        } else if (arg == "--tab" && i + 1 < argc) {
            const std::string val = motrix::core::AsciiLower(argv[++i]);
            if (val == "waiting") {
                opts.app.initialCategory = motrix::domain::TaskCategory::Waiting;
            } else if (val == "stopped") {
                opts.app.initialCategory = motrix::domain::TaskCategory::Stopped;
            } else {
                opts.app.initialCategory = motrix::domain::TaskCategory::Downloading;
            }
        } else if (arg == "--preview-tasks") {
            opts.app.populatePreviewTasks = true;
        } else if (arg == "--add") {
            opts.app.openAddModal = true;
        } else if (arg == "--uri" && i + 1 < argc) {
            opts.prefillUri = argv[++i];
        } else if (arg == "--about") {
            opts.app.openAboutModal = true;
        } else if (arg == "--prefs") {
            opts.app.openPrefsSection = true;
        } else if (arg == "--pref-tab" && i + 1 < argc) {
            const std::string val = motrix::core::AsciiLower(argv[++i]);
            if (val == "advanced" || val == "1") {
                opts.prefTab = 1;
            } else if (val == "lab" || val == "2") {
                opts.prefTab = 2;
            } else {
                opts.prefTab = 0;
            }
            opts.app.openPrefsSection = true;
        } else if (arg == "--detail") {
            opts.app.openDetailDemo = true;
            opts.app.populatePreviewTasks = true;
        } else if (arg == "--detail-tab" && i + 1 < argc) {
            (void)argv[++i];
        } else if (arg == "--engine" && i + 1 < argc) {
            opts.app.enginePathOverride = argv[++i];
            opts.forceEngine = true;
        } else if (arg == "--rpc-port" && i + 1 < argc) {
            const int port = std::atoi(argv[++i].c_str());
            if (port > 0 && port <= 65535) {
                opts.app.rpcPortOverride = static_cast<std::uint16_t>(port);
            }
        } else if (arg == "--with-engine") {
            opts.forceEngine = true;
        } else if (arg == "--no-engine") {
            opts.disableEngine = true;
        } else if (arg == "--no-single-instance") {
            opts.disableSingleInstance = true;
        } else if (arg == "--opened-at-login=1" || arg == "--opened-at-login") {
            opts.openedAtLogin = true;
        } else if (arg == "--screenshot" && i + 1 < argc) {
            opts.host.screenshotPath = argv[++i];
        } else if (arg == "--frames" && i + 1 < argc) {
            opts.host.screenshotFrames = std::max(1, std::atoi(argv[++i].c_str()));
        } else if (arg == "--width" && i + 1 < argc) {
            opts.host.width = std::max(478, std::atoi(argv[++i].c_str()));
        } else if (arg == "--height" && i + 1 < argc) {
            opts.host.height = std::max(420, std::atoi(argv[++i].c_str()));
        } else if (!arg.empty() && arg[0] != '-') {
            opts.positionalArgs.push_back(arg);
        }
    }

    if (opts.disableEngine || opts.app.populatePreviewTasks) {
        opts.app.enableEngine = false;
    } else if (!opts.host.screenshotPath.empty() && !opts.forceEngine) {
        opts.app.enableEngine = false;
    }
    return opts;
}

[[nodiscard]] motrix::app::HandleStartupPayloadCmd
ToStartupCommand(const motrix::ipc::StartupPayload& payload) {
    motrix::app::HandleStartupPayloadCmd cmd;
    cmd.uris = payload.uris;
    cmd.deepLinks = payload.deepLinks;
    cmd.activateWindow = payload.activateWindow;
    cmd.torrentFiles.reserve(payload.torrentFiles.size());
    for (const auto& p : payload.torrentFiles) {
        cmd.torrentFiles.push_back(motrix::core::PathToUtf8(p));
    }
    return cmd;
}

}  // namespace

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    const auto rawArgs = ReadProcessArgsUtf8();
    CliOptions cli = ParseArgs(rawArgs);

    const std::filesystem::path appDataDir = motrix::platform::ResolveAppDataDir();
    const std::filesystem::path appDataLogs = appDataDir / "logs";
    motrix::platform::InstallCrashMinidumpHandler(appDataLogs);

    const bool isScreenshotMode = !cli.host.screenshotPath.empty();
    cli.app.appDataDir = appDataDir;
    cli.app.defaultDownloadDir =
        motrix::core::PathToUtf8(motrix::platform::ResolveDefaultDownloadsDir());
    cli.app.openedAtLogin = cli.openedAtLogin;
    if (isScreenshotMode || cli.app.populatePreviewTasks) {
        cli.app.loadPersistedConfig = false;
    }
    cli.app.enableOsIntegration = cli.app.loadPersistedConfig && !isScreenshotMode;

    motrix::core::Logger logger(motrix::core::LoggerOptions{
        .logFilePath = appDataLogs / "motrix_native.log",
        .minLevel = motrix::core::LogLevel::Info,
    });
    logger.Info("main", "Starting Motrix Native (Milestone 4)");

    const motrix::ipc::StartupPayload initialPayload =
        motrix::ipc::BuildStartupPayloadFromArgs(cli.positionalArgs);

    std::atomic<motrix::app::AppCoordinator*> coordinatorPtr{nullptr};
    std::unique_ptr<motrix::ipc::ISingleInstanceChannel> ipcChannel;
    const bool useSingleInstance =
        !isScreenshotMode && !cli.disableSingleInstance && cli.app.rpcPortOverride == 0;
    if (useSingleInstance) {
        ipcChannel = motrix::ipc::CreateNamedPipeChannel(logger);
        auto acquired = ipcChannel->TryAcquirePrimary([&](const motrix::ipc::StartupPayload& remote) {
            if (auto* coord = coordinatorPtr.load(std::memory_order_acquire)) {
                coord->Dispatch(ToStartupCommand(remote));
            }
        });
        if (acquired && !*acquired) {
            logger.Info("ipc", "Secondary instance detected; forwarding startup payload to primary");
            (void)ipcChannel->ForwardToPrimary(initialPayload);
            return 0;
        }
    }

    motrix::platform::Win32WindowHost host;
    motrix::app::AppCoordinator coordinator(cli.app, logger, &host);
    coordinatorPtr.store(&coordinator, std::memory_order_release);

    const auto initSnap = coordinator.Snapshot();
    const auto& initPrefs = initSnap->prefs;
    const bool startDark = initPrefs.theme == motrix::domain::ThemePreference::Dark;
    const bool showTray = !isScreenshotMode && initPrefs.runMode != motrix::domain::RunMode::HideTray;
    const bool hasExplicitUiLaunch = !cli.prefillUri.empty() || !initialPayload.uris.empty()
                                     || !initialPayload.torrentFiles.empty()
                                     || !initialPayload.deepLinks.empty() || cli.app.openAddModal
                                     || cli.app.openAboutModal || cli.app.openPrefsSection;

    cli.host.initialDark = startDark;
    cli.host.initialZhCn = initPrefs.locale == motrix::core::Locale::ZhCN;
    cli.host.enableTrayIcon = showTray;
    cli.host.bindCloseToHide = showTray && !isScreenshotMode;
    cli.host.autoHideOnBlur = initPrefs.autoHideWindow && showTray && !isScreenshotMode;
    cli.host.startHidden = !isScreenshotMode && !hasExplicitUiLaunch && showTray
                           && (initPrefs.autoHideWindow || cli.openedAtLogin);
    if (initPrefs.keepWindowState && !isScreenshotMode) {
        cli.host.initialBounds = motrix::platform::WindowBounds{
            .x = initPrefs.windowX.value_or(0),
            .y = initPrefs.windowY.value_or(0),
            .width = initPrefs.windowWidth,
            .height = initPrefs.windowHeight,
            .hasPosition = initPrefs.windowX.has_value() && initPrefs.windowY.has_value(),
            .maximized = initPrefs.windowMaximized,
        };
    }

    cli.host.onFilesDropped = [&](std::vector<std::string> paths) {
        coordinator.Dispatch(motrix::app::QueueDroppedFilesCmd{std::move(paths)});
    };
    cli.host.onTrayAction = [&](motrix::platform::TrayMenuAction action) {
        coordinator.Dispatch(motrix::app::HandleTrayActionCmd{action});
    };
    cli.host.onBoundsChanged = [&](const motrix::platform::WindowBounds& bounds) {
        if (!isScreenshotMode) {
            coordinator.Dispatch(motrix::app::UpdateWindowBoundsCmd{bounds});
        }
    };
    cli.host.onQuitRequested = [&]() { logger.Info("main", "Quit requested cleanly"); };

    if (!initialPayload.uris.empty() || !initialPayload.torrentFiles.empty()
        || !initialPayload.deepLinks.empty()) {
        coordinator.Dispatch(ToStartupCommand(initialPayload));
    }

    motrix::ui::MotrixUiShell shell(startDark);
    if (!cli.prefillUri.empty()) {
        shell.PrefillAddTaskUri(cli.prefillUri);
    }
    if (cli.prefTab != 0) {
        shell.SetPreferencesTab(cli.prefTab);
    }

    const auto result = host.Run(
        cli.host,
        [&](motrix::platform::IWindowHost& h) { shell.InitializeImGui(h, startDark); },
        [&](motrix::platform::IWindowHost& h) {
            const auto snap = coordinator.Snapshot();
            shell.RenderFrame(*snap, coordinator, h);
        });

    coordinatorPtr.store(nullptr, std::memory_order_release);
    if (ipcChannel) {
        ipcChannel->Close();
    }

    if (!result) {
        logger.Error("main", "Host exited with error: {}", result.error().message);
        return 1;
    }

    const auto metrics = host.Metrics();
    logger.Info(
        "main", "Shutdown clean (frames={}, sleepWaits={})", metrics.framesRendered, metrics.sleepWaits);
    return *result;
}

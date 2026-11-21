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
    std::vector<std::string> parseWarnings;
};

[[nodiscard]] std::vector<std::string> ReadProcessArgsUtf8() {
    int wargc = 0;
    LPWSTR* rawArgv = ::CommandLineToArgvW(::GetCommandLineW(), &wargc);
    const std::unique_ptr<LPWSTR[], motrix::core::LocalFreeDeleter> guard(rawArgv);
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

[[nodiscard]] int ParseDetailTabValue(std::string_view rawVal) noexcept {
    using motrix::core::EqualsAsciiInsensitive;
    if (EqualsAsciiInsensitive(rawVal, "activity") || rawVal == "1") {
        return 1;
    }
    if (EqualsAsciiInsensitive(rawVal, "trackers") || rawVal == "2") {
        return 2;
    }
    if (EqualsAsciiInsensitive(rawVal, "peers") || rawVal == "3") {
        return 3;
    }
    if (EqualsAsciiInsensitive(rawVal, "files") || rawVal == "4") {
        return 4;
    }
    return 0;
}

[[nodiscard]] std::optional<int> ParseBoundedIntArg(
    CliOptions& opts, std::string_view flag, std::string_view rawVal, int minVal, int maxVal) {
    const auto parsed = motrix::core::ParseStrictInt(rawVal);
    if (!parsed) {
        opts.parseWarnings.push_back(
            std::format("Invalid integer '{}' for {}; using default", rawVal, flag));
        return std::nullopt;
    }
    if (*parsed < minVal || *parsed > maxVal) {
        opts.parseWarnings.push_back(
            std::format("Out-of-range integer {} for {} (expected [{}, {}]); using default",
                        *parsed,
                        flag,
                        minVal,
                        maxVal));
        return std::nullopt;
    }
    return *parsed;
}

void ApplyValueFlag(CliOptions& opts, std::string_view arg, const std::string& rawVal) {
    if (arg == "--theme") {
        const bool dark = (motrix::core::AsciiLower(rawVal) == "dark");
        opts.host.initialDark = dark;
        opts.app.initialTheme =
            dark ? motrix::domain::ThemePreference::Dark : motrix::domain::ThemePreference::Light;
        opts.app.themeOverride = true;
    } else if (arg == "--locale") {
        const std::string val = motrix::core::AsciiLower(rawVal);
        opts.app.initialLocale =
            (val == "zh" || val == "zh-cn") ? motrix::core::Locale::ZhCN : motrix::core::Locale::EnUS;
        opts.app.localeOverride = true;
    } else if (arg == "--tab") {
        const std::string val = motrix::core::AsciiLower(rawVal);
        if (val == "waiting") {
            opts.app.initialCategory = motrix::domain::TaskCategory::Waiting;
        } else if (val == "stopped") {
            opts.app.initialCategory = motrix::domain::TaskCategory::Stopped;
        } else {
            opts.app.initialCategory = motrix::domain::TaskCategory::Downloading;
        }
    } else if (arg == "--uri") {
        opts.prefillUri = rawVal;
    } else if (arg == "--pref-tab") {
        const std::string val = motrix::core::AsciiLower(rawVal);
        if (val == "advanced" || val == "1") {
            opts.prefTab = 1;
        } else if (val == "lab" || val == "2") {
            opts.prefTab = 2;
        } else {
            opts.prefTab = 0;
        }
        opts.app.openPrefsSection = true;
    } else if (arg == "--detail-tab") {
        opts.app.initialDetailTab = ParseDetailTabValue(rawVal);
    } else if (arg == "--engine") {
        opts.app.enginePathOverride = rawVal;
        opts.forceEngine = true;
    } else if (arg == "--rpc-port") {
        if (const auto port = ParseBoundedIntArg(opts, arg, rawVal, 1, 65535)) {
            opts.app.rpcPortOverride = static_cast<std::uint16_t>(*port);
        }
    } else if (arg == "--screenshot") {
        opts.host.screenshotPath = rawVal;
    } else if (arg == "--frames") {
        if (const auto frames = ParseBoundedIntArg(opts, arg, rawVal, 1, 10000)) {
            opts.host.screenshotFrames = *frames;
        }
    } else if (arg == "--width") {
        if (const auto w = ParseBoundedIntArg(opts, arg, rawVal, 478, 16384)) {
            opts.host.width = *w;
        }
    } else if (arg == "--height") {
        if (const auto h = ParseBoundedIntArg(opts, arg, rawVal, 420, 16384)) {
            opts.host.height = *h;
        }
    } else if (arg == "--hover") {
        const auto comma = rawVal.find(',');
        if (comma != std::string::npos) {
            const auto xOpt = motrix::core::ParseStrictInt(rawVal.substr(0, comma));
            const auto yOpt = motrix::core::ParseStrictInt(rawVal.substr(comma + 1));
            if (xOpt && yOpt) {
                opts.host.screenshotHover =
                    std::pair<float, float>{static_cast<float>(*xOpt), static_cast<float>(*yOpt)};
            }
        }
    }
}

[[nodiscard]] bool NeedsValueArg(std::string_view arg) noexcept {
    return arg == "--theme" || arg == "--locale" || arg == "--tab" || arg == "--uri"
           || arg == "--pref-tab" || arg == "--detail-tab" || arg == "--engine" || arg == "--rpc-port"
           || arg == "--screenshot" || arg == "--frames" || arg == "--width" || arg == "--height"
           || arg == "--hover";
}

void ApplySimpleFlag(CliOptions& opts, std::string_view arg) {
    if (arg == "--preview-tasks") {
        opts.app.populatePreviewTasks = true;
    } else if (arg == "--add") {
        opts.app.openAddModal = true;
    } else if (arg == "--about") {
        opts.app.openAboutModal = true;
    } else if (arg == "--prefs") {
        opts.app.openPrefsSection = true;
    } else if (arg == "--detail") {
        opts.app.openDetailDemo = true;
        opts.app.populatePreviewTasks = true;
    } else if (arg == "--with-engine") {
        opts.forceEngine = true;
    } else if (arg == "--no-engine") {
        opts.disableEngine = true;
    } else if (arg == "--no-single-instance") {
        opts.disableSingleInstance = true;
    } else if (arg == "--opened-at-login=1" || arg == "--opened-at-login") {
        opts.openedAtLogin = true;
    }
}

[[nodiscard]] CliOptions ParseArgs(const std::vector<std::string>& argv) {
    CliOptions opts;
    opts.app.initialLocale = motrix::platform::QuerySystemPreferredLocale();
    opts.host.title = "Motrix";
    opts.host.width = 1024;
    opts.host.height = 768;
    opts.host.minWidth = 478;
    opts.host.minHeight = 420;
    opts.host.screenshotFrames = 12;

    const std::size_t argc = argv.size();
    for (std::size_t i = 1; i < argc; ++i) {
        const std::string& arg = argv[i];
        if (NeedsValueArg(arg) && i + 1 < argc) {
            ApplyValueFlag(opts, arg, argv[++i]);
        } else if (!arg.empty() && arg[0] != '-') {
            opts.positionalArgs.push_back(arg);
        } else {
            ApplySimpleFlag(opts, arg);
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

struct EarlyIpcBuffer {
    std::mutex mu;
    std::vector<motrix::ipc::StartupPayload> pending;
    std::atomic<motrix::app::AppCoordinator*> coordinator{nullptr};
};

[[nodiscard]] motrix::ipc::StartupPayload AugmentPayloadFromCli(const CliOptions& cli,
                                                                motrix::ipc::StartupPayload payload) {
    if (!cli.prefillUri.empty()) {
        payload.uris.push_back(cli.prefillUri);
    }
    if (cli.app.openAddModal && payload.uris.empty() && payload.torrentFiles.empty()) {
        payload.deepLinks.push_back("mo://new-task");
    } else if (cli.app.openPrefsSection) {
        payload.deepLinks.push_back("mo://preferences");
    } else if (cli.app.openAboutModal) {
        payload.deepLinks.push_back("mo://about");
    }
    return payload;
}

[[nodiscard]] std::unique_ptr<motrix::ipc::ISingleInstanceChannel>
TryAcquireSingleInstance(const motrix::ipc::StartupPayload& forwardPayload,
                         motrix::core::Logger& logger,
                         EarlyIpcBuffer& earlyBuf) {
    auto ipcChannel = motrix::ipc::CreateNamedPipeChannel(logger);
    const auto acquired = ipcChannel->TryAcquirePrimary([&](const motrix::ipc::StartupPayload& remote) {
        std::scoped_lock lk(earlyBuf.mu);
        if (auto* coord = earlyBuf.coordinator.load(std::memory_order_acquire)) {
            coord->Dispatch(ToStartupCommand(remote));
        } else {
            earlyBuf.pending.push_back(remote);
        }
    });
    if (acquired && !*acquired) {
        logger.Info("ipc", "Secondary instance detected; forwarding startup payload to primary");
        (void)ipcChannel->ForwardToPrimary(forwardPayload);
        return nullptr;
    }
    return ipcChannel;
}

void ConfigureInitialHostPresentation(CliOptions& cli,
                                      const motrix::app::AppStateSnapshot& initSnap,
                                      const motrix::ipc::StartupPayload& initialPayload) {
    const bool isScreenshotMode = !cli.host.screenshotPath.empty();
    const auto& initPrefs = initSnap.prefs;
    const bool startDark = initPrefs.theme == motrix::domain::ThemePreference::Dark
                           || (initPrefs.theme == motrix::domain::ThemePreference::Auto
                               && motrix::platform::QuerySystemDarkMode());
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
                           && (initPrefs.autoHideWindow || cli.openedAtLogin
                               || initPrefs.runMode == motrix::domain::RunMode::Tray);
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
}

void AttachHostCallbacks(motrix::platform::WindowHostConfig& hostCfg,
                         motrix::app::AppCoordinator& coordinator,
                         motrix::core::Logger& logger,
                         bool isScreenshotMode) {
    hostCfg.onFilesDropped = [&](std::vector<std::string> paths) {
        coordinator.Dispatch(motrix::app::QueueDroppedFilesCmd{std::move(paths)});
    };
    hostCfg.onTrayAction = [&](motrix::platform::TrayMenuAction action) {
        coordinator.Dispatch(motrix::app::HandleTrayActionCmd{action});
    };
    hostCfg.onBoundsChanged = [&, isScreenshotMode](const motrix::platform::WindowBounds& bounds) {
        if (!isScreenshotMode) {
            coordinator.Dispatch(motrix::app::UpdateWindowBoundsCmd{bounds});
        }
    };
    hostCfg.onSessionEnding = [&](bool ending) {
        logger.Info("main", "OS session ending state={}", ending ? "suspending" : "resumed");
        coordinator.SetSessionEnding(ending);
    };
    hostCfg.onQuitRequested = [&]() {
        logger.Info("main", "Quit requested cleanly");
        coordinator.BeginShutdown();
    };
}

void PrepareWindowAndShell(CliOptions& cli,
                           const motrix::app::AppStateSnapshot& initSnap,
                           const motrix::ipc::StartupPayload& initialPayload,
                           motrix::app::AppCoordinator& coordinator) {
    ConfigureInitialHostPresentation(cli, initSnap, initialPayload);
    if (!initialPayload.uris.empty() || !initialPayload.torrentFiles.empty()
        || !initialPayload.deepLinks.empty()) {
        coordinator.Dispatch(ToStartupCommand(initialPayload));
    }
}

void DispatchQueuedIpcPayloads(EarlyIpcBuffer& earlyBuf, motrix::app::AppCoordinator& coordinator) {
    std::vector<motrix::ipc::StartupPayload> buffered;
    {
        std::scoped_lock lk(earlyBuf.mu);
        earlyBuf.coordinator.store(&coordinator, std::memory_order_release);
        buffered.swap(earlyBuf.pending);
    }
    for (const auto& remote : buffered) {
        coordinator.Dispatch(ToStartupCommand(remote));
    }
}

auto RunUiHostLoop(CliOptions& cli,
                   motrix::platform::Win32WindowHost& host,
                   motrix::app::AppCoordinator& coordinator,
                   const motrix::domain::UserPreferences& prefs) {
    const bool startDark = cli.host.initialDark;
    motrix::ui::MotrixUiShell shell(startDark);
    if (!cli.prefillUri.empty()) {
        shell.PrefillAddTaskUri(cli.prefillUri);
    }
    if (cli.prefTab != 0) {
        shell.SetPreferencesTab(cli.prefTab);
    }

    cli.host.onBeforeNewFrame = [&]() { shell.PrepareFrame(); };
    return host.Run(
        cli.host,
        [&](motrix::platform::IWindowHost& h) {
            shell.InitializeImGui(h, startDark, prefs.fontFamily, prefs.monoFontFamily);
        },
        [&](motrix::platform::IWindowHost& h) {
            const auto snap = coordinator.Snapshot();
            shell.RenderFrame(*snap, coordinator, h);
        });
}

}  // namespace

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    ::SetProcessShutdownParameters(0x300, SHUTDOWN_NORETRY);
    CliOptions cli = ParseArgs(ReadProcessArgsUtf8());

    const std::filesystem::path appDataDir = motrix::platform::ResolveAppDataDir();
    const std::filesystem::path appDataLogs = appDataDir / "logs";
    motrix::platform::InstallCrashMinidumpHandler(appDataLogs);

    const bool isScreenshotMode = !cli.host.screenshotPath.empty();
    cli.app.appDataDir = appDataDir;
    cli.app.defaultDownloadDir =
        motrix::core::PathToUtf8(motrix::platform::ResolveDefaultDownloadsDir());
    cli.app.openedAtLogin = cli.openedAtLogin;
    cli.app.initialUri = cli.prefillUri;
    if (isScreenshotMode || cli.app.populatePreviewTasks) {
        cli.app.loadPersistedConfig = false;
    }
    cli.app.enableOsIntegration = cli.app.loadPersistedConfig && !isScreenshotMode;

    motrix::core::Logger logger(motrix::core::LoggerOptions{
        .logFilePath = appDataLogs / "motrix_native.log",
        .minLevel = motrix::core::LogLevel::Info,
    });
    logger.Info("main", "Starting Motrix Native (Milestone 4)");
    for (const auto& warning : cli.parseWarnings) {
        logger.Warn("cli", "{}", warning);
    }

    const motrix::ipc::StartupPayload initialPayload =
        motrix::ipc::BuildStartupPayloadFromArgs(cli.positionalArgs);
    const motrix::ipc::StartupPayload forwardPayload = AugmentPayloadFromCli(cli, initialPayload);

    EarlyIpcBuffer earlyBuf;
    const bool useSingleInstance =
        !isScreenshotMode && !cli.disableSingleInstance && cli.app.rpcPortOverride == 0;
    auto ipcChannel =
        useSingleInstance ? TryAcquireSingleInstance(forwardPayload, logger, earlyBuf) : nullptr;
    if (useSingleInstance && !ipcChannel) {
        return 0;
    }

    motrix::platform::Win32WindowHost host;
    motrix::app::AppCoordinator coordinator(cli.app, logger, &host);
    DispatchQueuedIpcPayloads(earlyBuf, coordinator);

    const auto initSnap = coordinator.Snapshot();
    PrepareWindowAndShell(cli, *initSnap, initialPayload, coordinator);
    AttachHostCallbacks(cli.host, coordinator, logger, isScreenshotMode);

    const auto result = RunUiHostLoop(cli, host, coordinator, initSnap->prefs);

    earlyBuf.coordinator.store(nullptr, std::memory_order_release);
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

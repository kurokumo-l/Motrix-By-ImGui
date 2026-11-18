// clang-format off
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <aclapi.h>
#include <sddl.h>
// clang-format on

#include <catch2/catch_test_macros.hpp>

import std;
import motrix.core;
import motrix.engine;
import motrix.ipc;
import motrix.platform;

TEST_CASE("motrix.ipc DefaultIpcPipeName is scoped to current user SID and session", "[ipc][security]") {
    const std::string pipeName = motrix::ipc::DefaultIpcPipeName();
    CHECK(pipeName.starts_with(std::string(motrix::ipc::kDefaultPipeName) + "_S-"));
    CHECK(pipeName.find("_s") != std::string::npos);

    DWORD sessionId = 0;
    REQUIRE(::ProcessIdToSessionId(::GetCurrentProcessId(), &sessionId) != FALSE);
    CHECK(pipeName.ends_with("_s" + std::to_string(sessionId)));
}

TEST_CASE("motrix.ipc NamedPipe enforces explicit current-user DACL", "[ipc][security]") {
    motrix::core::Logger logger;
    const std::string pipeName = std::string(R"(\\.\pipe\MotrixDaclTest_)")
                                 + std::to_string(motrix::engine::CurrentHostProcessId());
    auto channel = motrix::ipc::CreateNamedPipeChannel(logger, pipeName);
    REQUIRE(channel != nullptr);
    const auto acq = channel->TryAcquirePrimary([](const motrix::ipc::StartupPayload&) {});
    REQUIRE(acq.has_value());
    CHECK(*acq == true);

    const std::wstring widePipe = motrix::core::Utf8ToWide(pipeName);
    PACL dacl = nullptr;
    PSECURITY_DESCRIPTOR rawSd = nullptr;
    const DWORD secRes = ::GetNamedSecurityInfoW(widePipe.c_str(),
                                                 SE_FILE_OBJECT,
                                                 DACL_SECURITY_INFORMATION,
                                                 nullptr,
                                                 nullptr,
                                                 &dacl,
                                                 nullptr,
                                                 &rawSd);
    REQUIRE(secRes == ERROR_SUCCESS);
    struct LocalSdGuard {
        PSECURITY_DESCRIPTOR sd = nullptr;
        ~LocalSdGuard() {
            if (sd) {
                ::LocalFree(sd);
            }
        }
    } sdGuard{rawSd};

    REQUIRE(dacl != nullptr);
    ACL_SIZE_INFORMATION sizeInfo{};
    REQUIRE(::GetAclInformation(dacl, &sizeInfo, sizeof(sizeInfo), AclSizeInformation) != FALSE);
    CHECK(sizeInfo.AceCount == 1u);

    PVOID aceRaw = nullptr;
    REQUIRE(::GetAce(dacl, 0, &aceRaw) != FALSE);
    const auto* ace = static_cast<const ACCESS_ALLOWED_ACE*>(aceRaw);
    LPSTR aceSid = nullptr;
    REQUIRE(::ConvertSidToStringSidA(reinterpret_cast<PSID>(const_cast<DWORD*>(&ace->SidStart)), &aceSid)
            != FALSE);
    const std::string aceSidStr(aceSid);
    ::LocalFree(aceSid);
    CHECK(motrix::ipc::DefaultIpcPipeName().find(aceSidStr) != std::string::npos);

    channel->Close();
}

TEST_CASE("motrix.platform crash dump filename includes PID, timestamp, and retry suffix",
          "[platform][crash]") {
    const std::string first =
        motrix::platform::FormatCrashDumpFilename(4242u, 20261115u, 120509087u, 0u);
    const std::string retry =
        motrix::platform::FormatCrashDumpFilename(4242u, 20261115u, 120509087u, 1u);
    CHECK(first == "motrix_crash_4242_20261115_120509087.dmp");
    CHECK(retry == "motrix_crash_4242_20261115_120509087_1.dmp");
    CHECK(first != retry);
}

TEST_CASE("motrix.platform Win32WindowHost supports deferred startHidden quit and multiple instances",
          "[platform][host]") {
    motrix::platform::Win32WindowHost hostA;
    motrix::platform::Win32WindowHost hostB;
    CHECK_FALSE(hostA.Metrics().hiddenToTray);
    CHECK_FALSE(hostB.Metrics().hiddenToTray);

    bool imguiInitCalled = false;
    bool frameRendered = false;
    std::jthread stopper([&](std::stop_token) {
        std::this_thread::sleep_for(std::chrono::milliseconds(80));
        hostA.RequestQuit();
    });

    motrix::platform::WindowHostConfig cfg{};
    cfg.title = "Motrix Hidden Test";
    cfg.enableTrayIcon = false;
    cfg.startHidden = true;

    const auto res = hostA.Run(
        cfg,
        [&](motrix::platform::IWindowHost&) { imguiInitCalled = true; },
        [&](motrix::platform::IWindowHost&) { frameRendered = true; });
    REQUIRE(res.has_value());
    CHECK(*res == 0);
    CHECK_FALSE(imguiInitCalled);
    CHECK_FALSE(frameRendered);
    CHECK(hostA.Metrics().framesRendered == 0u);
}

TEST_CASE("motrix.platform Win32WindowHost initializes UI when woken by tray left-click",
          "[platform][host][tray]") {
    motrix::platform::Win32WindowHost host;
    std::atomic<bool> imguiInitCalled{false};
    std::atomic<int> frameCount{0};

    std::jthread driver([&](std::stop_token) {
        HWND hwnd = nullptr;
        for (int i = 0; i < 100 && !hwnd; ++i) {
            hwnd = ::FindWindowW(L"MotrixNativeWindowClass", L"Motrix Tray Wake Test");
            std::this_thread::sleep_for(std::chrono::milliseconds(15));
        }
        if (hwnd) {
            ::PostMessageW(hwnd, WM_APP + 102u, 0, WM_LBUTTONUP);
        }
        for (int i = 0; i < 100 && frameCount.load(std::memory_order_acquire) < 1; ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
        host.RequestQuit();
    });

    motrix::platform::WindowHostConfig cfg{};
    cfg.title = "Motrix Tray Wake Test";
    cfg.enableTrayIcon = false;
    cfg.startHidden = true;

    const auto res = host.Run(
        cfg,
        [&](motrix::platform::IWindowHost&) { imguiInitCalled.store(true, std::memory_order_release); },
        [&](motrix::platform::IWindowHost&) { frameCount.fetch_add(1, std::memory_order_acq_rel); });
    REQUIRE(res.has_value());
    CHECK(*res == 0);
    CHECK(imguiInitCalled.load());
    CHECK(frameCount.load() >= 1);
}

TEST_CASE("motrix.platform Win32WindowHost renders after ShowFromTray -> HideToTray -> ShowFromTray",
          "[platform][host][tray]") {
    motrix::platform::Win32WindowHost host;
    std::atomic<int> initCount{0};
    std::atomic<int> frameCount{0};

    std::jthread driver([&](std::stop_token) {
        std::this_thread::sleep_for(std::chrono::milliseconds(40));
        host.ShowFromTray();
        for (int i = 0; i < 100 && frameCount.load(std::memory_order_acquire) < 1; ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
        const int beforeHide = frameCount.load(std::memory_order_acquire);
        host.HideToTray();
        std::this_thread::sleep_for(std::chrono::milliseconds(80));
        host.ShowFromTray();
        for (int i = 0; i < 100 && frameCount.load(std::memory_order_acquire) <= beforeHide; ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
        host.RequestQuit();
    });

    motrix::platform::WindowHostConfig cfg{};
    cfg.title = "Motrix Tray Reshow Test";
    cfg.enableTrayIcon = false;
    cfg.startHidden = true;

    const auto res = host.Run(
        cfg,
        [&](motrix::platform::IWindowHost&) { initCount.fetch_add(1, std::memory_order_acq_rel); },
        [&](motrix::platform::IWindowHost&) { frameCount.fetch_add(1, std::memory_order_acq_rel); });
    REQUIRE(res.has_value());
    CHECK(*res == 0);
    CHECK(initCount.load() == 1);
    CHECK(frameCount.load() >= 2);
}

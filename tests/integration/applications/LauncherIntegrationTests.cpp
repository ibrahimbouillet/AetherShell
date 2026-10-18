// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
//
// Failure paths of the launcher only. These tests never start an application and never activate a
// window: every request below is rejected before ShellExecuteEx or the activation manager is reached.
#include "AetherTest.h"

#include "applications/ShellApplicationLauncher.h"
#include "core/Dispatcher.h"

#include <windows.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <optional>
#include <thread>

using namespace aether;
using namespace aether::apps;

namespace {

std::optional<Result<void>> LaunchAndWait(ManualDispatcher& dispatcher, ShellApplicationLauncher& launcher,
                                          const ApplicationInfo& app) {
    std::optional<Result<void>> outcome;
    launcher.LaunchAsync(app, [&](Result<void> result) {
        CHECK(dispatcher.IsUiThread());
        outcome.emplace(std::move(result));
    });
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (!outcome && std::chrono::steady_clock::now() < deadline) {
        dispatcher.RunPending();
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    return outcome;
}

}  // namespace

AETHER_TEST("launcher: an entry without any launchable route reports NotFound") {
    ManualDispatcher dispatcher;
    ShellApplicationLauncher launcher(dispatcher);
    ApplicationInfo nothing;
    nothing.displayName = L"Ghost App";
    const auto outcome = LaunchAndWait(dispatcher, launcher, nothing);
    REQUIRE(outcome.has_value());
    REQUIRE_FALSE(outcome->ok());
    CHECK_EQ(outcome->error().code, ErrorCode::NotFound);
    CHECK_EQ(outcome->error().message, std::string("Ghost App could not be found. It may have been moved or uninstalled."));
}

AETHER_TEST("launcher: missing and invalid executable paths are rejected before launching") {
    ManualDispatcher dispatcher;
    ShellApplicationLauncher launcher(dispatcher);

    ApplicationInfo missing;
    missing.displayName = L"Missing Tool";
    missing.executablePath = L"C:\\AetherShell-does-not-exist\\missing-tool.exe";
    missing.shortcutPath = L"C:\\AetherShell-does-not-exist\\Missing Tool.lnk";
    const auto notFound = LaunchAndWait(dispatcher, launcher, missing);
    REQUIRE(notFound.has_value());
    REQUIRE_FALSE(notFound->ok());
    CHECK_EQ(notFound->error().code, ErrorCode::NotFound);

    ApplicationInfo relative;
    relative.displayName = L"Relative\r\nForged line";  // untrusted names never put line breaks in messages
    relative.executablePath = L"tool.exe";
    const auto invalid = LaunchAndWait(dispatcher, launcher, relative);
    REQUIRE(invalid.has_value());
    REQUIRE_FALSE(invalid->ok());
    CHECK_EQ(invalid->error().code, ErrorCode::InvalidArgument);
    CHECK(invalid->error().message.find_first_of("\r\n") == std::string::npos);
    TEST_NOTE("invalid path: " + invalid->error().message);

    ApplicationInfo device;
    device.displayName = L"Device";
    device.executablePath = L"\\\\?\\GLOBALROOT\\Device\\HarddiskVolume1\\Windows\\notepad.exe";
    const auto rejected = LaunchAndWait(dispatcher, launcher, device);
    REQUIRE(rejected.has_value());
    REQUIRE_FALSE(rejected->ok());
    CHECK_EQ(rejected->error().code, ErrorCode::InvalidArgument);
}

AETHER_TEST("launcher: an executable whose shortcut arguments are gone is refused, not started bare") {
    // The file exists but is not a real program, so even a regression that called ShellExecuteEx could
    // not start anything (it would fail with ERROR_BAD_EXE_FORMAT, i.e. Unsupported, not NotFound).
    const auto directory = test::MakeTempDirectory("apps-launcher-args");
    const auto fake = directory / L"fake-tool.exe";
    { std::ofstream(fake) << "not a program"; }

    ManualDispatcher dispatcher;
    ShellApplicationLauncher launcher(dispatcher);
    ApplicationInfo withArguments;
    withArguments.displayName = L"Developer Prompt";
    withArguments.executablePath = fake.native();
    withArguments.arguments = L"/k vcvars64.bat";  // came from a shortcut that no longer exists
    const auto outcome = LaunchAndWait(dispatcher, launcher, withArguments);
    REQUIRE(outcome.has_value());
    REQUIRE_FALSE(outcome->ok());
    CHECK_EQ(outcome->error().code, ErrorCode::NotFound);
    CHECK_EQ(outcome->error().message, std::string("The shortcut for Developer Prompt no longer exists."));

    std::error_code ignored;
    std::filesystem::remove_all(directory, ignored);
}

AETHER_TEST("launcher: completions are dropped once the launcher is destroyed") {
    ManualDispatcher dispatcher;
    bool called = false;
    {
        ShellApplicationLauncher launcher(dispatcher);
        launcher.LaunchAsync(ApplicationInfo{}, [&](Result<void>) { called = true; });
        std::this_thread::sleep_for(std::chrono::milliseconds(200));  // let the worker post its result
    }
    dispatcher.RunPending();
    CHECK_FALSE(called);
}

AETHER_TEST("launcher: Activate validates its input without touching other windows") {
    ManualDispatcher dispatcher;
    ShellApplicationLauncher launcher(dispatcher);

    RunningApplication none;
    none.displayName = L"Nothing";
    const auto noWindow = launcher.Activate(none);
    REQUIRE_FALSE(noWindow.ok());
    CHECK_EQ(noWindow.error().code, ErrorCode::NotFound);

    // A window handle that no longer exists.
    WNDCLASSEXW windowClass{sizeof(windowClass)};
    windowClass.lpfnWndProc = ::DefWindowProcW;
    windowClass.hInstance = ::GetModuleHandleW(nullptr);
    windowClass.lpszClassName = L"AetherShell.LauncherTestStale";
    ::RegisterClassExW(&windowClass);
    const HWND stale = ::CreateWindowExW(0, windowClass.lpszClassName, L"stale", WS_OVERLAPPED, 0, 0, 10, 10, nullptr,
                                         nullptr, windowClass.hInstance, nullptr);
    REQUIRE(stale != nullptr);
    ::DestroyWindow(stale);
    ::UnregisterClassW(windowClass.lpszClassName, windowClass.hInstance);
    RunningApplication gone;
    gone.windows.push_back(RunningWindow{stale, L"stale", false});
    const auto staleResult = launcher.Activate(gone);
    REQUIRE_FALSE(staleResult.ok());
    CHECK_EQ(staleResult.error().code, ErrorCode::NotFound);

    // Off the UI thread.
    std::optional<ErrorCode> offThread;
    std::thread other([&] {
        auto result = launcher.Activate(none);
        if (!result.ok()) offThread = result.error().code;
    });
    other.join();
    REQUIRE(offThread.has_value());
    CHECK_EQ(*offThread, ErrorCode::InvalidState);
}

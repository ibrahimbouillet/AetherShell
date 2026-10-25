// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
#include "AetherTest.h"

#include "core/FileSystem.h"
#include "diagnostics/Logger.h"
#include <stdexcept>
#include "platform/windows/AppPaths.h"
#include "platform/windows/DpiUtilities.h"
#include "platform/windows/MessageLoop.h"
#include "platform/windows/MonitorUtilities.h"
#include "platform/windows/OsVersion.h"
#include "platform/windows/SingleInstance.h"
#include "platform/windows/Win32Error.h"
#include "platform/windows/Win32UiDispatcher.h"

#include <atomic>
#include <thread>

using namespace aether;
using namespace aether::platform;

namespace {
void PumpPendingMessages() {
    MSG msg;
    while (::PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
        ::TranslateMessage(&msg);
        ::DispatchMessageW(&msg);
    }
}
}  // namespace

AETHER_TEST("platform: dpi conversions") {
    CHECK_EQ(DipsToPixels(48.0f, 96), 48);
    CHECK_EQ(DipsToPixels(48.0f, 144), 72);
    CHECK_EQ(DipsToPixels(10.0f, 120), 13);  // 12.5 rounds away from zero
    CHECK_NEAR(PixelsToDips(72, 144), 48.0, 1e-6);
    CHECK_NEAR(ScaleForDpi(0), 1.0, 1e-6);
}

AETHER_TEST("platform: os version is at least the supported minimum") {
    const auto version = GetOsVersion();
    CHECK_EQ(version.major, 10u);
    CHECK(version.build >= 19041u);
    CHECK(IsBuildOrGreater(10240));
}

AETHER_TEST("platform: win32 errors are categorized") {
    CHECK_EQ(ErrorFromWin32(ERROR_FILE_NOT_FOUND, "open").code, ErrorCode::NotFound);
    CHECK_EQ(ErrorFromWin32(ERROR_ACCESS_DENIED, "open").code, ErrorCode::AccessDenied);
    CHECK_EQ(ErrorFromHResult(HRESULT_FROM_WIN32(ERROR_DISK_FULL), "write").code, ErrorCode::InsufficientSpace);
    CHECK_EQ(ErrorFromHResult(E_ABORT, "x").code, ErrorCode::Cancelled);
    CHECK(ErrorFromWin32(ERROR_FILE_NOT_FOUND, "open").message.find("open: ") == 0);
}

AETHER_TEST("platform: ui dispatcher marshals work from background threads") {
    Win32UiDispatcher dispatcher;
    REQUIRE(dispatcher.Initialize().ok());
    CHECK(dispatcher.IsUiThread());
    std::atomic<int> sum{0};
    std::thread worker([&] {
        CHECK_FALSE(dispatcher.IsUiThread());
        for (int i = 1; i <= 10; ++i) dispatcher.Post([&sum, i] { sum += i; });
    });
    worker.join();
    CHECK_EQ(sum.load(), 0);  // nothing runs until the UI thread pumps
    PumpPendingMessages();
    CHECK_EQ(sum.load(), 55);
    dispatcher.Shutdown();
    CHECK_FALSE(dispatcher.Post([] {}));
}

AETHER_TEST("platform: message loop runs wait callbacks and exits on WM_QUIT") {
    MessageLoop loop;
    HANDLE event = ::CreateEventW(nullptr, FALSE, TRUE, nullptr);
    int fired = 0;
    loop.AddWaitHandle(event, [&] {
        ++fired;
        ::PostQuitMessage(7);
    });
    CHECK_EQ(loop.Run(), 7);
    CHECK_EQ(fired, 1);
    ::CloseHandle(event);
}

AETHER_TEST("platform: nested loop keeps servicing waits, intercepts first and re-posts WM_QUIT") {
    MessageLoop loop;
    PumpPendingMessages();
    HANDLE event = ::CreateEventW(nullptr, FALSE, TRUE, nullptr);
    REQUIRE(event != nullptr);
    int fired = 0;
    bool done = false;
    // Windows reports queued input before a signalled handle; the nested loop must still reach it.
    loop.AddWaitHandle(event, [&] { ++fired; done = true; });
    int filtered = 0;
    loop.AddMessageFilter([&](MSG& message) { if (message.message == WM_APP + 9) ++filtered; return false; });
    int intercepted = 0;
    ::PostThreadMessageW(::GetCurrentThreadId(), WM_APP + 9, 0, 0);
    const bool ended = loop.RunNested([&] { return done; }, [&](MSG& message) {
        if (message.message == WM_APP + 9) { ++intercepted; return true; }  // consumed: filters skipped
        return false;
    });
    CHECK(ended);
    CHECK_EQ(fired, 1);        // the signalled wait handle ran inside the nested loop and ended it
    CHECK_EQ(intercepted, 1);
    CHECK_EQ(filtered, 0);
    // Without an interceptor the filters run; WM_QUIT ends the nested loop and is re-posted.
    ::PostThreadMessageW(::GetCurrentThreadId(), WM_APP + 9, 0, 0);
    ::PostQuitMessage(5);
    CHECK_FALSE(loop.RunNested([] { return false; }));
    CHECK_EQ(filtered, 1);
    MSG quit{};
    REQUIRE(::PeekMessageW(&quit, nullptr, WM_QUIT, WM_QUIT, PM_REMOVE));
    CHECK_EQ(quit.wParam, WPARAM{5});
    ::CloseHandle(event);
}

AETHER_TEST("platform: single instance lock detects a second owner") {
    auto first = SingleInstanceLock::Acquire(L"AetherShell.Tests.SingleInstance");
    REQUIRE(first.ok());
    CHECK(first.value().IsPrimary());
    auto second = SingleInstanceLock::Acquire(L"AetherShell.Tests.SingleInstance");
    REQUIRE(second.ok());
    CHECK_FALSE(second.value().IsPrimary());
    CHECK(CurrentUserSid().value().rfind(L"S-1-", 0) == 0);
}

AETHER_TEST("platform: instance channel delivers allow-listed commands only") {
    InstanceChannel channel;
    std::wstring received;
    REQUIRE(channel.Listen(L"tests", [&](std::wstring_view command) { received = command; }).ok());
    CHECK_EQ(InstanceChannel::FindListenerProcessId(L"tests"), ::GetCurrentProcessId());
    CHECK(InstanceChannel::Send(L"tests", L"settings").ok());
    CHECK_EQ(received, std::wstring(L"settings"));
    CHECK_FALSE(InstanceChannel::Send(L"tests", L"Bad Command!").ok());
    CHECK_FALSE(InstanceChannel::Send(L"tests", std::wstring(100, L'a')).ok());
    channel.Close();
    CHECK_EQ(InstanceChannel::Send(L"tests", L"settings").error().code, ErrorCode::NotFound);
}

AETHER_TEST("platform: monitors and paths resolve") {
    const auto monitors = EnumerateMonitors();
    CHECK(!monitors.empty());
    const auto primary = PrimaryMonitor();
    CHECK(Width(primary.bounds) > 0);
    CHECK(Width(primary.workArea) <= Width(primary.bounds));

    const auto root = aether::test::MakeTempDirectory("paths");
    auto paths = AppPaths::Resolve(root / "data");
    REQUIRE(paths.ok());
    CHECK(std::filesystem::is_directory(paths.value().configDir));
    CHECK(std::filesystem::is_directory(paths.value().updatesDir));
    CHECK(paths.value().executablePath.filename() == L"aether_platform_tests.exe");
    std::filesystem::remove_all(root);
}

AETHER_TEST("platform: dispatcher exception details never enter public diagnostics") {
    const auto directory=test::MakeTempDirectory("dispatcher-privacy");
    diag::LoggerOptions logging;logging.directory=directory;logging.baseName=L"privacy";logging.writeToDebugger=false;
    diag::Logger::Initialize(logging);
    struct Shutdown { ~Shutdown(){diag::Logger::Shutdown();} } shutdown;
    Win32UiDispatcher dispatcher;REQUIRE(dispatcher.Initialize().ok());
    REQUIRE(dispatcher.Post([]{throw std::runtime_error("SYNTHETIC_PRIVATE_EXCEPTION_MARKER");}));
    bool continued=false;REQUIRE(dispatcher.Post([&]{continued=true;}));PumpPendingMessages();CHECK(continued);
    dispatcher.Shutdown();diag::Logger::Shutdown();
    auto log=fs::ReadFile(directory/L"privacy.log",1024*1024);REQUIRE(log.ok());
    CHECK(log->find("UI work item threw an exception")!=std::string::npos);
    CHECK(log->find("SYNTHETIC_PRIVATE_EXCEPTION_MARKER")==std::string::npos);
    std::filesystem::remove_all(directory);
}

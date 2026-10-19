// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
#include "AetherTest.h"

#include "applications/LaunchRules.h"

#include <cstdint>

using namespace aether;
using namespace aether::apps;

namespace {

HWND FakeWindow(std::uintptr_t value) { return reinterpret_cast<HWND>(value); }

RunningWindow Window(std::uintptr_t value, bool minimized = false) {
    return RunningWindow{FakeWindow(value), L"window", minimized};
}

}  // namespace

AETHER_TEST("launch rules: Win32 failures become user-facing errors") {
    const auto notFound = MakeLaunchError(ERROR_FILE_NOT_FOUND, L"Paint");
    CHECK_EQ(notFound.code, ErrorCode::NotFound);
    CHECK_EQ(notFound.message, std::string("Paint could not be found. It may have been moved or uninstalled."));
    CHECK_EQ(notFound.platformCode, std::int64_t{ERROR_FILE_NOT_FOUND});
    CHECK_EQ(MakeLaunchError(ERROR_PATH_NOT_FOUND, L"Paint").code, ErrorCode::NotFound);

    const auto declinedUac = MakeLaunchError(ERROR_CANCELLED, L"Registry Editor");
    CHECK_EQ(declinedUac.code, ErrorCode::Cancelled);
    CHECK_EQ(declinedUac.message, std::string("The launch of Registry Editor was cancelled."));

    const auto denied = MakeLaunchError(ERROR_ACCESS_DENIED, L"Tool");
    CHECK_EQ(denied.code, ErrorCode::AccessDenied);
    CHECK_EQ(denied.message, std::string("Windows denied access to Tool."));

    const auto elevation = MakeLaunchError(ERROR_ELEVATION_REQUIRED, L"Tool");
    CHECK_EQ(elevation.code, ErrorCode::AccessDenied);
    CHECK_EQ(elevation.message, std::string("Tool requires administrator rights."));

    CHECK_EQ(MakeLaunchError(ERROR_NO_ASSOCIATION, L"Tool").code, ErrorCode::Unsupported);
    CHECK_EQ(MakeLaunchError(ERROR_BAD_EXE_FORMAT, L"Tool").code, ErrorCode::Unsupported);

    const auto other = MakeLaunchError(ERROR_NOT_ENOUGH_MEMORY, L"Tool");
    CHECK_EQ(other.code, ErrorCode::Platform);
    CHECK_EQ(other.message, std::string("Windows could not start Tool."));

    CHECK_EQ(MakeLaunchError(ERROR_FILE_NOT_FOUND, L"  ").message,
             std::string("The application could not be found. It may have been moved or uninstalled."));
    // Untrusted names cannot inject line breaks into messages that may end up in logs.
    CHECK(MakeLaunchError(ERROR_FILE_NOT_FOUND, L"Evil\r\nName").message.find('\n') == std::string::npos);
}

AETHER_TEST("launch rules: HRESULT failures") {
    const auto cancelled = MakeLaunchErrorFromHResult(HRESULT_FROM_WIN32(ERROR_CANCELLED), L"App");
    CHECK_EQ(cancelled.code, ErrorCode::Cancelled);
    CHECK_EQ(cancelled.platformCode, std::int64_t{ERROR_CANCELLED});
    CHECK_EQ(MakeLaunchErrorFromHResult(E_ACCESSDENIED, L"App").code, ErrorCode::AccessDenied);
    const auto failed = MakeLaunchErrorFromHResult(E_FAIL, L"App");
    CHECK_EQ(failed.code, ErrorCode::Platform);
    CHECK_EQ(failed.platformCode, std::int64_t{0x80004005});
}

AETHER_TEST("launch rules: only missing routes fall through to the next one") {
    CHECK(ShouldTryNextLaunchRoute(MakeLaunchError(ERROR_FILE_NOT_FOUND, L"App")));
    CHECK(ShouldTryNextLaunchRoute(MakeError(ErrorCode::InvalidArgument, "bad path")));
    CHECK_FALSE(ShouldTryNextLaunchRoute(MakeLaunchError(ERROR_CANCELLED, L"App")));  // never re-prompt UAC
    CHECK_FALSE(ShouldTryNextLaunchRoute(MakeLaunchError(ERROR_ACCESS_DENIED, L"App")));
    CHECK_FALSE(ShouldTryNextLaunchRoute(MakeLaunchError(ERROR_NOT_ENOUGH_MEMORY, L"App")));
}

AETHER_TEST("activation choice: background application") {
    CHECK_FALSE(ChooseWindowToActivate({}, nullptr, nullptr).has_value());

    // Most recent window that is not minimized.
    const auto choice = ChooseWindowToActivate({Window(1, true), Window(2), Window(3)}, FakeWindow(99), FakeWindow(99));
    REQUIRE(choice.has_value());
    CHECK(choice->window == FakeWindow(2));
    CHECK_FALSE(choice->alreadyActive);
    CHECK_FALSE(choice->cycled);

    // Everything minimized: the most recent one (the caller restores it).
    const auto minimized = ChooseWindowToActivate({Window(1, true), Window(2, true)}, nullptr, nullptr);
    REQUIRE(minimized.has_value());
    CHECK(minimized->window == FakeWindow(1));
    CHECK_FALSE(minimized->minimize);

    // A lone minimized window is restored, never minimized again.
    const auto restore = ChooseWindowToActivate({Window(7, true)}, FakeWindow(99), FakeWindow(99));
    REQUIRE(restore.has_value());
    CHECK(restore->window == FakeWindow(7));
    CHECK_FALSE(restore->minimize);
    CHECK_FALSE(restore->alreadyActive);
}

AETHER_TEST("activation choice: application already in front") {
    // Taskbar parity: a click on the application whose only window is in front minimizes it.
    const auto single = ChooseWindowToActivate({Window(1)}, FakeWindow(1), FakeWindow(1));
    REQUIRE(single.has_value());
    CHECK(single->window == FakeWindow(1));
    CHECK(single->alreadyActive);
    CHECK(single->minimize);
    CHECK_FALSE(single->cycled);

    // The same through an owned dialog in front of the only window.
    const auto owned = ChooseWindowToActivate({Window(4)}, FakeWindow(40), FakeWindow(4));
    REQUIRE(owned.has_value());
    CHECK(owned->window == FakeWindow(4));
    CHECK(owned->minimize);

    // Cycles to the least recently active window.
    const auto cycle = ChooseWindowToActivate({Window(1), Window(2), Window(3)}, FakeWindow(1), FakeWindow(1));
    REQUIRE(cycle.has_value());
    CHECK(cycle->window == FakeWindow(3));
    CHECK(cycle->cycled);
    CHECK_FALSE(cycle->alreadyActive);
    CHECK_FALSE(cycle->minimize);

    // The foreground window is an owned dialog: its root owner identifies the application.
    const auto dialog = ChooseWindowToActivate({Window(5), Window(6)}, FakeWindow(42), FakeWindow(5));
    REQUIRE(dialog.has_value());
    CHECK(dialog->window == FakeWindow(6));
    CHECK(dialog->cycled);

    // The front window is the least recent entry of a stale list: pick the previous one.
    const auto stale = ChooseWindowToActivate({Window(1), Window(2), Window(3)}, FakeWindow(3), nullptr);
    REQUIRE(stale.has_value());
    CHECK(stale->window == FakeWindow(2));
}

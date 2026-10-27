// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
// The hidden-icons route against a fake Explorer: no taskbar, UI Automation or window is touched.
#include "AetherTest.h"
#include "app/HiddenIcons.h"

#include <functional>

using namespace aether;
using namespace aether::app;

namespace {

// A scripted taskbar: button found after `missingFor` invokes, overflow shown `opensAfter` checks
// after the invoke (never when `opensAfter` is negative).
struct FakeTray final : ITrayOverflow {
    int missingFor = 0;
    int opensAfter = 0;
    bool alreadyOpen = false;
    ErrorCode missing = ErrorCode::NotFound;
    ErrorCode failure = ErrorCode::Unknown;  // Unknown: invoking succeeds
    int invokes = 0, openChecks = 0, invokeAttempts = 0;
    std::function<void()> onCheck;

    Result<void> InvokeOverflowButton() override {
        if (invokeAttempts++ < missingFor) return MakeError(missing, "not populated yet");
        if (failure != ErrorCode::Unknown) return MakeError(failure, "fake failure");
        ++invokes;
        return Ok();
    }
    bool OverflowOpen() override {
        if (onCheck) onCheck();
        if (alreadyOpen) return true;
        if (invokes == 0 || opensAfter < 0) return false;
        return openChecks++ >= opensAfter;
    }
};

OverflowTiming Fast() {
    OverflowTiming timing;
    timing.attempts = 4;
    timing.interval = std::chrono::milliseconds{75};
    timing.openWait = std::chrono::milliseconds{900};
    return timing;
}

}  // namespace

AETHER_TEST("hidden icons: the overflow button is invoked once and success means Windows showed the overflow") {
    FakeTray tray;
    tray.opensAfter = 3;
    std::chrono::milliseconds slept{0};
    const auto result = OpenTrayOverflow(tray, {}, Fast(), [&](std::chrono::milliseconds d) { slept += d; });
    CHECK(result.ok());
    CHECK_EQ(tray.invokes, 1);
    CHECK(slept.count() > 0);  // it waited for the window instead of assuming success
}

AETHER_TEST("hidden icons: an overflow that is already open is not invoked again") {
    FakeTray tray;
    tray.alreadyOpen = true;
    CHECK(OpenTrayOverflow(tray, {}, Fast(), [](std::chrono::milliseconds) {}).ok());
    CHECK_EQ(tray.invokeAttempts, 0);  // invoking would close it
}

AETHER_TEST("hidden icons: a taskbar still populating its content is waited for briefly") {
    FakeTray tray;
    tray.missingFor = 2;
    tray.opensAfter = 0;
    CHECK(OpenTrayOverflow(tray, {}, Fast(), [](std::chrono::milliseconds) {}).ok());
    CHECK_EQ(tray.invokes, 1);
    CHECK_EQ(tray.invokeAttempts, 3);
}

AETHER_TEST("hidden icons: no overflow after the invoke is a failure, not a second click") {
    FakeTray tray;
    tray.opensAfter = -1;
    const auto result = OpenTrayOverflow(tray, {}, Fast(), [](std::chrono::milliseconds) {});
    REQUIRE_FALSE(result.ok());
    CHECK(result.error().code == ErrorCode::Unavailable);
    CHECK_EQ(tray.invokes, 1);
}

AETHER_TEST("hidden icons: unsupported or missing buttons, no taskbar and cancellation end honestly and soon") {
    FakeTray unsupported;
    unsupported.failure = ErrorCode::Unsupported;
    auto result = OpenTrayOverflow(unsupported, {}, Fast(), [](std::chrono::milliseconds) {});
    REQUIRE_FALSE(result.ok());
    CHECK(result.error().code == ErrorCode::Unsupported);
    CHECK_EQ(unsupported.invokeAttempts, 1);

    FakeTray missing;
    missing.missingFor = 1000;  // no hidden icons at all: the taskbar shows no button
    std::chrono::milliseconds slept{0};
    result = OpenTrayOverflow(missing, {}, Fast(), [&](std::chrono::milliseconds d) { slept += d; });
    REQUIRE_FALSE(result.ok());
    CHECK(result.error().code == ErrorCode::NotFound);
    CHECK_EQ(missing.invokes, 0);
    CHECK_EQ(missing.invokeAttempts, 4);
    CHECK(slept.count() <= 300);  // reported within a moment, not after a long wait

    FakeTray absent;
    absent.missingFor = 1000;
    absent.missing = ErrorCode::Unavailable;  // Explorer's taskbar is gone (restarting)
    result = OpenTrayOverflow(absent, {}, Fast(), [](std::chrono::milliseconds) {});
    REQUIRE_FALSE(result.ok());
    CHECK(result.error().code == ErrorCode::Unavailable);
    CHECK_EQ(absent.invokes, 0);

    CancellationSource source;
    FakeTray cancelled;
    cancelled.missingFor = 2;
    cancelled.onCheck = [&] { if (cancelled.invokeAttempts >= 1) source.Cancel(); };
    result = OpenTrayOverflow(cancelled, source.Token(), Fast(), [](std::chrono::milliseconds) {});
    REQUIRE_FALSE(result.ok());
    CHECK(result.error().code == ErrorCode::Cancelled);
    CHECK_EQ(cancelled.invokes, 0);
}

AETHER_TEST("hidden icons: only a single chevron glyph identifies the overflow button, in any language") {
    for (const wchar_t glyph : {L'\xE971', L'\xE972', L'\xE70E', L'\xE70D', L'\xE96D', L'\xE96E'})
        CHECK(IsOverflowGlyph(std::wstring_view(&glyph, 1)));
    // Other tray glyphs (keyboard, volume, network, battery, notifications) and text never match.
    for (const wchar_t glyph : {L'\xE765', L'\xE995', L'\xEC3F', L'\xF8DB', L'\xF2A8', L'\xE76B', L'\xE76C', L'^', L'v'})
        CHECK_FALSE(IsOverflowGlyph(std::wstring_view(&glyph, 1)));
    CHECK_FALSE(IsOverflowGlyph(L""));
    CHECK_FALSE(IsOverflowGlyph(L"\xE971\xE971"));
    CHECK_FALSE(IsOverflowGlyph(L"Show hidden icons"));
}

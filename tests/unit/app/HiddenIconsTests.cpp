// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
// The hidden-icons route against a fake Explorer: no taskbar, UI Automation or window is touched.
#include "AetherTest.h"
#include "app/HiddenIcons.h"

#include <functional>

using namespace aether;
using namespace aether::app;

namespace {

// A scripted taskbar: visible after `hiddenFor` checks, button found after `missingFor` invokes,
// overflow shown `opensAfter` checks after the invoke (never when `opensAfter` is negative).
struct FakeTray final : ITrayOverflow {
    int hiddenFor = 0;
    int missingFor = 0;
    int opensAfter = 0;
    bool alreadyOpen = false;
    ErrorCode failure = ErrorCode::Unknown;  // Unknown: invoking succeeds
    int visibleChecks = 0, invokes = 0, openChecks = 0, invokeAttempts = 0;
    std::function<void()> onCheck;

    bool TaskbarVisible() override { return visibleChecks++ >= hiddenFor; }
    Result<void> InvokeOverflowButton() override {
        if (invokeAttempts++ < missingFor) return MakeError(ErrorCode::NotFound, "not populated yet");
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
    timing.attempts = 12;
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

AETHER_TEST("hidden icons: a taskbar coming back and content still populating are waited for") {
    FakeTray tray;
    tray.hiddenFor = 3;
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

AETHER_TEST("hidden icons: unsupported buttons, missing buttons, a hidden taskbar and cancellation end honestly") {
    FakeTray unsupported;
    unsupported.failure = ErrorCode::Unsupported;
    auto result = OpenTrayOverflow(unsupported, {}, Fast(), [](std::chrono::milliseconds) {});
    REQUIRE_FALSE(result.ok());
    CHECK(result.error().code == ErrorCode::Unsupported);
    CHECK_EQ(unsupported.invokeAttempts, 1);

    FakeTray missing;
    missing.missingFor = 1000;  // no hidden icons at all: the taskbar shows no button
    result = OpenTrayOverflow(missing, {}, Fast(), [](std::chrono::milliseconds) {});
    REQUIRE_FALSE(result.ok());
    CHECK(result.error().code == ErrorCode::NotFound);
    CHECK_EQ(missing.invokes, 0);

    FakeTray hidden;
    hidden.hiddenFor = 1000;
    result = OpenTrayOverflow(hidden, {}, Fast(), [](std::chrono::milliseconds) {});
    REQUIRE_FALSE(result.ok());
    CHECK(result.error().code == ErrorCode::Timeout);
    CHECK_EQ(hidden.invokeAttempts, 0);

    CancellationSource source;
    FakeTray cancelled;
    cancelled.hiddenFor = 2;
    cancelled.onCheck = [&] { if (cancelled.visibleChecks >= 1) source.Cancel(); };
    result = OpenTrayOverflow(cancelled, source.Token(), Fast(), [](std::chrono::milliseconds) {});
    REQUIRE_FALSE(result.ok());
    CHECK(result.error().code == ErrorCode::Cancelled);
    CHECK_EQ(cancelled.invokeAttempts, 0);
}

AETHER_TEST("hidden icons: only a taskbar Aether currently hides has to be shown first") {
    TaskbarUpdate state;
    CHECK_FALSE(OverflowNeedsReveal(state));          // Windows' taskbar is visible
    state.active = true;
    CHECK(OverflowNeedsReveal(state));                // hidden by replacement
    state.suspended = true;
    CHECK_FALSE(OverflowNeedsReveal(state));          // already shown (suspended)
}

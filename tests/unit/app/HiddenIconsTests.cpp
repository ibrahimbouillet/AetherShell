// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
// The hidden-icons route against a fake Explorer: no taskbar, UI Automation or window is touched.
#include "AetherTest.h"
#include "app/HiddenIcons.h"

#include <functional>
#include <vector>

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
    std::optional<RECT> OverflowBounds() override { return std::nullopt; }
    bool MoveOverflow(POINT) override { return false; }
};

// Explorer's shown overflow window: where Windows put it, how often Windows puts it back after a
// move (`fightsBack`), and when it is dismissed (after `closesAfter` bounds reads; never if negative).
struct FakeOverflowWindow final : ITrayOverflow {
    std::optional<RECT> shown;
    RECT windowsPlace{};
    int fightsBack = 0;
    int closesAfter = -1;
    int reads = 0;
    std::vector<POINT> moves;

    Result<void> InvokeOverflowButton() override { return Ok(); }
    bool OverflowOpen() override { return shown.has_value(); }
    std::optional<RECT> OverflowBounds() override {
        if (closesAfter >= 0 && reads++ >= closesAfter) shown.reset();
        return shown;
    }
    bool MoveOverflow(POINT topLeft) override {
        if (!shown) return false;
        moves.push_back(topLeft);
        if (fightsBack-- > 0) { shown = windowsPlace; return true; }  // Explorer re-placed it again
        ::OffsetRect(&*shown, topLeft.x - shown->left, topLeft.y - shown->top);
        return true;
    }
};

// A 1920 x 1080 monitor at 144 DPI with a Top Bar reserving its top 90 px; the chevron sits at the right.
constexpr RECT kWork{0, 90, 1920, 1080};
constexpr RECT kChevron{1500, 12, 1545, 72};
constexpr RECT kBottomRight{1255, 930, 1606, 1101};  // where Windows puts it for its hidden taskbar

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

AETHER_TEST("hidden icons: the overflow goes centred below the chevron and stays on its monitor") {
    // Centred below the chevron, a small gap under it.
    POINT at = OverflowPlacement(kChevron, SIZE{351, 171}, kWork, 9, 12);
    CHECK_EQ(at.x, 1522 - 175);
    CHECK_EQ(at.y, 72 + 9);
    // Near the right edge it slides left to stay whole; near the left edge, right.
    at = OverflowPlacement(RECT{1880, 12, 1910, 72}, SIZE{351, 171}, kWork, 9, 12);
    CHECK_EQ(at.x, 1920 - 12 - 351);
    at = OverflowPlacement(RECT{0, 12, 30, 72}, SIZE{351, 171}, kWork, 9, 12);
    CHECK_EQ(at.x, 12);
    // Too little room below: it moves up only as far as the work area requires.
    at = OverflowPlacement(RECT{1500, 950, 1545, 1000}, SIZE{351, 171}, kWork, 9, 12);
    CHECK_EQ(at.y, 1080 - 12 - 171);
}

AETHER_TEST("hidden icons: the shown overflow is moved below the chevron once and held there") {
    FakeOverflowWindow tray;
    tray.shown = kBottomRight;
    REQUIRE(PlaceTrayOverflow(tray, kChevron, kWork, 144, {}, {}, [](std::chrono::milliseconds) {}).ok());
    REQUIRE_EQ(tray.moves.size(), std::size_t{1});
    const POINT expected = OverflowPlacement(kChevron, SIZE{351, 171}, kWork, 9, 12);
    CHECK_EQ(tray.moves[0].x, expected.x);
    CHECK_EQ(tray.moves[0].y, expected.y);
    CHECK_EQ(tray.shown->left, expected.x);
    CHECK_EQ(tray.shown->right - tray.shown->left, 351);  // never resized

    // Explorer re-placing it after the first move is corrected again.
    FakeOverflowWindow relaid;
    relaid.shown = relaid.windowsPlace = kBottomRight;
    relaid.fightsBack = 1;
    REQUIRE(PlaceTrayOverflow(relaid, kChevron, kWork, 144, {}, {}, [](std::chrono::milliseconds) {}).ok());
    CHECK_EQ(relaid.moves.size(), std::size_t{2});
    CHECK_EQ(relaid.shown->top, expected.y);
}

AETHER_TEST("hidden icons: placement ends quietly when dismissed and gives up against a persistent re-layout") {
    FakeOverflowWindow dismissed;
    dismissed.shown = kBottomRight;
    dismissed.closesAfter = 0;  // the person clicked elsewhere before it could move
    CHECK(PlaceTrayOverflow(dismissed, kChevron, kWork, 144, {}, {}, [](std::chrono::milliseconds) {}).ok());
    CHECK(dismissed.moves.empty());

    FakeOverflowWindow stubborn;
    stubborn.shown = stubborn.windowsPlace = kBottomRight;
    stubborn.fightsBack = 1000;
    OverflowTiming timing;
    timing.moves = 3;
    const auto result = PlaceTrayOverflow(stubborn, kChevron, kWork, 144, {}, timing, [](std::chrono::milliseconds) {});
    REQUIRE_FALSE(result.ok());
    CHECK(result.error().code == ErrorCode::Unavailable);
    CHECK_EQ(stubborn.moves.size(), std::size_t{3});  // bounded: no tug of war with Explorer

    CancellationSource source;
    source.Cancel();
    FakeOverflowWindow cancelled;
    cancelled.shown = kBottomRight;
    const auto stopped = PlaceTrayOverflow(cancelled, kChevron, kWork, 144, source.Token(), {}, [](std::chrono::milliseconds) {});
    REQUIRE_FALSE(stopped.ok());
    CHECK(stopped.error().code == ErrorCode::Cancelled);
    CHECK(cancelled.moves.empty());
}

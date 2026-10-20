// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
#include "AetherTest.h"
#include "platform/windows/MonitorUtilities.h"

#include <limits>

using namespace aether::platform;

AETHER_TEST("platform fullscreen: a maximized framed app is ordinary even at complete monitor bounds") {
    const RECT monitor{0, 0, 1920, 1080};
    const DWORD framed = WS_VISIBLE | WS_OVERLAPPEDWINDOW | WS_MAXIMIZE;
    CHECK_FALSE(IsFullscreenWindowGeometry(monitor, monitor, framed));
    CHECK_FALSE(IsFullscreenWindowGeometry(RECT{-8, -8, 1928, 1088}, monitor, framed));
    CHECK_FALSE(IsFullscreenWindowGeometry(monitor, monitor, WS_MAXIMIZE | WS_CAPTION));
    CHECK_FALSE(IsFullscreenWindowGeometry(monitor, monitor, WS_MAXIMIZE | WS_THICKFRAME));
    CHECK_FALSE(IsFullscreenWindowGeometry(monitor, monitor, WS_POPUP | WS_CHILD));
}

AETHER_TEST("platform fullscreen: borderless fullscreen permits maximized and frame-stripped overlapped styles") {
    const RECT monitor{-1920, -200, 0, 880};
    CHECK(IsFullscreenWindowGeometry(monitor, monitor, WS_VISIBLE | WS_POPUP));
    CHECK(IsFullscreenWindowGeometry(monitor, monitor, WS_VISIBLE | WS_POPUP | WS_MAXIMIZE));
    // Chromium removes caption/thickframe for fullscreen while retaining the other saved style bits.
    const DWORD frameStripped = (WS_VISIBLE | WS_OVERLAPPEDWINDOW | WS_MAXIMIZE) & ~(WS_CAPTION | WS_THICKFRAME);
    CHECK(IsFullscreenWindowGeometry(monitor, monitor, frameStripped));
    CHECK(IsFullscreenWindowGeometry(monitor, monitor, 0));
    CHECK(IsFullscreenWindowGeometry(RECT{-1930, -210, 10, 890}, monitor, WS_POPUP));
}

AETHER_TEST("platform fullscreen: every monitor edge must be covered and malformed rectangles fail closed") {
    const RECT monitor{-1920, -200, 0, 880};
    CHECK_FALSE(IsFullscreenWindowGeometry(RECT{-1919, -200, 0, 880}, monitor, WS_POPUP));
    CHECK_FALSE(IsFullscreenWindowGeometry(RECT{-1920, -199, 0, 880}, monitor, WS_POPUP));
    CHECK_FALSE(IsFullscreenWindowGeometry(RECT{-1920, -200, -1, 880}, monitor, WS_POPUP));
    CHECK_FALSE(IsFullscreenWindowGeometry(RECT{-1920, -200, 0, 879}, monitor, WS_POPUP));
    CHECK_FALSE(IsFullscreenWindowGeometry(RECT{0, 0, 0, 1080}, monitor, WS_POPUP));
    CHECK_FALSE(IsFullscreenWindowGeometry(monitor, RECT{0, 0, 1920, 0}, WS_POPUP));
    CHECK_FALSE(IsFullscreenWindowGeometry(monitor, RECT{1, 0, 0, 1}, WS_POPUP));
    const RECT extreme{std::numeric_limits<LONG>::min(), std::numeric_limits<LONG>::min(),
        std::numeric_limits<LONG>::max(), std::numeric_limits<LONG>::max()};
    CHECK(IsFullscreenWindowGeometry(extreme, extreme, WS_POPUP | WS_MAXIMIZE));
}

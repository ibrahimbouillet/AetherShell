// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
#include "AetherTest.h"
#include "showcase/DxgiDesktopCapture.h"

#include <limits>

using namespace aether;
using namespace aether::showcase;

AETHER_TEST("showcase DXGI capture: invalid geometry fails before opening a display or acquiring pixels") {
    const RECT invalid[] = {
        {40, 40, 40, 80},
        {40, 80, 80, 40},
        {0, 0, 16'385, 1080},
        {std::numeric_limits<LONG>::min(), 0, std::numeric_limits<LONG>::max(), 1080},
    };
    for (const RECT& bounds : invalid) {
        const auto capture = DxgiDesktopCaptureSession::Create(bounds);
        CHECK_FALSE(capture.ok());
        CHECK_EQ(capture.error().code, ErrorCode::InvalidArgument);
    }
}

AETHER_TEST("showcase DXGI capture: empty session cannot return fabricated desktop pixels or metrics") {
    DxgiDesktopCaptureSession capture;
    const auto frame = capture.Capture(true);
    CHECK_FALSE(frame.ok());
    CHECK_EQ(frame.error().code, ErrorCode::InvalidState);
    CHECK_FALSE(capture.UpdatedLastCapture());
    CHECK_EQ(capture.LastPresentCounter(), 0);
    CHECK_EQ(capture.AcquiredFrameCount(), 0U);
    CHECK_EQ(capture.DesktopFrameCount(), 0U);
}

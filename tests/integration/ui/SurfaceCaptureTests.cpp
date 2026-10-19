// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
//
// SurfaceWindow capture notifications on a private window that is never shown. Only messages sent
// to that window and its own thread's capture are used; no global input is injected.
#include "AetherTest.h"

#include "graphics/FrameScheduler.h"
#include "graphics/GraphicsDevice.h"
#include "platform/windows/ComApartment.h"
#include "platform/windows/MessageLoop.h"
#include "ui/SurfaceWindow.h"

using namespace aether;

namespace {

class CaptureProbe final : public ui::SurfaceWindow {
public:
    using SurfaceWindow::SurfaceWindow;
    ~CaptureProbe() override { Destroy(); }

    int lost = 0;
    int downs = 0;
    int ups = 0;
    bool upAfterOwnRelease = false;

protected:
    void OnRender(const ui::RenderContext&) override {}
    void OnPointerDown(ui::PointerButton, D2D1_POINT_2F) override { ++downs; }
    void OnPointerUp(ui::PointerButton, D2D1_POINT_2F) override {
        ++ups;
        upAfterOwnRelease = ::GetCapture() != Handle();
    }
    void OnPointerCaptureLost() override { ++lost; }
};

}  // namespace

AETHER_TEST("ui surface: capture loss is reported except for the release after the left button goes up") {
    platform::ComApartment com(COINIT_APARTMENTTHREADED);
    platform::MessageLoop loop;
    graphics::GraphicsDevice device;
    REQUIRE(device.Initialize().ok());
    graphics::FrameScheduler scheduler;
    REQUIRE(scheduler.Initialize(loop).ok());
    CaptureProbe window(device, scheduler);
    ui::SurfaceWindowOptions options;
    options.className = L"AetherShell.Tests.CaptureProbe";
    options.title = L"Capture probe";
    REQUIRE(window.Create(options, RECT{0, 0, 64, 64}).ok());
    const HWND hwnd = window.Handle();
    CHECK_FALSE(window.IsShown());

    // Another window or the system took the capture.
    ::SendMessageW(hwnd, WM_CAPTURECHANGED, 0, 0);
    CHECK_EQ(window.lost, 1);

    // A normal click: the base class captures on button-down and releases on button-up without a loss.
    ::SendMessageW(hwnd, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(8, 8));
    CHECK(::GetCapture() == hwnd);
    ::SendMessageW(hwnd, WM_LBUTTONUP, 0, MAKELPARAM(8, 8));
    CHECK(::GetCapture() != hwnd);
    CHECK_EQ(window.downs, 1);
    CHECK_EQ(window.ups, 1);
    CHECK(window.upAfterOwnRelease);
    CHECK_EQ(window.lost, 1);

    // Capture released by someone else while the button is down (for example a drag interrupted).
    ::SendMessageW(hwnd, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(8, 8));
    CHECK(::GetCapture() == hwnd);
    ::ReleaseCapture();
    CHECK_EQ(window.lost, 2);
    ::SendMessageW(hwnd, WM_LBUTTONUP, 0, MAKELPARAM(8, 8));
    CHECK_EQ(window.ups, 2);
    CHECK_EQ(window.lost, 2);
    window.Destroy();
    CHECK(::GetCapture() != hwnd);
}

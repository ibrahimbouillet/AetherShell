// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
#include "AetherTest.h"

#include "graphics/CompositionSurface.h"
#include "graphics/GraphicsDevice.h"
#include "platform/windows/ComApartment.h"
#include "platform/windows/WindowUtilities.h"

using namespace aether;

namespace {
// A hidden, never-shown own window: the surface needs only a composition target.
struct HiddenWindow {
    HWND window = nullptr;
    HiddenWindow() {
        WNDCLASSEXW type{sizeof(type)};
        type.hInstance = ::GetModuleHandleW(nullptr);
        type.lpszClassName = L"AetherShell.Test.CompositionSurface";
        type.lpfnWndProc = ::DefWindowProcW;
        REQUIRE(platform::RegisterWindowClassOnce(type).ok());
        window = ::CreateWindowExW(WS_EX_NOREDIRECTIONBITMAP | WS_EX_TOOLWINDOW, type.lpszClassName, L"Aether surface fixture",
                                   WS_POPUP, 0, 0, 64, 64, nullptr, nullptr, type.hInstance, nullptr);
        REQUIRE(window != nullptr);
    }
    ~HiddenWindow() { if (window) ::DestroyWindow(window); }
};
}  // namespace

AETHER_TEST("composition surface: reattaching sizes the new drawing surface even for an unchanged size (AS4-24)") {
    platform::ComApartment com{COINIT_APARTMENTTHREADED};
    graphics::GraphicsDevice device;
    REQUIRE(device.Initialize().ok());
    HiddenWindow host;
    graphics::CompositionSurface surface;
    REQUIRE(surface.Attach(device, host.window).ok());
    REQUIRE(surface.Resize(200, 100).ok());
    CHECK_EQ(surface.DrawingSizePx().cx, 200L); CHECK_EQ(surface.DrawingSizePx().cy, 100L);
    surface.Detach();
    CHECK_EQ(surface.DrawingSizePx().cx, 0L);
    REQUIRE(surface.Attach(device, host.window).ok());
    // Before the correction the cached 200x100 made this a no-op and left the new surface 1x1.
    REQUIRE(surface.Resize(200, 100).ok());
    CHECK_EQ(surface.DrawingSizePx().cx, 200L); CHECK_EQ(surface.DrawingSizePx().cy, 100L);
    surface.Detach();
    REQUIRE(surface.Attach(device, host.window).ok());
    REQUIRE(surface.Resize(320, 90).ok());
    CHECK_EQ(surface.DrawingSizePx().cx, 320L); CHECK_EQ(surface.DrawingSizePx().cy, 90L);
    surface.Detach();
    device.Shutdown();
}

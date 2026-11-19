// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
//
// D84, D86: Liquid's smart transparency as it is composed on screen. A Liquid form with one label, over the private
// striped backdrop of these tests (an ordinary window behind it): the stripes are blurred around the label and far from
// it alike, since a window lies behind the whole form (D86: nothing of a window behind is read through the form). The
// blur's window sits directly below the form, click-through and never activated, and it goes when the setting is turned
// off. Captures read only our own windows.
#include "AetherTest.h"
#include "FormTestSupport.h"

#include "graphics/GraphicsDevice.h"
#include "graphics/Theme.h"
#include "platform/windows/ComApartment.h"
#include "platform/windows/WindowUtilities.h"
#include "ui/form/FormSurface.h"
#include "ui/form/SmartGlass.h"

#include <algorithm>
#include <cstdlib>
#include <string>

using namespace aether;
using namespace aether::ui::form;
using namespace aether::test::form;
using aether::graphics::ThemeMode;

namespace {

// How much of the stripes' edges is lost in `area` (capture pixels): the mean and the largest change of the step from
// each pixel to the next, between the stripes alone and the stripes seen through the form. Liquid's sheen adds a
// smooth light, which keeps the steps; a blur removes them.
struct EdgeLoss {
    double mean = 0.0;
    int largest = 0;
};
EdgeLoss MeasureEdgeLoss(const Capture& plain, const Capture& shown, const RECT& area) {
    EdgeLoss loss;
    double total = 0.0;
    std::size_t count = 0;
    for (int y = area.top; y < area.bottom; ++y) {
        for (int x = area.left; x + 1 < area.right; ++x) {
            const auto* a0 = plain.Pixel(x, y);
            const auto* a1 = plain.Pixel(x + 1, y);
            const auto* b0 = shown.Pixel(x, y);
            const auto* b1 = shown.Pixel(x + 1, y);
            for (int c = 0; c < 3; ++c) {
                const int change = std::abs((b1[c] - b0[c]) - (a1[c] - a0[c]));
                loss.largest = std::max(loss.largest, change);
                total += change;
                ++count;
            }
        }
    }
    loss.mean = count ? total / static_cast<double>(count) : 0.0;
    return loss;
}

// The mean step from each pixel to the next in `area`: how sharp the stripes are there.
double MeanStep(const Capture& capture, const RECT& area) {
    double total = 0.0;
    std::size_t count = 0;
    for (int y = area.top; y < area.bottom; ++y)
        for (int x = area.left; x + 1 < area.right; ++x)
            for (int c = 0; c < 3; ++c) {
                total += std::abs(capture.Pixel(x + 1, y)[c] - capture.Pixel(x, y)[c]);
                ++count;
            }
    return count ? total / static_cast<double>(count) : 0.0;
}

HWND FindGlassWindow() {
    for (HWND window = ::FindWindowExW(nullptr, nullptr, L"AetherShell.SmartGlass", nullptr); window;
         window = ::FindWindowExW(nullptr, window, L"AetherShell.SmartGlass", nullptr)) {
        DWORD process = 0;
        ::GetWindowThreadProcessId(window, &process);
        if (process == ::GetCurrentProcessId()) return window;
    }
    return nullptr;
}

}  // namespace

AETHER_TEST("smart glass integration: a window behind is blurred under the content and away from it, off with the setting") {
    platform::ComApartment com(COINIT_APARTMENTTHREADED);
    if (!SystemBackdropSupported() || !platform::SystemTransparencyEnabled()) {
        TEST_NOTE("SKIPPED: no Acrylic material here (Windows 10, or transparency effects off)");
        return;
    }
    graphics::GraphicsDevice device;
    REQUIRE(device.Initialize().ok());
    if (!device.Capabilities().hostBackdrop) {
        TEST_NOTE("SKIPPED: the compositor cannot blur the desktop here");
        device.Shutdown();
        return;
    }
    {
        auto tokens = graphics::MakeDesignTokens(ThemeMode::Dark);
        tokens.transparencyEnabled = true;
        tokens.liquid = true;
        tokens.liquidSmart = true;
        graphics::MotionPolicy motion;
        motion.reduced = true;
        FormHost host;
        REQUIRE(host.Create(420.0f, 240.0f, L"Aether smart glass test"));
        REQUIRE(host.surface.Attach(host.hwnd, tokens, motion).ok());
        const HWND label = host.Add(host.hwnd, L"STATIC", L"Smart glass", SS_OWNERDRAW, 101);
        ControlSpec spec;
        spec.role = ControlRole::Label;
        spec.text = TextRole::Title;
        REQUIRE(host.surface.Register(label, spec).ok());
        host.surface.SetFrame(label, host.PxRect(16.0f, 16.0f, 180.0f, 48.0f));
        host.surface.EnableSmartGlass(&device);
        CHECK(host.surface.SmartGlassActive());

        const RECT client = host.ClientOnScreen();
        RECT guard = client;
        ::InflateRect(&guard, 80, 80);
        BackdropGuard backdrop;
        backdrop.hwnd = CreateTestBackdrop(guard);
        REQUIRE(backdrop.hwnd != nullptr);
        Pump(300);
        const auto plain = CaptureWithRetry(backdrop.hwnd, guard, client);   // the stripes alone
        host.Show();
        Pump(900);

        const HWND glass = FindGlassWindow();
        REQUIRE(glass != nullptr);
        CHECK(::GetWindow(host.hwnd, GW_HWNDNEXT) == glass);   // directly below the form
        const auto ex = ::GetWindowLongPtrW(glass, GWL_EXSTYLE);
        CHECK((ex & WS_EX_TRANSPARENT) != 0);   // clicks go through to what is behind
        CHECK((ex & WS_EX_NOACTIVATE) != 0);
        CHECK((ex & WS_EX_TOOLWINDOW) != 0);    // no taskbar entry, not in Alt+Tab
        RECT placed{};
        REQUIRE(::GetWindowRect(glass, &placed));
        CHECK(::EqualRect(&placed, &client));   // exactly under the client area
        CHECK_FALSE(host.surface.Tokens().liquidSmart == false);

        const auto shown = CaptureWithRetry(backdrop.hwnd, guard, client);
        if (!plain.Valid() || !shown.Valid()) {
            TEST_NOTE("inconclusive: a foreign window kept overlapping the private backdrop");
        } else {
            CHECK(SavePng(shown, InspectionPath("smart-glass")));
            TEST_NOTE("saved " + InspectionPath("smart-glass").string());
            // Beside the label's text, inside its frame: the stripes' edges are gone.
            const RECT under{host.Px(150.0f), host.Px(20.0f), host.Px(175.0f), host.Px(46.0f)};
            const auto blurred = MeasureEdgeLoss(plain, shown, under);
            const double underSteps = MeanStep(plain, under);
            TEST_NOTE("under the content: edge loss mean " + std::to_string(blurred.mean) + ", largest " + std::to_string(blurred.largest) +
                      " (stripes' mean step " + std::to_string(underSteps) + ")");
            CHECK(blurred.largest >= 40);
            CHECK(blurred.mean > underSteps * 0.5);
            // D86: far from the label (and inside the rim of light along the edge), over the same window: gone too, so
            // no line of the window behind reads between the form's own content.
            const RECT away{host.Px(250.0f), host.Px(120.0f), host.Px(400.0f), host.Px(220.0f)};
            const auto distant = MeasureEdgeLoss(plain, shown, away);
            const double awaySteps = MeanStep(plain, away);
            TEST_NOTE("far from the content: edge loss mean " + std::to_string(distant.mean) + ", largest " + std::to_string(distant.largest) +
                      " (stripes' mean step " + std::to_string(awaySteps) + ")");
            CHECK(distant.largest >= 40);
            CHECK(distant.mean > awaySteps * 0.5);
            // The mask says the same: every cell of the client is fully blurred.
            const auto* mask = host.surface.SmartGlassMask();
            REQUIRE(mask != nullptr);
            CHECK(!mask->cells.empty());
            CHECK(std::all_of(mask->cells.begin(), mask->cells.end(), [](auto v) { return v == 255; }));
        }

        // The setting off: the window's ordinary Liquid glass, and no blur window left.
        tokens.liquidSmart = false;
        host.surface.SetTheme(tokens, motion);
        Pump(200);
        CHECK_FALSE(host.surface.SmartGlassActive());
        CHECK(FindGlassWindow() == nullptr);
        host.Destroy();
    }
    device.Shutdown();
}

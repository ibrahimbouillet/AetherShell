// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
//
// Compositor-driven show and hide transitions of SurfaceWindow, and the tooltip's fade, slide and
// warm-up. The windows are this test's own: tiny, transparent to hit-testing, never activated and
// placed in a corner of the primary work area for well under a second. Nothing is captured, no
// input is injected and no other window is touched.
#include "AetherTest.h"

#include "graphics/FrameScheduler.h"
#include "graphics/GraphicsDevice.h"
#include "graphics/Theme.h"
#include "graphics/Transition.h"
#include "platform/windows/ComApartment.h"
#include "platform/windows/Handles.h"
#include "platform/windows/MessageLoop.h"
#include "platform/windows/MonitorUtilities.h"
#include "ui/SurfaceWindow.h"
#include "ui/TooltipWindow.h"

#include <chrono>

using namespace aether;

namespace {

class Probe final : public ui::SurfaceWindow {
public:
    using SurfaceWindow::SurfaceWindow;
    ~Probe() override { Destroy(); }

protected:
    void OnRender(const ui::RenderContext&) override {}  // nothing visible
    std::optional<LRESULT> OnMessage(UINT message, WPARAM, LPARAM) override {
        if (message == WM_NCHITTEST) return HTTRANSPARENT;
        return std::nullopt;
    }
};

class RegionProbe final : public ui::SurfaceWindow {
public:
    using SurfaceWindow::SurfaceWindow;
    ~RegionProbe() override { Destroy(); }
protected:
    void OnRender(const ui::RenderContext&) override {}
};

void RunFor(platform::MessageLoop& loop, DWORD milliseconds) {
    HANDLE timer = ::CreateWaitableTimerW(nullptr, TRUE, nullptr);
    REQUIRE(timer != nullptr);
    LARGE_INTEGER due{};
    due.QuadPart = -static_cast<LONGLONG>(milliseconds) * 10'000;
    REQUIRE(::SetWaitableTimer(timer, &due, 0, nullptr, nullptr, FALSE));
    const auto id = loop.AddWaitHandle(timer, [] { ::PostQuitMessage(0); });
    loop.Run();
    loop.RemoveWaitHandle(id);
    ::CloseHandle(timer);
}

// Pumps until `done` holds or `limitMs` passed; returns the elapsed milliseconds.
template <class Predicate>
long long RunUntil(platform::MessageLoop& loop, Predicate done, int limitMs) {
    const auto start = std::chrono::steady_clock::now();
    while (!done()) {
        const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count();
        if (elapsed >= limitMs) break;
        RunFor(loop, 10);
    }
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count();
}

graphics::Transition Timed(double seconds, float opacityFrom, float opacityTo, float offsetFrom, float offsetTo) {
    graphics::Transition transition;
    transition.opacityFrom = opacityFrom;
    transition.opacityTo = opacityTo;
    transition.offsetFromY = offsetFrom;
    transition.offsetToY = offsetTo;
    transition.duration = seconds;
    transition.curve = graphics::CurveTokens{}.standard;
    return transition;
}

RECT CornerBounds(int size) {
    const auto monitor = platform::PrimaryMonitor();
    return RECT{monitor.workArea.right - size - 8, monitor.workArea.top + 8, monitor.workArea.right - 8,
                monitor.workArea.top + 8 + size};
}

struct Harness {
    platform::ComApartment com{COINIT_APARTMENTTHREADED};
    platform::MessageLoop loop;
    graphics::GraphicsDevice device;
    graphics::FrameScheduler scheduler;
    Harness() {
        REQUIRE(device.Initialize().ok());
        REQUIRE(scheduler.Initialize(loop).ok());
    }
    ~Harness() {
        scheduler.Shutdown();
        device.Shutdown();
    }
};

}  // namespace

AETHER_TEST("ui transitions: an animated hide keeps the window shown until the compositor completes it") {
    Harness harness;
    Probe window(harness.device, harness.scheduler);
    ui::SurfaceWindowOptions options;
    options.className = L"AetherShell.Tests.TransitionProbe";
    options.title = L"Transition probe";
    REQUIRE(window.Create(options, CornerBounds(48)).ok());

    window.ShowAnimated(Timed(0.15, 0.0f, 1.0f, 8.0f, 0.0f));
    CHECK(window.IsShown());
    CHECK(::IsWindowVisible(window.Handle()) != FALSE);
    CHECK(window.IsTransitioning());
    const auto shown = RunUntil(harness.loop, [&] { return !window.IsTransitioning(); }, 1500);
    TEST_NOTE("show transition completed after " + std::to_string(shown) + " ms");
    CHECK_FALSE(window.IsTransitioning());
    CHECK(window.IsShown());

    window.HideAnimated(Timed(0.25, 1.0f, 0.0f, 0.0f, 8.0f));
    CHECK(window.IsShown());
    CHECK(window.IsHiding());
    CHECK(::IsWindowVisible(window.Handle()) != FALSE);
    const auto hidden = RunUntil(harness.loop, [&] { return !window.IsShown(); }, 1500);
    TEST_NOTE("hide transition completed after " + std::to_string(hidden) + " ms");
    CHECK_FALSE(window.IsShown());
    CHECK_FALSE(window.IsHiding());
    CHECK(::IsWindowVisible(window.Handle()) == FALSE);
    CHECK(hidden >= 150);  // not hidden before the transition could run
    CHECK(hidden <= 600);
}

AETHER_TEST("ui regions: visual margins are opt-in and the default region behavior is preserved") {
    Harness harness;
    RegionProbe window(harness.device, harness.scheduler);
    ui::SurfaceWindowOptions options;
    options.className = L"AetherShell.Tests.VisualRegionProbe";
    REQUIRE(window.Create(options, CornerBounds(48)).ok());
    const RECT input{10, 10, 30, 30};
    window.SetInputRegionPx(std::span<const RECT>(&input, 1));
    platform::UniqueRegion region(::CreateRectRgn(0, 0, 0, 0));
    REQUIRE(region.Get() != nullptr);
    REQUIRE(::GetWindowRgn(window.Handle(), region.Get()) != ERROR);
    CHECK(::PtInRegion(region.Get(), 5, 15) == FALSE);
    CHECK(::PtInRegion(region.Get(), 15, 15) != FALSE);
    const RECT visual{0, 0, 40, 40};
    window.SetVisualRegionPx(std::span<const RECT>(&visual, 1));
    REQUIRE(::GetWindowRgn(window.Handle(), region.Get()) != ERROR);
    CHECK(::PtInRegion(region.Get(), 5, 15) != FALSE);
    const auto hit = [&](POINT point) {
        REQUIRE(::ClientToScreen(window.Handle(), &point));
        return ::SendMessageW(window.Handle(), WM_NCHITTEST, 0, MAKELPARAM(point.x, point.y));
    };
    CHECK_EQ(hit(POINT{5, 15}), static_cast<LRESULT>(HTTRANSPARENT));
    CHECK_EQ(hit(POINT{15, 15}), static_cast<LRESULT>(HTCLIENT));
    window.SetVisualRegionPx({});
    REQUIRE(::GetWindowRgn(window.Handle(), region.Get()) != ERROR);
    CHECK(::PtInRegion(region.Get(), 5, 15) == FALSE);
    CHECK_FALSE(window.IsShown());
}

AETHER_TEST("ui transitions: instant transitions act synchronously and a hidden window hides at once") {
    Harness harness;
    Probe window(harness.device, harness.scheduler);
    ui::SurfaceWindowOptions options;
    options.className = L"AetherShell.Tests.TransitionProbe";
    options.title = L"Transition probe";
    REQUIRE(window.Create(options, CornerBounds(48)).ok());

    // Hiding a window that is not shown never waits for anything.
    window.HideAnimated(Timed(0.25, 1.0f, 0.0f, 0.0f, 8.0f));
    CHECK_FALSE(window.IsShown());
    CHECK_FALSE(window.IsHiding());

    const auto tokens = graphics::MakeDesignTokens(graphics::ThemeMode::Dark);
    graphics::MotionPolicy reduced;
    reduced.reduced = true;
    window.ShowAnimated(graphics::MakeEnterTransition({0.0f, 24.0f}, tokens, reduced));
    CHECK(window.IsShown());
    CHECK_FALSE(window.IsTransitioning());
    window.HideAnimated(graphics::MakeExitTransition({0.0f, 24.0f}, tokens, reduced));
    CHECK_FALSE(window.IsShown());
    CHECK(::IsWindowVisible(window.Handle()) == FALSE);

    // High contrast is instant as well, whatever the motion policy.
    const auto contrast = graphics::MakeDesignTokens(graphics::ThemeMode::HighContrast);
    window.ShowAnimated(graphics::MakeEnterTransition({}, contrast, graphics::MotionPolicy{}));
    CHECK(window.IsShown());
    window.HideAnimated(graphics::MakeExitTransition({}, contrast, graphics::MotionPolicy{}));
    CHECK_FALSE(window.IsShown());
}

AETHER_TEST("ui transitions: showing during an animated hide cancels the hide") {
    Harness harness;
    Probe window(harness.device, harness.scheduler);
    ui::SurfaceWindowOptions options;
    options.className = L"AetherShell.Tests.TransitionProbe";
    options.title = L"Transition probe";
    REQUIRE(window.Create(options, CornerBounds(48)).ok());

    window.Show(false);
    window.HideAnimated(Timed(0.25, 1.0f, 0.0f, 0.0f, 8.0f));
    REQUIRE(window.IsHiding());
    RunFor(harness.loop, 60);
    window.ShowAnimated(Timed(0.1, 0.0f, 1.0f, 8.0f, 0.0f));
    CHECK_FALSE(window.IsHiding());
    RunFor(harness.loop, 500);  // well past the superseded hide's end
    CHECK(window.IsShown());
    CHECK(::IsWindowVisible(window.Handle()) != FALSE);
    CHECK_FALSE(window.IsTransitioning());

    // A plain Show() during a hide cancels it too, and Hide() during a show hides at once.
    window.HideAnimated(Timed(0.25, 1.0f, 0.0f, 0.0f, 8.0f));
    window.Show(false);
    RunFor(harness.loop, 400);
    CHECK(window.IsShown());
    window.ShowAnimated(Timed(0.3, 0.0f, 1.0f, 8.0f, 0.0f));
    window.Hide();
    CHECK_FALSE(window.IsShown());
    CHECK_FALSE(window.IsTransitioning());
    RunFor(harness.loop, 400);
    CHECK_FALSE(window.IsShown());
}

AETHER_TEST("ui tooltip: fades out, then stays warm for the warm window and switches at once") {
    Harness harness;
    ui::TooltipWindow tooltip(harness.device, harness.scheduler);
    REQUIRE(tooltip.Initialize().ok());
    const auto tokens = graphics::MakeDesignTokens(graphics::ThemeMode::Dark);
    tooltip.SetTheme(tokens, graphics::MotionPolicy{});
    const RECT corner = CornerBounds(40);

    CHECK_FALSE(tooltip.IsWarm(graphics::FrameScheduler::Now()));
    tooltip.ShowFor(L"Aether Shell test tooltip", corner, ui::TooltipPlacement::Below);
    CHECK(tooltip.IsShown());
    CHECK(tooltip.Text() == L"Aether Shell test tooltip");
    CHECK(tooltip.IsWarm(graphics::FrameScheduler::Now()));
    // The bubble slides in and settles.
    const auto settled = RunUntil(harness.loop, [&] { return !tooltip.IsAnimating(graphics::FrameScheduler::Now()); }, 1000);
    CHECK(settled <= 600);
    // The tooltip never takes the pointer.
    CHECK_EQ(::SendMessageW(tooltip.Handle(), WM_NCHITTEST, 0, 0), static_cast<LRESULT>(HTTRANSPARENT));

    tooltip.HideTooltip();
    const double hiddenAt = graphics::FrameScheduler::Now();
    CHECK(tooltip.IsShown());  // fading out
    const auto faded = RunUntil(harness.loop, [&] { return !tooltip.IsShown(); }, 1000);
    CHECK_FALSE(tooltip.IsShown());
    CHECK(faded <= 500);
    CHECK(tooltip.IsWarm(hiddenAt + 0.1));
    CHECK_FALSE(tooltip.IsWarm(hiddenAt + tokens.tooltipWarmWindow + 0.05));

    // Within the warm window the next tooltip shows without the slide: it settles after a micro fade.
    tooltip.ShowFor(L"Next item", corner, ui::TooltipPlacement::Below);
    CHECK(tooltip.IsShown());
    const auto warmSettle = RunUntil(harness.loop, [&] { return !tooltip.IsAnimating(graphics::FrameScheduler::Now()); }, 1000);
    CHECK(warmSettle <= 400);

    // Reduced motion hides at once.
    graphics::MotionPolicy reduced;
    reduced.reduced = true;
    tooltip.SetTheme(tokens, reduced);
    tooltip.HideTooltip();
    CHECK_FALSE(tooltip.IsShown());
    tooltip.Destroy();
}

AETHER_TEST("ui tooltip: a policy change finishes an entrance or exit synchronously") {
    Harness harness;
    ui::TooltipWindow tooltip(harness.device, harness.scheduler);
    REQUIRE(tooltip.Initialize().ok());
    const auto tokens = graphics::MakeDesignTokens(graphics::ThemeMode::Dark);
    tooltip.SetTheme(tokens, graphics::MotionPolicy{});
    tooltip.ShowFor(L"Policy test tooltip", CornerBounds(40), ui::TooltipPlacement::Below);
    REQUIRE(tooltip.IsAnimating(graphics::FrameScheduler::Now()));
    graphics::MotionPolicy reduced;
    reduced.reduced = true;
    tooltip.SetTheme(tokens, reduced);
    CHECK_FALSE(tooltip.IsAnimating(graphics::FrameScheduler::Now()));
    CHECK(tooltip.IsShown());
    tooltip.HideTooltip();
    CHECK_FALSE(tooltip.IsShown());

    tooltip.SetTheme(tokens, graphics::MotionPolicy{});
    tooltip.ShowFor(L"Policy test tooltip", CornerBounds(40), ui::TooltipPlacement::Below);
    tooltip.HideTooltip();
    REQUIRE(tooltip.IsShown());
    tooltip.SetTheme(graphics::MakeDesignTokens(graphics::ThemeMode::HighContrast), graphics::MotionPolicy{});
    CHECK_FALSE(tooltip.IsAnimating(graphics::FrameScheduler::Now()));
    CHECK_FALSE(tooltip.IsShown());
}

AETHER_TEST("ui transitions: batch completion survives cancellation and contains callback exceptions") {
    Harness harness;
    struct OwnWindow {
        HWND hwnd = ::CreateWindowExW(WS_EX_NOREDIRECTIONBITMAP | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
            L"STATIC", L"Aether transition test", WS_POPUP, 0, 0, 1, 1, nullptr, nullptr, ::GetModuleHandleW(nullptr), nullptr);
        ~OwnWindow() { if (hwnd) ::DestroyWindow(hwnd); }
    } window;
    REQUIRE(window.hwnd != nullptr);  // never shown and never receives real input
    graphics::CompositionSurface surface;
    REQUIRE(surface.Attach(harness.device, window.hwnd).ok());
    int completions = 0;
    REQUIRE(surface.Animate(Timed(0.05, 0.0f, 1.0f, 0.0f, 0.0f), 1.0f, [&] { ++completions; }).ok());
    RunFor(harness.loop, 250);
    CHECK_EQ(completions, 1);
    CHECK_FALSE(surface.IsTransitioning());

    REQUIRE(surface.Animate(Timed(0.1, 0.0f, 1.0f, 0.0f, 0.0f), 1.0f, [&] { ++completions; }).ok());
    surface.ResetTransition();
    RunFor(harness.loop, 250);
    CHECK_EQ(completions, 1);

    REQUIRE(surface.Animate(Timed(0.05, 0.0f, 1.0f, 0.0f, 0.0f), 1.0f, [&] {
        ++completions;
        throw std::runtime_error("synthetic callback failure");
    }).ok());
    RunFor(harness.loop, 250);
    CHECK_EQ(completions, 2);
    CHECK_FALSE(surface.IsTransitioning());
    CHECK_FALSE(surface.Animate(Timed(0.0, 0.0f, 1.0f, 0.0f, 0.0f), 1.0f, [] {
        throw std::runtime_error("synthetic immediate callback failure");
    }).ok());

    REQUIRE(surface.Animate(Timed(0.1, 0.0f, 1.0f, 0.0f, 0.0f), 1.0f, [&] { ++completions; }).ok());
    surface.Detach();
    RunFor(harness.loop, 250);
    CHECK_EQ(completions, 2);
    auto destroyed = std::make_unique<graphics::CompositionSurface>();
    REQUIRE(destroyed->Attach(harness.device, window.hwnd).ok());
    REQUIRE(destroyed->Animate(Timed(0.1, 0.0f, 1.0f, 0.0f, 0.0f), 1.0f, [&] { ++completions; }).ok());
    destroyed.reset();
    RunFor(harness.loop, 250);
    CHECK_EQ(completions, 2);
}

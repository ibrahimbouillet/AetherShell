// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
//
// Search window in the design language: a fixed window whose panel animates its height, no frames
// while idle, and caret placement through the drawn DirectWrite layout. Uses this test's own Search
// window with a fake catalog, a fake launcher and a private placement area; messages are sent only to
// that window. Nothing is captured and no application is launched.
#include "AetherTest.h"
#include "FakeTouch.h"

#include "graphics/TextCache.h"
#include "platform/windows/ComApartment.h"
#include "platform/windows/MonitorUtilities.h"
#include "platform/windows/Handles.h"
#include "search/ui/SearchWindow.h"

#include <chrono>
#include <cmath>
#include <thread>

using namespace aether;
using namespace std::chrono_literals;

namespace {

class Launcher final : public apps::IApplicationLauncher {
public:
    int launches = 0;
    void LaunchAsync(const apps::ApplicationInfo&, std::function<void(Result<void>)> completion) override {
        ++launches;
        completion(Ok());
    }
    Result<void> Activate(const apps::RunningApplication&) override { return Ok(); }
};

class Icons final : public apps::IIconProvider {
public:
    void RequestIcon(apps::IconRequest request, Callback callback) override { callback(request.key, nullptr); }
    void CancelAll() noexcept override {}
};

void RunFor(platform::MessageLoop& loop, DWORD milliseconds) {
    HANDLE timer = ::CreateWaitableTimerW(nullptr, TRUE, nullptr);
    REQUIRE(timer != nullptr);
    LARGE_INTEGER due{};
    due.QuadPart = -static_cast<LONGLONG>(milliseconds) * 10'000;
    REQUIRE(::SetWaitableTimer(timer, &due, 0, nullptr, nullptr, FALSE));
    const auto token = loop.AddWaitHandle(timer, [] { ::PostQuitMessage(0); });
    loop.Run();
    loop.RemoveWaitHandle(token);
    ::CloseHandle(timer);
}

struct Fixture {
    platform::ComApartment apartment;
    ManualDispatcher dispatcher;
    search::SearchService service{dispatcher};
    graphics::GraphicsDevice device;
    platform::MessageLoop loop;
    graphics::FrameScheduler scheduler;
    Launcher launcher;
    Icons icons;
    std::unique_ptr<search::ui::SearchWindow> window;

    Fixture() {
        REQUIRE(SUCCEEDED(apartment.Status()));
        std::vector<apps::ApplicationInfo> apps;
        for (int i = 0; i < 30; ++i) {
            apps::ApplicationInfo app;
            app.id = L"test:" + std::to_wstring(i);
            app.displayName = L"Test Application " + std::to_wstring(i);
            apps.push_back(std::move(app));
        }
        apps::ApplicationInfo single;
        single.id = L"test:unique";
        single.displayName = L"Zebra Notes";
        apps.push_back(std::move(single));
        REQUIRE(service.Rebuild(std::move(apps)).ok());
        const auto deadline = std::chrono::steady_clock::now() + 5s;
        while (service.Current().state == search::IndexState::Building && std::chrono::steady_clock::now() < deadline) {
            dispatcher.RunPending();
            std::this_thread::sleep_for(1ms);
        }
        REQUIRE_EQ(service.Current().state, search::IndexState::Ready);
        REQUIRE(device.Initialize().ok());
        REQUIRE(scheduler.Initialize(loop).ok());
        window = std::make_unique<search::ui::SearchWindow>(device, scheduler, service, launcher, icons);
        REQUIRE(window->Initialize().ok());
        const auto monitor = platform::PrimaryMonitor();
        window->SetPlacementArea(RECT{monitor.workArea.left + 40, monitor.workArea.top + 40, monitor.workArea.left + 840,
                                      monitor.workArea.top + 760});
    }
    ~Fixture() {
        window.reset();
        scheduler.Shutdown();
    }
    void Query(const wchar_t* query) {
        ::SetWindowTextW(window->EditHandle(), query);
        RunFor(loop, 110);
    }
};

float Height(const D2D1_RECT_F& rect) { return rect.bottom - rect.top; }

}  // namespace

AETHER_TEST("search UI: the panel animates its height while the window keeps its size, then rendering stops") {
    Fixture fixture;
    auto& window = *fixture.window;
    window.SetTheme(graphics::MakeDesignTokens(graphics::ThemeMode::Dark), graphics::MotionPolicy{});
    window.Open();
    fixture.Query(L"Test Application");
    RunFor(fixture.loop, 500);
    REQUIRE(window.IsOpen());
    const RECT bounds = window.BoundsPx();
    const float tall = Height(window.PanelDip());

    // One result: the panel shrinks with the slide spring, the window does not move or resize.
    const auto resizeStarted = std::chrono::steady_clock::now();
    ::SetWindowTextW(window.EditHandle(), L"Zebra");
    ::SendMessageW(window.EditHandle(), WM_KEYDOWN, VK_HOME, 0);  // flush the query now
    REQUIRE_EQ(window.Results().size(), 1u);
    RunFor(fixture.loop, 40);
    const float midway = Height(window.PanelDip());
    RunFor(fixture.loop, 700);
    const float settled = Height(window.PanelDip());
    TEST_NOTE("panel heights: tall=" + std::to_string(tall) + " midway=" + std::to_string(midway) +
              " settled=" + std::to_string(settled));
    CHECK(settled < tall);
    CHECK(midway <= tall + 0.5f);
    CHECK(midway > settled - 0.5f);
    const RECT after = window.BoundsPx();
    CHECK(::EqualRect(&bounds, &after) != FALSE);

    // Spring settlement depends on the remaining velocity, not just on an almost-equal height.
    // Keep the idle assertion strict, but establish its precondition under a bounded deadline.
    const auto settleDeadline = resizeStarted + 2s;
    while (window.IsLayoutAnimating() && std::chrono::steady_clock::now() < settleDeadline) RunFor(fixture.loop, 20);
    REQUIRE_FALSE(window.IsLayoutAnimating());
    // Idle: only the caret blink may render (at most one blink in this window of time).
    const auto frames = window.RenderedFrames();
    RunFor(fixture.loop, 300);
    CHECK(window.RenderedFrames() - frames <= 2u);
    window.Close();
    RunFor(fixture.loop, 300);
    CHECK_FALSE(window.IsShown());
}

AETHER_TEST("search UI: clicking the field places the caret at the drawn glyph boundary") {
    Fixture fixture;
    auto& window = *fixture.window;
    const auto tokens = graphics::MakeDesignTokens(graphics::ThemeMode::Light);
    graphics::MotionPolicy reduced;
    reduced.reduced = true;
    window.SetTheme(tokens, reduced);
    window.Open();
    ::SetWindowTextW(window.EditHandle(), L"WWWWWW");
    RunFor(fixture.loop, 120);

    graphics::TextCache text;
    text.Initialize(fixture.device.DWriteFactory(), tokens.fontFamily, tokens.displayFontFamily);
    graphics::TextStyle style;
    style.size = 16.0f;  // the field's font size at text scale 1
    style.ellipsis = false;
    const float two = text.Measure(L"WW", style).width;
    const float four = text.Measure(L"WWWW", style).width;
    REQUIRE(two > 0.0f);
    const auto input = window.InputDip();
    const float scale = static_cast<float>(window.Dpi()) / 96.0f;
    const auto click = [&](float xDip) {
        const float yDip = (input.top + input.bottom) / 2.0f;
        const LPARAM point = MAKELPARAM(static_cast<int>(std::lround(xDip * scale)), static_cast<int>(std::lround(yDip * scale)));
        ::SendMessageW(window.Handle(), WM_LBUTTONDOWN, MK_LBUTTON, point);
        ::SendMessageW(window.Handle(), WM_LBUTTONUP, 0, point);
        DWORD start = 0;
        DWORD end = 0;
        ::SendMessageW(window.EditHandle(), EM_GETSEL, reinterpret_cast<WPARAM>(&start), reinterpret_cast<LPARAM>(&end));
        CHECK_EQ(start, end);  // a click places a caret, it selects nothing
        return end;
    };
    CHECK_EQ(click(input.left + 1.0f), 0u);
    CHECK_EQ(click(input.left + two + 2.0f), 2u);           // just inside the third glyph's leading half
    CHECK_EQ(click(input.left + four - 2.0f), 4u);          // in the fourth glyph's trailing half
    CHECK_EQ(click(input.right - 4.0f), 6u);                // beyond the text: the end
    CHECK_EQ(fixture.launcher.launches, 0);                 // clicks in the field never launch
    window.Close();
    CHECK_FALSE(window.IsShown());
}

AETHER_TEST("search UI: drawing preserves the shadow while unused window space stays outside its region") {
    Fixture fixture;
    auto& window = *fixture.window;
    graphics::MotionPolicy reduced;
    reduced.reduced = true;
    auto tokens = graphics::MakeDesignTokens(graphics::ThemeMode::Dark);
    window.SetTheme(tokens, reduced);
    window.Open();
    fixture.Query(L"Zebra");
    const auto panel = window.PanelDip();
    platform::UniqueRegion region(::CreateRectRgn(0, 0, 0, 0));
    REQUIRE(region.Get() != nullptr);
    REQUIRE(::GetWindowRgn(window.Handle(), region.Get()) != ERROR);
    // The old region was exactly the panel rectangle and removed all exterior shadow pixels.
    const int shadowX = static_cast<int>(std::lround(window.ToPx(panel.left - 10.0f)));
    const int shadowY = static_cast<int>(std::lround(window.ToPx(panel.top + 30.0f)));
    CHECK(::PtInRegion(region.Get(), shadowX, shadowY) != FALSE);
    const auto bounds = window.BoundsPx();
    CHECK(::PtInRegion(region.Get(), (bounds.right - bounds.left) / 2, bounds.bottom - bounds.top - 2) == FALSE);
    POINT shadowPoint{shadowX, shadowY};
    REQUIRE(::ClientToScreen(window.Handle(), &shadowPoint));
    CHECK_EQ(::SendMessageW(window.Handle(), WM_NCHITTEST, 0, MAKELPARAM(shadowPoint.x, shadowPoint.y)),
             static_cast<LRESULT>(HTTRANSPARENT));
    window.Close();
}

AETHER_TEST("search UI: expanding rows cannot be clicked or exposed through UIA under the current footer") {
    Fixture fixture;
    auto& window = *fixture.window;
    auto tokens = graphics::MakeDesignTokens(graphics::ThemeMode::Dark);
    graphics::MotionPolicy reduced;
    reduced.reduced = true;
    window.SetTheme(tokens, reduced);
    window.Open();
    fixture.Query(L"Zebra");
    REQUIRE_EQ(window.Results().size(), 1u);
    window.SetTheme(tokens, graphics::MotionPolicy{});
    ::SetWindowTextW(window.EditHandle(), L"Test Application");
    ::SendMessageW(window.EditHandle(), WM_KEYDOWN, VK_HOME, 0);  // start expansion without pumping a frame
    REQUIRE(window.Results().size() > 1u);
    const auto panel = window.PanelDip();
    const auto hidden = window.Item(1);
    CHECK(hidden.isOffscreen);
    CHECK_EQ(hidden.boundsPx.left, 0L);
    CHECK_EQ(hidden.boundsPx.bottom, 0L);
    const auto footer = MAKELPARAM(static_cast<int>(std::lround(window.ToPx(panel.left + 100.0f))),
                                  static_cast<int>(std::lround(window.ToPx(panel.bottom - 18.0f))));
    ::SendMessageW(window.Handle(), WM_LBUTTONUP, 0, footer);
    CHECK_EQ(fixture.launcher.launches, 0);
    CHECK(window.IsOpen());
    RunFor(fixture.loop, 700);
    CHECK_FALSE(window.Item(1).isOffscreen);
    window.Close();
    RunFor(fixture.loop, 300);
}

AETHER_TEST("search UI: a policy change cancels an entrance immediately") {
    Fixture fixture;
    auto& window = *fixture.window;
    const auto tokens = graphics::MakeDesignTokens(graphics::ThemeMode::Dark);
    window.SetTheme(tokens, graphics::MotionPolicy{});
    ::SetWindowTextW(window.EditHandle(), L"Test");
    window.Open();
    REQUIRE(window.IsTransitioning());
    ::SendMessageW(window.EditHandle(), EM_SETSEL, 4, 4);
    const auto input = window.InputDip();
    const auto fieldPoint = MAKELPARAM(static_cast<int>(std::lround(window.ToPx(input.left + 1.0f))),
        static_cast<int>(std::lround(window.ToPx((input.top + input.bottom) / 2.0f))));
    ::SendMessageW(window.Handle(), WM_LBUTTONDOWN, MK_LBUTTON, fieldPoint);
    ::SendMessageW(window.Handle(), WM_LBUTTONUP, 0, fieldPoint);
    DWORD caret = 0;
    ::SendMessageW(window.EditHandle(), EM_GETSEL, 0, reinterpret_cast<LPARAM>(&caret));
    CHECK_EQ(caret, 4u);  // clicking moving text cannot place the caret at a different resting glyph
    const auto row = window.Item(0).boundsPx;
    const auto bounds = window.BoundsPx();
    ::SendMessageW(window.Handle(), WM_LBUTTONUP, 0, MAKELPARAM((row.left + row.right) / 2 - bounds.left,
        (row.top + row.bottom) / 2 - bounds.top));
    CHECK_EQ(fixture.launcher.launches, 0);  // moving rows cannot launch the wrong application
    CHECK(window.IsOpen());
    graphics::MotionPolicy reduced;
    reduced.reduced = true;
    window.SetTheme(tokens, reduced);
    CHECK_FALSE(window.IsTransitioning());
    CHECK(window.IsOpen());
    window.Close();
    CHECK_FALSE(window.IsShown());

    window.SetTheme(tokens, graphics::MotionPolicy{});
    window.Open();
    REQUIRE(window.IsTransitioning());
    window.SetTheme(graphics::MakeDesignTokens(graphics::ThemeMode::HighContrast), graphics::MotionPolicy{});
    CHECK_FALSE(window.IsTransitioning());
    CHECK(window.IsOpen());
    window.Close();
    CHECK_FALSE(window.IsShown());
}

AETHER_TEST("search UI: a release launches only the row its press started on (AS4-12)") {
    Fixture fixture;
    auto& window = *fixture.window;
    graphics::MotionPolicy reduced; reduced.reduced = true;
    window.SetTheme(graphics::MakeDesignTokens(graphics::ThemeMode::Dark), reduced);
    window.Open();
    fixture.Query(L"Test Application");
    REQUIRE(window.Results().size() > 3u);
    const auto bounds = window.BoundsPx();
    const auto rowPoint = [&](int row) {
        const auto rect = window.Item(static_cast<std::size_t>(row)).boundsPx;
        return MAKELPARAM((rect.left + rect.right) / 2 - bounds.left, (rect.top + rect.bottom) / 2 - bounds.top);
    };
    ::SendMessageW(window.Handle(), WM_LBUTTONDOWN, MK_LBUTTON, rowPoint(0));
    ::SendMessageW(window.Handle(), WM_LBUTTONUP, 0, rowPoint(2));
    CHECK_EQ(fixture.launcher.launches, 0);
    ::SendMessageW(window.Handle(), WM_LBUTTONUP, 0, rowPoint(2));  // a release with no press
    CHECK_EQ(fixture.launcher.launches, 0);
    ::SendMessageW(window.Handle(), WM_LBUTTONDOWN, MK_LBUTTON, rowPoint(1));
    ::SendMessageW(window.Handle(), WM_CAPTURECHANGED, 0, 0);
    ::SendMessageW(window.Handle(), WM_LBUTTONUP, 0, rowPoint(1));
    CHECK_EQ(fixture.launcher.launches, 0);
    ::SendMessageW(window.Handle(), WM_LBUTTONDOWN, MK_LBUTTON, rowPoint(1));
    ::SendMessageW(window.Handle(), WM_LBUTTONUP, 0, rowPoint(1));
    CHECK_EQ(fixture.launcher.launches, 1);
}

AETHER_TEST("search UI: equal wheel totals move the selection equally far (AS4-15)") {
    Fixture fixture;
    auto& window = *fixture.window;
    graphics::MotionPolicy reduced; reduced.reduced = true;
    window.SetTheme(graphics::MakeDesignTokens(graphics::ThemeMode::Dark), reduced);
    window.Open();
    fixture.Query(L"Test Application");
    REQUIRE(window.Results().size() > 5u);
    const auto wheel = [&](int delta) { ::SendMessageW(window.Handle(), WM_MOUSEWHEEL, MAKEWPARAM(0, static_cast<WORD>(static_cast<SHORT>(delta))), 0); };
    const auto selected = [&] { return window.SelectedIndex(); };
    // From the first row, wherever the real pointer happens to rest: a row under it is selected on opening.
    wheel(8 * WHEEL_DELTA);
    REQUIRE_EQ(selected(), 0u);
    wheel(-WHEEL_DELTA);
    CHECK_EQ(selected(), 1u);
    for (int i = 0; i < 12; ++i) wheel(-10);  // one notch in twelve precision packets: one row, not twelve
    CHECK_EQ(selected(), 2u);
    wheel(-3 * WHEEL_DELTA);                  // three notches in one packet: three rows
    CHECK_EQ(selected(), 5u);
    for (int i = 0; i < 24; ++i) wheel(10);   // reversed
    CHECK_EQ(selected(), 3u);
    wheel(0);
    CHECK_EQ(selected(), 3u);
}

AETHER_TEST("search UI: touch pans the rows, a tap launches its own row, a pan or cancel launches nothing (FR-040)") {
    Fixture fixture;
    auto& window = *fixture.window;
    graphics::MotionPolicy reduced; reduced.reduced = true;
    window.SetTheme(graphics::MakeDesignTokens(graphics::ThemeMode::Dark), reduced);
    // Results are capped at eight: a short placement area is what makes them overflow and scroll.
    const auto monitor = platform::PrimaryMonitor();
    window.SetPlacementArea(RECT{monitor.workArea.left + 40, monitor.workArea.top + 40, monitor.workArea.left + 840, monitor.workArea.top + 40 + static_cast<LONG>(window.ToPx(430.0f))});
    window.Open();
    fixture.Query(L"Test Application");
    REQUIRE_EQ(window.Results().size(), 8u);
    REQUIRE(window.Item(7).isOffscreen);
    REQUIRE_FALSE(window.Item(1).isOffscreen);
    test::FakeTouch touch;
    const HWND hwnd = window.Handle();
    const auto center = [&](std::size_t row) {
        const auto rect = window.Item(row).boundsPx;
        return POINT{(rect.left + rect.right) / 2, (rect.top + rect.bottom) / 2};
    };
    const POINT from = center(1);
    const int rowPx = window.Item(1).boundsPx.top - window.Item(0).boundsPx.top;
    REQUIRE(rowPx > 0);
    // A pan by two rows: the list follows the finger, and nothing launches.
    touch.Send(hwnd, WM_POINTERDOWN, 1, from);
    touch.Send(hwnd, WM_POINTERUPDATE, 1, POINT{from.x, from.y - 20});
    touch.Send(hwnd, WM_POINTERUPDATE, 1, POINT{from.x, from.y - 20 - 2 * rowPx});
    CHECK(window.Item(0).isOffscreen);
    CHECK(window.Item(1).isOffscreen);
    touch.Send(hwnd, WM_POINTERUP, 1, POINT{from.x, from.y - 20 - 2 * rowPx});
    CHECK_EQ(fixture.launcher.launches, 0);
    // The rows reach their last entry by touch, without launching.
    for (int i = 0; i < 6; ++i) {
        touch.Send(hwnd, WM_POINTERDOWN, 1, from);
        touch.Send(hwnd, WM_POINTERUPDATE, 1, POINT{from.x, from.y - 20});
        touch.Send(hwnd, WM_POINTERUPDATE, 1, POINT{from.x, from.y - 20 - 2 * rowPx});
        touch.Send(hwnd, WM_POINTERUP, 1, POINT{from.x, from.y - 20 - 2 * rowPx});
    }
    CHECK_FALSE(window.Item(window.Results().size() - 1).isOffscreen);
    CHECK_EQ(fixture.launcher.launches, 0);
    // A second finger or a canceled contact launches nothing.
    const POINT visible = center(window.Results().size() - 1);
    touch.Send(hwnd, WM_POINTERDOWN, 1, visible);
    touch.Send(hwnd, WM_POINTERDOWN, 2, POINT{visible.x, visible.y - rowPx});
    touch.Send(hwnd, WM_POINTERUP, 2, POINT{visible.x, visible.y - rowPx});
    touch.Send(hwnd, WM_POINTERUP, 1, visible);
    touch.Send(hwnd, WM_POINTERDOWN, 1, visible);
    touch.Send(hwnd, WM_POINTERUP, 1, visible, /*canceled*/ true);
    CHECK_EQ(fixture.launcher.launches, 0);
    // A tap launches once.
    touch.Send(hwnd, WM_POINTERDOWN, 1, visible);
    touch.Send(hwnd, WM_POINTERUP, 1, visible);
    CHECK_EQ(fixture.launcher.launches, 1);
}

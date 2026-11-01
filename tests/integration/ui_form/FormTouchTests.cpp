// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
//
// FR-040 on the shared form scroll view, with this test's own window and synthetic contacts through
// the real router (FakeTouch.h). Physical touch, Windows gestures and real input timing are not
// exercised here. Rows hold auto-checkboxes: their check state counts clicks without any callback.
#include "AetherTest.h"
#include "FakeTouch.h"
#include "FormTestSupport.h"

#include "graphics/FrameScheduler.h"
#include "graphics/Theme.h"
#include "platform/windows/ComApartment.h"
#include "platform/windows/MessageLoop.h"
#include "ui/form/FormScrollView.h"
#include "ui/form/FormSurface.h"

#include <commctrl.h>

#include <chrono>
#include <cstdlib>
#include <functional>
#include <vector>

using namespace aether;
using namespace aether::ui::form;
using namespace aether::test::form;

namespace {

constexpr int kRows = 40;
constexpr float kRowPitch = 60.0f;
constexpr std::uint32_t kFinger = 7, kOtherFinger = 8;

struct TouchForm {
    platform::ComApartment com{COINIT_APARTMENTTHREADED};
    platform::MessageLoop loop;
    graphics::FrameScheduler scheduler;
    FormHost host;
    FormScrollView scroll;
    std::vector<HWND> checks;
    HWND field = nullptr, slider = nullptr;
    test::FakeTouch touch;

    explicit TouchForm(graphics::MotionPolicy motion = {}) {
        REQUIRE(scheduler.Initialize(loop).ok());
        INITCOMMONCONTROLSEX init{sizeof(init), ICC_STANDARD_CLASSES | ICC_BAR_CLASSES};
        REQUIRE(::InitCommonControlsEx(&init) != FALSE);
        REQUIRE(host.Create(520.0f, 400.0f, L"Aether touch probe"));
        REQUIRE(host.surface.Attach(host.hwnd, graphics::MakeDesignTokens(graphics::ThemeMode::Dark), motion, &scheduler).ok());
        REQUIRE(scroll.Create(host.surface, host.hwnd, 900).ok());
        scroll.SetFrame(host.PxRect(10.0f, 10.0f, 510.0f, 390.0f));
        scroll.SetContentHeight(host.Px(static_cast<float>(kRows) * kRowPitch));
        const HWND canvas = scroll.Canvas();
        for (int row = 0; row < kRows; ++row) {
            HWND check = host.Add(canvas, L"BUTTON", L"Option", WS_TABSTOP | BS_AUTOCHECKBOX, 4000 + row);
            ControlSpec spec; spec.role = ControlRole::Check;
            REQUIRE(host.surface.Register(check, spec).ok());
            const float top = static_cast<float>(row) * kRowPitch + 8.0f;
            host.surface.SetFrame(check, host.PxRect(20.0f, top, 240.0f, top + 32.0f));
            checks.push_back(check);
        }
        // Below the first screen, so the fixture's first pans need no editor masking frames.
        const float bottom = static_cast<float>(kRows - 1) * kRowPitch + 8.0f;
        field = host.Add(canvas, L"EDIT", L"text", WS_TABSTOP | ES_AUTOHSCROLL, 3000, WS_EX_CLIENTEDGE);
        ControlSpec fieldSpec; fieldSpec.role = ControlRole::Field;
        REQUIRE(host.surface.Register(field, fieldSpec).ok());
        host.surface.SetFrame(field, host.PxRect(260.0f, bottom, 480.0f, bottom + 32.0f));
        slider = host.Add(canvas, TRACKBAR_CLASSW, L"", WS_TABSTOP | TBS_HORZ | TBS_NOTICKS, 5000);
        ControlSpec sliderSpec; sliderSpec.role = ControlRole::Slider;
        REQUIRE(host.surface.Register(slider, sliderSpec).ok());
        host.surface.SetFrame(slider, host.PxRect(260.0f, 8.0f, 480.0f, 40.0f));
        host.Show();
        Pump([] { return false; }, 60);
    }
    ~TouchForm() { scroll.Destroy(); host.surface.Detach(); host.Destroy(); scheduler.Shutdown(); }

    bool Pump(const std::function<bool()>& done, DWORD limitMs) {
        HANDLE timer = ::CreateWaitableTimerW(nullptr, FALSE, nullptr);
        REQUIRE(timer != nullptr);
        LARGE_INTEGER due{}; due.QuadPart = -10'000;
        REQUIRE(::SetWaitableTimer(timer, &due, 4, nullptr, nullptr, FALSE) != FALSE);
        const auto id = loop.AddWaitHandle(timer, [] {});
        const auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(limitMs);
        loop.RunNested([&] { return done() || std::chrono::steady_clock::now() >= end; });
        loop.RemoveWaitHandle(id);
        ::CancelWaitableTimer(timer); ::CloseHandle(timer);
        return done();
    }
    [[nodiscard]] POINT Center(HWND control) const {
        RECT bounds{}; ::GetWindowRect(control, &bounds);
        return POINT{(bounds.left + bounds.right) / 2, (bounds.top + bounds.bottom) / 2};
    }
    // Motion next to native editors starts a frame after they are masked (FR-037 SF-01).
    void Settle() { (void)Pump([] { return false; }, 60); }
    bool Route(UINT message, HWND target, std::uint32_t id, POINT screen, bool canceled = false) {
        MSG routed{}; routed.hwnd = target; routed.message = message;
        routed.wParam = touch.Set(id, screen, canceled);
        return FormScrollView::RoutePointer(routed);
    }
    [[nodiscard]] bool Checked(int row) const { return ::SendMessageW(checks[static_cast<std::size_t>(row)], BM_GETCHECK, 0, 0) == BST_CHECKED; }
    [[nodiscard]] int CanvasOffset() const {
        RECT canvas{}, view{};
        ::GetWindowRect(scroll.Canvas(), &canvas); ::GetWindowRect(scroll.Viewport(), &view);
        return canvas.top - view.top;
    }
    // A pan with a rested release (no fling): the finger stops, waits, then lifts.
    void RestedPan(HWND target, POINT from, int deltaY) {
        REQUIRE(Route(WM_POINTERDOWN, target, kFinger, from));
        POINT at = from;
        at.y += deltaY > 0 ? 20 : -20;
        REQUIRE(Route(WM_POINTERUPDATE, target, kFinger, at));
        at.y += deltaY;
        REQUIRE(Route(WM_POINTERUPDATE, target, kFinger, at));
        touch.Advance(0.3);
        REQUIRE(Route(WM_POINTERUP, target, kFinger, at));
    }
};

}  // namespace

AETHER_TEST("form touch: a still tap clicks the control once; the content has not moved") {
    TouchForm form;
    const POINT at = form.Center(form.checks[1]);
    CHECK(form.Route(WM_POINTERDOWN, form.checks[1], kFinger, at));
    CHECK_FALSE(form.Checked(1));  // nothing is pressed before the gesture is known
    CHECK(form.Route(WM_POINTERUP, form.checks[1], kFinger, at));
    CHECK(form.Checked(1));
    CHECK_EQ(form.scroll.Position(), 0);
}

AETHER_TEST("form touch: the content follows the finger exactly and a pan never clicks") {
    TouchForm form;
    form.scroll.ScrollTo(form.host.Px(300.0f));
    const int start = form.scroll.Position();
    const auto row = 6;
    const POINT from = form.Center(form.checks[row]);
    REQUIRE(form.Route(WM_POINTERDOWN, form.checks[row], kFinger, from));
    REQUIRE(form.Route(WM_POINTERUPDATE, form.checks[row], kFinger, POINT{from.x, from.y - 20}));  // passes the slop
    form.Settle();
    CHECK_EQ(form.scroll.Position(), start);                                                       // no jump by the slop
    REQUIRE(form.Route(WM_POINTERUPDATE, form.checks[row], kFinger, POINT{from.x, from.y - 120}));
    form.Settle();
    CHECK_EQ(form.scroll.Position(), start + 100);
    REQUIRE(form.Route(WM_POINTERUPDATE, form.checks[row], kFinger, POINT{from.x, from.y - 70}));  // reverses directly
    form.Settle();
    CHECK_EQ(form.scroll.Position(), start + 50);
    form.touch.Advance(0.3);
    REQUIRE(form.Route(WM_POINTERUP, form.checks[row], kFinger, POINT{from.x, from.y - 70}));
    CHECK_FALSE(form.Checked(row));
    (void)form.Pump([] { return false; }, 60);
    CHECK_EQ(form.scroll.Position(), start + 50);  // a rested release does not fling
}

AETHER_TEST("form touch: an across move, a second finger, a canceled or a lost contact never click") {
    TouchForm form;
    const POINT at = form.Center(form.checks[2]);
    // Horizontal movement on a vertical scroller.
    REQUIRE(form.Route(WM_POINTERDOWN, form.checks[2], kFinger, at));
    REQUIRE(form.Route(WM_POINTERUPDATE, form.checks[2], kFinger, POINT{at.x + 40, at.y}));
    REQUIRE(form.Route(WM_POINTERUP, form.checks[2], kFinger, at));
    CHECK_FALSE(form.Checked(2)); CHECK_EQ(form.scroll.Position(), 0);
    // A second contact is consumed and turns the first into nothing.
    REQUIRE(form.Route(WM_POINTERDOWN, form.checks[2], kFinger, at));
    CHECK(form.Route(WM_POINTERDOWN, form.checks[3], kOtherFinger, form.Center(form.checks[3])));
    CHECK_FALSE(form.Route(WM_POINTERUP, form.checks[3], kOtherFinger, form.Center(form.checks[3])));
    REQUIRE(form.Route(WM_POINTERUP, form.checks[2], kFinger, at));
    CHECK_FALSE(form.Checked(2)); CHECK_FALSE(form.Checked(3));
    // Canceled by the system.
    REQUIRE(form.Route(WM_POINTERDOWN, form.checks[2], kFinger, at));
    REQUIRE(form.Route(WM_POINTERUP, form.checks[2], kFinger, at, /*canceled*/ true));
    CHECK_FALSE(form.Checked(2));
    // The contact vanishes without an up reaching this loop (capture moved, a system gesture).
    REQUIRE(form.Route(WM_POINTERDOWN, form.checks[2], kFinger, at));
    form.touch.Lift(kFinger);
    (void)form.Pump([] { return false; }, 180);
    CHECK_FALSE(form.Route(WM_POINTERUP, form.checks[2], kFinger, at));  // no longer tracked
    CHECK_FALSE(form.Checked(2));
    // After all of that an ordinary tap still works exactly once.
    REQUIRE(form.Route(WM_POINTERDOWN, form.checks[2], kFinger, at));
    REQUIRE(form.Route(WM_POINTERUP, form.checks[2], kFinger, at));
    CHECK(form.Checked(2));
}

AETHER_TEST("form touch: native editors, and mouse or pen pointers, keep their own input") {
    TouchForm form;
    CHECK_FALSE(form.Route(WM_POINTERDOWN, form.field, kFinger, form.Center(form.field)));
    CHECK_FALSE(form.Route(WM_POINTERUP, form.field, kFinger, form.Center(form.field)));
    MSG pen{}; pen.hwnd = form.checks[1]; pen.message = WM_POINTERDOWN;
    pen.wParam = form.touch.Set(kFinger, form.Center(form.checks[1]), false, /*touch*/ false);
    CHECK_FALSE(FormScrollView::RoutePointer(pen));
    MSG unrelated{}; unrelated.hwnd = form.host.hwnd; unrelated.message = WM_LBUTTONDOWN;
    CHECK_FALSE(FormScrollView::RoutePointer(unrelated));
}

AETHER_TEST("form touch: a release flings on and rests within range; a press catches it without clicking") {
    TouchForm form;
    const POINT from = form.Center(form.checks[3]);
    REQUIRE(form.Route(WM_POINTERDOWN, form.checks[3], kFinger, from));
    POINT at = from;
    for (int i = 0; i < 12; ++i) {  // 60 pixels every 16 ms: a brisk flick
        at.y -= 60;
        form.touch.Advance(0.016);
        REQUIRE(form.Route(WM_POINTERUPDATE, form.checks[3], kFinger, at));
    }
    form.Settle();
    const int released = form.scroll.Position();
    CHECK(released > 0);
    REQUIRE(form.Route(WM_POINTERUP, form.checks[3], kFinger, at));
    CHECK_FALSE(form.Checked(3));
    REQUIRE(form.Pump([&] { return form.scroll.Position() > released + form.host.Px(40.0f); }, 2000));  // it travels on
    const int moving = form.scroll.Position();
    CHECK(moving >= 0); CHECK(moving <= form.scroll.Range());
    // Press during the fling: it stops, and that press is not a tap on whatever passed under it.
    const int row = std::min(kRows - 1, (moving + form.host.Px(120.0f)) / form.host.Px(kRowPitch));
    const POINT caught = form.Center(form.checks[static_cast<std::size_t>(row)]);
    REQUIRE(form.Route(WM_POINTERDOWN, form.checks[static_cast<std::size_t>(row)], kFinger, caught));
    REQUIRE(form.Route(WM_POINTERUP, form.checks[static_cast<std::size_t>(row)], kFinger, caught));
    CHECK_FALSE(form.Checked(row));
    const int stopped = form.scroll.Position();
    (void)form.Pump([] { return false; }, 80);
    CHECK_EQ(form.scroll.Position(), stopped);
}

AETHER_TEST("form touch: pulling past an end is elastic and bounded, keeps the logical position valid and returns") {
    TouchForm form;
    const POINT from = form.Center(form.checks[1]);
    REQUIRE(form.Route(WM_POINTERDOWN, form.checks[1], kFinger, from));
    REQUIRE(form.Route(WM_POINTERUPDATE, form.checks[1], kFinger, POINT{from.x, from.y + 20}));
    REQUIRE(form.Route(WM_POINTERUPDATE, form.checks[1], kFinger, POINT{from.x, from.y + 300}));
    form.Settle();
    const int stretched = form.CanvasOffset();
    CHECK(stretched > 0);
    CHECK(stretched < 300);                     // resisted, not one to one
    CHECK(stretched <= form.host.Px(120.0f));   // and bounded
    CHECK_EQ(form.scroll.Position(), 0);        // the logical (UIA) offset never leaves the range
    form.touch.Advance(0.3);
    REQUIRE(form.Route(WM_POINTERUP, form.checks[1], kFinger, POINT{from.x, from.y + 300}));
    CHECK_FALSE(form.Checked(1));
    CHECK(form.Pump([&] { return form.CanvasOffset() == 0; }, 2000));
    CHECK_EQ(form.scroll.Position(), 0);
}

AETHER_TEST("form touch: every row of a long form can be reached by touch and tapped") {
    TouchForm form;
    int guard = 0;
    while (form.scroll.Position() < form.scroll.Range() && guard++ < 40) {
        const int row = std::min(kRows - 1, (form.scroll.Position() + form.host.Px(200.0f)) / form.host.Px(kRowPitch));
        const HWND target = form.checks[static_cast<std::size_t>(row)];
        form.RestedPan(target, form.Center(target), -form.host.Px(180.0f));
        (void)form.Pump([] { return false; }, 40);
    }
    CHECK_EQ(form.scroll.Position(), form.scroll.Range());
    (void)form.Pump([&] { return form.CanvasOffset() == -form.scroll.Range(); }, 2000);
    CHECK_EQ(form.CanvasOffset(), -form.scroll.Range());
    form.Settle();  // the spring's last frames: a press now would catch it, not tap
    for (int row = 0; row < kRows; ++row) CHECK_FALSE(form.Checked(row));  // none of those pans clicked
    const HWND last = form.checks.back();
    REQUIRE(form.Route(WM_POINTERDOWN, last, kFinger, form.Center(last)));
    REQUIRE(form.Route(WM_POINTERUP, last, kFinger, form.Center(last)));
    CHECK(form.Checked(kRows - 1));
}

AETHER_TEST("form touch: reduced motion tracks directly but has no fling or elastic displacement") {
    graphics::MotionPolicy reduced; reduced.reduced = true;
    TouchForm form(reduced);
    const POINT from = form.Center(form.checks[1]);
    REQUIRE(form.Route(WM_POINTERDOWN, form.checks[1], kFinger, from));
    REQUIRE(form.Route(WM_POINTERUPDATE, form.checks[1], kFinger, POINT{from.x, from.y + 20}));
    REQUIRE(form.Route(WM_POINTERUPDATE, form.checks[1], kFinger, POINT{from.x, from.y + 300}));
    form.Settle();
    CHECK_EQ(form.CanvasOffset(), 0);  // no displacement past the top
    REQUIRE(form.Route(WM_POINTERUP, form.checks[1], kFinger, POINT{from.x, from.y + 300}));
    REQUIRE(form.Route(WM_POINTERDOWN, form.checks[1], kFinger, from));
    POINT at = from;
    at.y -= 20; REQUIRE(form.Route(WM_POINTERUPDATE, form.checks[1], kFinger, at));
    for (int i = 0; i < 6; ++i) {
        at.y -= 60; form.touch.Advance(0.016);
        REQUIRE(form.Route(WM_POINTERUPDATE, form.checks[1], kFinger, at));
    }
    form.Settle();
    const int released = form.scroll.Position();
    CHECK_EQ(released, 360);
    REQUIRE(form.Route(WM_POINTERUP, form.checks[1], kFinger, at));
    (void)form.Pump([] { return false; }, 120);
    CHECK_EQ(form.scroll.Position(), released);
}

AETHER_TEST("form scroll: equal wheel totals travel equally far in any packet size and sign (AS4-13)") {
    graphics::MotionPolicy reduced; reduced.reduced = true;  // immediate moves: positions compare exactly
    TouchForm form(reduced);
    const auto wheel = [&](int delta) { ::SendMessageW(form.scroll.Viewport(), WM_MOUSEWHEEL, MAKEWPARAM(0, static_cast<WORD>(static_cast<SHORT>(delta))), 0); };
    wheel(-WHEEL_DELTA);
    (void)form.Pump([] { return false; }, 30);
    const int notch = form.scroll.Position();
    REQUIRE(notch > 0);
    form.scroll.ScrollTo(0);
    for (int i = 0; i < WHEEL_DELTA; ++i) wheel(-1);  // the same total as 120 precision packets
    (void)form.Pump([] { return false; }, 30);
    CHECK_EQ(form.scroll.Position(), notch);
    for (int i = 0; i < 40; ++i) wheel(-3);           // and again in another size
    (void)form.Pump([] { return false; }, 30);
    CHECK_EQ(form.scroll.Position(), 2 * notch);
    for (int i = 0; i < 2 * WHEEL_DELTA; ++i) wheel(1);  // reversed, it comes back exactly
    (void)form.Pump([] { return false; }, 30);
    CHECK_EQ(form.scroll.Position(), 0);
}


namespace {

// Client coordinates of a share of the slider's travel, at the height of its thumb.
POINT SliderPoint(HWND slider, float share) {
    RECT channel{}, thumb{};
    ::SendMessageW(slider, TBM_GETCHANNELRECT, 0, reinterpret_cast<LPARAM>(&channel));
    ::SendMessageW(slider, TBM_GETTHUMBRECT, 0, reinterpret_cast<LPARAM>(&thumb));
    const float width = static_cast<float>(thumb.right - thumb.left);
    const float travel = static_cast<float>(channel.right - channel.left) - width;
    return POINT{channel.left + static_cast<LONG>(width / 2.0f + travel * share), (thumb.top + thumb.bottom) / 2};
}
POINT ThumbCenter(HWND slider) {
    RECT thumb{};
    ::SendMessageW(slider, TBM_GETTHUMBRECT, 0, reinterpret_cast<LPARAM>(&thumb));
    return POINT{(thumb.left + thumb.right) / 2, (thumb.top + thumb.bottom) / 2};
}
POINT OnScreen(HWND slider, POINT client) {
    ::ClientToScreen(slider, &client);
    return client;
}
LRESULT Mouse(HWND slider, UINT message, POINT client) {
    return ::SendMessageW(slider, message, message == WM_LBUTTONUP ? 0 : MK_LBUTTON, MAKELPARAM(client.x, client.y));
}
int Position(HWND slider) { return static_cast<int>(::SendMessageW(slider, TBM_GETPOS, 0, 0)); }
void Prepare(HWND slider, int position) {
    ::SendMessageW(slider, TBM_SETRANGE, TRUE, MAKELPARAM(0, 100));
    ::SendMessageW(slider, TBM_SETPOS, TRUE, position);
}

}  // namespace

// The owner's report: sliders that lag, or return to where they started.
AETHER_TEST("form slider: a mouse drag follows the pointer wherever it wanders, and never returns by itself") {
    TouchForm form;
    Prepare(form.slider, 20);
    (void)Mouse(form.slider, WM_LBUTTONDOWN, ThumbCenter(form.slider));
    CHECK_EQ(Position(form.slider), 20);  // grabbing the thumb does not move it
    const POINT target = SliderPoint(form.slider, 0.75f);
    (void)Mouse(form.slider, WM_MOUSEMOVE, target);
    CHECK(std::abs(Position(form.slider) - 75) <= 2);
    const int held = Position(form.slider);
    (void)Mouse(form.slider, WM_MOUSEMOVE, POINT{target.x, target.y + 400});  // far below the track
    CHECK_EQ(Position(form.slider), held);
    (void)Mouse(form.slider, WM_MOUSEMOVE, POINT{target.x, target.y - 400});  // far above it
    CHECK_EQ(Position(form.slider), held);
    (void)Mouse(form.slider, WM_LBUTTONUP, POINT{target.x, target.y - 400});
    CHECK_EQ(Position(form.slider), held);
    (void)form.Pump([] { return false; }, 150);
    CHECK_EQ(Position(form.slider), held);
    CHECK(::GetCapture() != form.slider);
    // Past either end: exactly the end, not an overshoot.
    (void)Mouse(form.slider, WM_LBUTTONDOWN, ThumbCenter(form.slider));
    (void)Mouse(form.slider, WM_MOUSEMOVE, POINT{target.x + 2000, target.y});
    CHECK_EQ(Position(form.slider), 100);
    (void)Mouse(form.slider, WM_MOUSEMOVE, POINT{target.x - 2000, target.y});
    CHECK_EQ(Position(form.slider), 0);
    (void)Mouse(form.slider, WM_LBUTTONUP, POINT{target.x - 2000, target.y});
}

AETHER_TEST("form slider: a press on the track goes there at once and can keep dragging, with no repeated steps") {
    TouchForm form;
    Prepare(form.slider, 10);
    const POINT there = SliderPoint(form.slider, 0.8f);
    (void)Mouse(form.slider, WM_LBUTTONDOWN, there);
    CHECK(std::abs(Position(form.slider) - 80) <= 2);
    const int pressed = Position(form.slider);
    (void)form.Pump([] { return false; }, 400);       // the native control keeps stepping while held
    CHECK_EQ(Position(form.slider), pressed);
    (void)Mouse(form.slider, WM_MOUSEMOVE, SliderPoint(form.slider, 0.5f));
    CHECK(std::abs(Position(form.slider) - 50) <= 2);
    (void)Mouse(form.slider, WM_LBUTTONUP, SliderPoint(form.slider, 0.5f));
}

AETHER_TEST("form slider: a finger sideways moves it directly; a second finger does nothing; a lost contact keeps the value") {
    TouchForm form;
    Prepare(form.slider, 20);
    const auto screen = [&](float share) { return OnScreen(form.slider, SliderPoint(form.slider, share)); };
    const POINT start = OnScreen(form.slider, ThumbCenter(form.slider));
    const auto first = screen(0.6f), second = screen(0.9f);
    REQUIRE(form.Route(WM_POINTERDOWN, form.slider, kFinger, start));
    CHECK_EQ(Position(form.slider), 20);   // a touch alone moves nothing yet: it may be a pan
    REQUIRE(form.Route(WM_POINTERUPDATE, form.slider, kFinger, first));
    CHECK(std::abs(Position(form.slider) - 60) <= 2);
    const int held = Position(form.slider);
    REQUIRE(form.Route(WM_POINTERDOWN, form.slider, kOtherFinger, second));
    CHECK_FALSE(form.Route(WM_POINTERUP, form.slider, kOtherFinger, second));
    CHECK_EQ(Position(form.slider), held);
    REQUIRE(form.Route(WM_POINTERUPDATE, form.slider, kFinger, POINT{second.x, second.y + 300}));  // off the track, still following
    CHECK(std::abs(Position(form.slider) - 90) <= 2);
    REQUIRE(form.Route(WM_POINTERUP, form.slider, kFinger, POINT{second.x, second.y + 300}));
    const int released = Position(form.slider);
    CHECK(std::abs(released - 90) <= 2);
    (void)form.Pump([] { return false; }, 120);
    CHECK_EQ(Position(form.slider), released);
    CHECK(::GetCapture() != form.slider);
    CHECK_EQ(form.CanvasOffset(), 0);      // the page did not move
    // A contact Windows takes away ends the drag where it was; the next move no longer changes anything.
    REQUIRE(form.Route(WM_POINTERDOWN, form.slider, kFinger, OnScreen(form.slider, ThumbCenter(form.slider))));
    REQUIRE(form.Route(WM_POINTERUPDATE, form.slider, kFinger, first));
    const int before = Position(form.slider);
    REQUIRE(form.Route(WM_POINTERUP, form.slider, kFinger, first, /*canceled*/ true));
    CHECK_EQ(Position(form.slider), before);
    CHECK_FALSE(form.Route(WM_POINTERUPDATE, form.slider, kFinger, second));
    CHECK_EQ(Position(form.slider), before);
    // A disabled slider ignores a finger.
    ::EnableWindow(form.slider, FALSE);
    (void)form.Route(WM_POINTERDOWN, form.slider, kFinger, second);
    (void)form.Route(WM_POINTERUPDATE, form.slider, kFinger, first);
    (void)form.Route(WM_POINTERUP, form.slider, kFinger, first);
    CHECK_EQ(Position(form.slider), before);
    ::EnableWindow(form.slider, TRUE);
}

AETHER_TEST("form slider: a tap goes where the finger is, and a pan up or down scrolls the page without moving it") {
    TouchForm form;
    Prepare(form.slider, 20);
    const POINT there = OnScreen(form.slider, SliderPoint(form.slider, 0.7f));
    REQUIRE(form.Route(WM_POINTERDOWN, form.slider, kFinger, there));
    REQUIRE(form.Route(WM_POINTERUP, form.slider, kFinger, there));
    const int tapped = Position(form.slider);
    CHECK(std::abs(tapped - 70) <= 2);
    form.Settle();
    const int before = form.scroll.Position();
    form.RestedPan(form.slider, there, -120);
    form.Settle();
    CHECK_EQ(Position(form.slider), tapped);
    CHECK(form.scroll.Position() >= before + 100);  // the page followed the finger
    CHECK(::GetCapture() != form.slider);
}

AETHER_TEST("form slider: its owner hears every change during a drag, and one end when it finishes") {
    TouchForm form;
    Prepare(form.slider, 20);
    const auto tracks = [&] { return form.host.Count(WM_HSCROLL, 5000, TB_THUMBTRACK); };
    const auto ends = [&] { return form.host.Count(WM_HSCROLL, 5000, TB_ENDTRACK); };
    (void)Mouse(form.slider, WM_LBUTTONDOWN, ThumbCenter(form.slider));
    (void)Mouse(form.slider, WM_MOUSEMOVE, SliderPoint(form.slider, 0.5f));
    (void)Mouse(form.slider, WM_MOUSEMOVE, SliderPoint(form.slider, 0.7f));
    CHECK(tracks() >= 2);
    CHECK_EQ(ends(), 0);
    (void)Mouse(form.slider, WM_LBUTTONUP, SliderPoint(form.slider, 0.7f));
    CHECK_EQ(ends(), 1);
    // The keyboard stays native: arrows step, and it is still a slider to assistive technology.
    ::SetFocus(form.slider);
    const int position = Position(form.slider);
    ::SendMessageW(form.slider, WM_KEYDOWN, VK_RIGHT, 0);
    ::SendMessageW(form.slider, WM_KEYUP, VK_RIGHT, 0);
    CHECK_EQ(Position(form.slider), position + 1);
}

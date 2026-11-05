// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
// D60: an open panel closes when anything else is used, whether or not it has the foreground. Real input goes
// only to this test's own window, which stands for the application being used.
#include "AetherTest.h"
#include "app/PanelDismissal.h"

#include <windows.h>

#include <chrono>
#include <thread>
#include <vector>

using namespace aether;
using namespace aether::app;

namespace {

constexpr wchar_t kClass[] = L"AetherShell.Test.PanelDismissal";

struct OwnWindow {
    HWND window = nullptr;
    RECT bounds{120, 120, 520, 420};
    OwnWindow() {
        WNDCLASSEXW windowClass{};
        windowClass.cbSize = sizeof(windowClass);
        windowClass.lpfnWndProc = ::DefWindowProcW;
        windowClass.hInstance = ::GetModuleHandleW(nullptr);
        windowClass.lpszClassName = kClass;
        (void)::RegisterClassExW(&windowClass);
        window = ::CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, kClass, L"", WS_POPUP, bounds.left, bounds.top,
                                   bounds.right - bounds.left, bounds.bottom - bounds.top, nullptr, nullptr, windowClass.hInstance, nullptr);
        ::ShowWindow(window, SW_SHOWNOACTIVATE);
    }
    ~OwnWindow() { if (window) ::DestroyWindow(window); }
};

void Pump(int milliseconds) {
    const auto until = std::chrono::steady_clock::now() + std::chrono::milliseconds(milliseconds);
    while (std::chrono::steady_clock::now() < until) {
        MSG message{};
        while (::PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) { ::TranslateMessage(&message); ::DispatchMessageW(&message); }
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
}

void ClickAt(POINT point) {
    (void)::SetCursorPos(point.x, point.y);
    INPUT inputs[2]{};
    inputs[0].type = inputs[1].type = INPUT_MOUSE;
    inputs[0].mi.dwFlags = MOUSEEVENTF_LEFTDOWN;
    inputs[1].mi.dwFlags = MOUSEEVENTF_LEFTUP;
    (void)::SendInput(2, inputs, sizeof(INPUT));
}

struct Watch {
    PanelDismissal dismissal;
    std::vector<PanelDismissal::Reason> reasons;
    RECT panel{140, 140, 300, 300};
    bool touched = false;
    void Arm() {
        dismissal.Arm(PanelDismissal::Callbacks{
            [this](POINT point) { return ::PtInRect(&panel, point) != FALSE; },
            [this](std::uint64_t) { return touched; },
            [this](PanelDismissal::Reason reason) { reasons.push_back(reason); dismissal.Disarm(); }});
    }
};

struct PointerBack {
    POINT saved{};
    PointerBack() { (void)::GetCursorPos(&saved); }
    ~PointerBack() { (void)::SetCursorPos(saved.x, saved.y); }
};

}  // namespace

AETHER_TEST("panel dismissal: a press elsewhere closes it; a press on it does not") {
    OwnWindow own;
    REQUIRE(own.window != nullptr);
    PointerBack back;
    Watch watch;
    watch.Arm();
    REQUIRE(watch.dismissal.Armed());
    ClickAt(POINT{200, 200});              // on the panel
    Pump(120);
    CHECK(watch.reasons.empty());
    CHECK(watch.dismissal.Armed());
    (void)::SetCursorPos(400, 400);        // only moving elsewhere
    Pump(60);
    CHECK(watch.reasons.empty());
    ClickAt(POINT{400, 400});              // on what stands for the page being used, which keeps its click
    Pump(120);
    REQUIRE_EQ(watch.reasons.size(), 1u);
    CHECK_EQ(watch.reasons.front(), PanelDismissal::Reason::Press);
    CHECK_FALSE(watch.dismissal.Armed());
    ClickAt(POINT{400, 400});              // closed: nothing watches any more
    Pump(80);
    CHECK_EQ(watch.reasons.size(), 1u);
}

AETHER_TEST("panel dismissal: a touch elsewhere closes it a moment later; one on the panel does not") {
    Watch watch;
    watch.Arm();
    watch.touched = true;
    watch.dismissal.ContactLanded();
    Pump(220);
    CHECK(watch.reasons.empty());
    CHECK(watch.dismissal.Armed());
    watch.touched = false;
    watch.dismissal.ContactLanded();
    watch.dismissal.ContactLanded();       // a second finger of the same gesture
    CHECK(watch.reasons.empty());          // not before the panel could have heard of it
    Pump(220);
    REQUIRE_EQ(watch.reasons.size(), 1u);
    CHECK_EQ(watch.reasons.front(), PanelDismissal::Reason::Touch);
}

AETHER_TEST("panel dismissal: Escape reaches it without the focus, and is left alone once it closes") {
    Watch watch;
    watch.Arm();
    INPUT inputs[2]{};
    inputs[0].type = inputs[1].type = INPUT_KEYBOARD;
    inputs[0].ki.wVk = inputs[1].ki.wVk = VK_ESCAPE;
    inputs[1].ki.dwFlags = KEYEVENTF_KEYUP;
    (void)::SendInput(2, inputs, sizeof(INPUT));
    Pump(150);
    REQUIRE_EQ(watch.reasons.size(), 1u);
    CHECK_EQ(watch.reasons.front(), PanelDismissal::Reason::Escape);
    // The hotkey is gone with it: another registration of the same key succeeds.
    const bool free = ::RegisterHotKey(nullptr, 77, 0, VK_ESCAPE) != FALSE;
    CHECK(free);
    if (free) ::UnregisterHotKey(nullptr, 77);
}

AETHER_TEST("panel dismissal: disarming leaves nothing behind and it can be armed again") {
    Watch watch;
    watch.Arm();
    watch.dismissal.ContactLanded();
    watch.dismissal.Disarm();
    Pump(200);
    CHECK(watch.reasons.empty());          // the pending touch went with it
    watch.Arm();
    CHECK(watch.dismissal.Armed());
    watch.dismissal.Disarm();
    watch.dismissal.Disarm();
    CHECK_FALSE(watch.dismissal.Armed());
}

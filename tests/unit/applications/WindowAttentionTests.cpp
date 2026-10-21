// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
#include "AetherTest.h"
#include "applications/WindowAttention.h"

#include <cstdint>

using namespace aether::apps;
namespace {
HWND Window(std::uintptr_t value) { return reinterpret_cast<HWND>(value); }
}

AETHER_TEST("attention: only explicit Shell flash establishes an attention request") {
    WindowAttentionState state;
    for (const auto event : {WindowAttentionEvent::Created, WindowAttentionEvent::Redraw,
                             WindowAttentionEvent::Activated, WindowAttentionEvent::Destroyed}) {
        CHECK_FALSE(state.Observe(event, Window(1)));
        CHECK_FALSE(state.Requested(Window(1)));
    }
    CHECK(state.Observe(WindowAttentionEvent::Flash, Window(1)));
    CHECK(state.Requested(Window(1)));
    CHECK_FALSE(state.Observe(WindowAttentionEvent::Redraw, Window(1)));
    CHECK(state.Requested(Window(1))); // Title redraw is not a documented flash-stop notification.
    CHECK(state.Observe(WindowAttentionEvent::Created, Window(1)));
    CHECK_FALSE(state.Requested(Window(1))); // a reused HWND cannot inherit a previous lifetime
}

AETHER_TEST("attention: repeated OS flash notices coalesce until real acknowledgement") {
    WindowAttentionState state; int changes = 0;
    for (int i = 0; i < 1000; ++i) changes += state.Observe(WindowAttentionEvent::Flash, Window(1)) ? 1 : 0;
    CHECK_EQ(changes, 1);
    CHECK(state.Observe(WindowAttentionEvent::Activated, Window(1)));
    CHECK_FALSE(state.Requested(Window(1)));
    CHECK(state.Observe(WindowAttentionEvent::Flash, Window(1))); // a later genuine request is new
}

AETHER_TEST("attention: foreground windows and null handles cannot request attention") {
    WindowAttentionState state;
    CHECK_FALSE(state.Observe(WindowAttentionEvent::Flash, nullptr));
    CHECK_FALSE(state.Observe(WindowAttentionEvent::Flash, Window(1), true));
    CHECK_FALSE(state.Requested(Window(1)));
    CHECK(state.Observe(WindowAttentionEvent::Flash, Window(1)));
    CHECK(state.Observe(WindowAttentionEvent::Flash, Window(1), true));
    CHECK_FALSE(state.Requested(Window(1)));
}

AETHER_TEST("attention: destruction and eligibility pruning forget only removed windows") {
    WindowAttentionState state;
    CHECK(state.Observe(WindowAttentionEvent::Flash, Window(1)));
    CHECK(state.Observe(WindowAttentionEvent::Flash, Window(2)));
    CHECK(state.Observe(WindowAttentionEvent::Flash, Window(3)));
    CHECK(state.Observe(WindowAttentionEvent::Destroyed, Window(1)));
    state.Retain({Window(2)});
    CHECK_FALSE(state.Requested(Window(1)));CHECK(state.Requested(Window(2)));CHECK_FALSE(state.Requested(Window(3)));
    state.Clear();CHECK_FALSE(state.Requested(Window(2)));
}

AETHER_TEST("attention: storage is bounded without evicting an existing real request") {
    WindowAttentionState state;
    for (std::uintptr_t i = 1; i <= WindowAttentionState::kMaximumRequests; ++i)
        REQUIRE(state.Observe(WindowAttentionEvent::Flash, Window(i)));
    CHECK_FALSE(state.Observe(WindowAttentionEvent::Flash, Window(WindowAttentionState::kMaximumRequests + 1)));
    CHECK(state.Requested(Window(1)));
    CHECK(state.Observe(WindowAttentionEvent::Destroyed, Window(1)));
    CHECK(state.Observe(WindowAttentionEvent::Flash, Window(WindowAttentionState::kMaximumRequests + 1)));
}

AETHER_TEST("attention: registered Shell code classification keeps redraw distinct from flash") {
    CHECK(ClassifyShellAttentionEvent(HSHELL_FLASH) == WindowAttentionEvent::Flash);
    CHECK(ClassifyShellAttentionEvent(HSHELL_REDRAW) == WindowAttentionEvent::Redraw);
    CHECK(ClassifyShellAttentionEvent(HSHELL_WINDOWCREATED) == WindowAttentionEvent::Created);
    CHECK(ClassifyShellAttentionEvent(HSHELL_WINDOWACTIVATED) == WindowAttentionEvent::Activated);
    CHECK(ClassifyShellAttentionEvent(HSHELL_RUDEAPPACTIVATED) == WindowAttentionEvent::Activated);
    CHECK(ClassifyShellAttentionEvent(HSHELL_WINDOWDESTROYED) == WindowAttentionEvent::Destroyed);
    CHECK_FALSE(ClassifyShellAttentionEvent(HSHELL_APPCOMMAND).has_value());
}

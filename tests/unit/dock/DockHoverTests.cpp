// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
#include "AetherTest.h"
#include "dock/DockHover.h"

using namespace aether::dock;

namespace {
constexpr DockHoverTiming kTiming{0.6, 0.12, 0.35, 0.8};
}

AETHER_TEST("dock hover: a running icon emphasizes at once and opens its card only after the dwell") {
    DockHoverSession session(kTiming);
    session.PointerOnItem(L"exe:b", true, true, 10.0);
    CHECK_EQ(session.Emphasized(), L"exe:b");
    CHECK(session.Previewed().empty());
    REQUIRE(session.Deadline().has_value());
    CHECK_NEAR(*session.Deadline(), 10.6, 1e-9);
    session.Advance(10.59);
    CHECK(session.Previewed().empty());
    session.Advance(10.6);
    CHECK_EQ(session.Previewed(), L"exe:b");
    CHECK_EQ(session.Emphasized(), L"exe:b");
    CHECK_FALSE(session.Deadline().has_value());
}

AETHER_TEST("dock hover: leaving before the dwell cancels it and contracts without a stale timer") {
    DockHoverSession session(kTiming);
    session.PointerOnItem(L"exe:b", true, true, 0.0);
    session.PointerOffDock(0.3);
    CHECK(session.Emphasized().empty());
    CHECK_FALSE(session.Deadline().has_value());
    session.Advance(5.0);  // a late timer callback cannot open the cancelled card
    CHECK(session.Previewed().empty());
    session.PointerOnItem(L"exe:b", true, true, 6.0);
    session.PointerOnItem(L"exe:c", true, true, 6.2);  // passing over icons restarts the dwell
    session.Advance(6.6);
    CHECK(session.Previewed().empty());
    session.Advance(6.8);
    CHECK_EQ(session.Previewed(), L"exe:c");
}

AETHER_TEST("dock hover: icon and card are one interaction with a bounded crossing grace") {
    DockHoverSession session(kTiming);
    session.PointerOnItem(L"exe:b", true, true, 0.0);
    session.Advance(0.6);
    session.PointerOffDock(1.0);  // towards the card
    CHECK_EQ(session.Previewed(), L"exe:b");
    CHECK_EQ(session.Emphasized(), L"exe:b");
    REQUIRE(session.Deadline().has_value());
    session.PointerOnPreview(1.1);
    CHECK_FALSE(session.Deadline().has_value());
    session.Advance(3.0);
    CHECK_EQ(session.Previewed(), L"exe:b");
    CHECK_EQ(session.Emphasized(), L"exe:b");
    CHECK(session.PointerOnPreviewSurface());
    session.PointerOffPreview(3.0);  // leaves both
    session.Advance(3.34);
    CHECK_EQ(session.Previewed(), L"exe:b");
    session.Advance(3.35);
    CHECK(session.Previewed().empty());
    CHECK(session.Emphasized().empty());
}

AETHER_TEST("dock hover: returning from the card to its icon keeps it in either event order") {
    DockHoverSession first(kTiming), second(kTiming);
    for (auto* session : {&first, &second}) {
        session->PointerOnItem(L"exe:b", true, true, 0.0);
        session->Advance(0.6);
        session->PointerOffDock(1.0);
        session->PointerOnPreview(1.05);
    }
    first.PointerOffPreview(2.0); first.PointerOnItem(L"EXE:B", true, true, 2.02);
    second.PointerOnItem(L"exe:b", true, true, 2.0); second.PointerOffPreview(2.02);
    for (auto* session : {&first, &second}) {
        session->Advance(5.0);
        CHECK_EQ(session->Previewed(), L"exe:b");
        CHECK(session->Emphasizes(L"exe:b"));
    }
}

AETHER_TEST("dock hover: a shown card switches to another running icon and closes on other entries") {
    DockHoverSession session(kTiming);
    session.PointerOnItem(L"exe:b", true, true, 0.0);
    session.Advance(0.6);
    session.PointerOnItem(L"exe:c", true, true, 1.0);
    CHECK_EQ(session.Emphasized(), L"exe:c");
    CHECK_EQ(session.Previewed(), L"exe:b");  // warm switch, no extra dwell
    session.Advance(1.12);
    CHECK_EQ(session.Previewed(), L"exe:c");
    session.PointerOnItem(L"exe:closed-pin", false, false, 2.0);
    CHECK(session.Previewed().empty());
    CHECK(session.Emphasized().empty());  // a closed pin never acquires a marker
    session.PointerOnItem(L"aether:search", false, false, 2.5);
    CHECK(session.Emphasized().empty());
}

AETHER_TEST("dock hover: activation closes the card, holds emphasis briefly and suppresses until leave") {
    DockHoverSession session(kTiming);
    session.PointerOnItem(L"exe:b", true, true, 0.0);
    session.Advance(0.6);
    session.PointerOnPreview(0.7);
    session.Activated(L"exe:b", 1.0);
    CHECK(session.Previewed().empty());
    CHECK(session.Emphasizes(L"exe:b"));
    session.PointerOffDock(1.0);
    CHECK(session.Emphasizes(L"exe:b"));  // the observed foreground transfer continues this width
    session.Advance(1.79);
    CHECK(session.Emphasizes(L"exe:b"));
    session.Advance(1.8);
    CHECK_FALSE(session.Emphasizes(L"exe:b"));

    session.PointerOnItem(L"exe:b", true, true, 2.0);
    session.Activated(L"exe:b", 2.1);
    session.Advance(5.0);
    CHECK(session.Previewed().empty());  // still on the clicked icon: no card reappears
    session.PointerOffDock(5.1);
    session.PointerOnItem(L"exe:b", true, true, 5.2);
    session.Advance(5.8);
    CHECK_EQ(session.Previewed(), L"exe:b");
}

AETHER_TEST("dock hover: refused activation drops the hold so the marker stays truthful") {
    DockHoverSession session(kTiming);
    session.PointerOnItem(L"exe:b", true, true, 0.0);
    session.Activated(L"exe:b", 0.1);
    session.PointerOffDock(0.1);
    session.ActivationFailed();
    CHECK_FALSE(session.Emphasizes(L"exe:b"));
}

AETHER_TEST("dock hover: windows lost during dwell or while shown never leave a card behind") {
    DockHoverSession session(kTiming);
    session.PointerOnItem(L"exe:b", true, true, 0.0);
    session.PointerOnItem(L"exe:b", true, false, 0.3);  // its last window was destroyed
    session.Advance(1.0);
    CHECK(session.Previewed().empty());
    CHECK_EQ(session.Emphasized(), L"exe:b");
    session.PointerOnItem(L"exe:b", true, true, 1.1);
    CHECK_FALSE(session.Deadline().has_value());  // same item: the dwell restarts only on re-entry
    session.PointerOffDock(1.2);
    session.PointerOnItem(L"exe:b", true, true, 1.3);
    session.Advance(1.9);
    REQUIRE_EQ(session.Previewed(), L"exe:b");
    session.PointerOffDock(1.95);
    session.PointerOnPreview(2.0);
    session.PreviewUnavailable(L"exe:b");
    CHECK(session.Previewed().empty());
    CHECK_FALSE(session.PointerOnPreviewSurface());
    CHECK(session.Emphasized().empty());
}

AETHER_TEST("dock hover: keyboard preview opens at once, ignores pointer grace and resets cleanly") {
    DockHoverSession session(kTiming);
    session.OpenForKeyboard(L"exe:b");
    CHECK_EQ(session.Previewed(), L"exe:b");
    CHECK(session.KeyboardPreview());
    CHECK_EQ(session.Emphasized(), L"exe:b");
    session.PointerOffDock(1.0);
    CHECK_FALSE(session.Deadline().has_value());
    CHECK_EQ(session.Previewed(), L"exe:b");
    session.Reset();
    CHECK(session.Previewed().empty());
    CHECK(session.Emphasized().empty());
    CHECK_FALSE(session.KeyboardPreview());
    CHECK_FALSE(session.Deadline().has_value());
}

AETHER_TEST("dock hover: rapid alternation between icons never leaves two cards or stale emphasis") {
    DockHoverSession session(kTiming);
    double now = 0;
    for (int pass = 0; pass < 40; ++pass) {
        session.PointerOnItem(pass % 2 ? L"exe:a" : L"exe:b", true, true, now);
        CHECK_EQ(session.Emphasized(), pass % 2 ? L"exe:a" : L"exe:b");
        now += 0.05;
        session.Advance(now);
        CHECK(session.Previewed().empty());
    }
    session.Advance(now + 0.6);
    CHECK_EQ(session.Previewed(), L"exe:a");
    CHECK_FALSE(session.Emphasizes(L"exe:b"));
}

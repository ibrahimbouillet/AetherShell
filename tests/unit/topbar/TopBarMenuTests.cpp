// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
#include "AetherTest.h"
#include "topbar/TopBarMenu.h"

#include <algorithm>
#include <utility>
#include <vector>

using namespace aether;
using namespace aether::topbar;

namespace {
const ui::MenuEntry* Find(const std::vector<ui::MenuEntry>& menu, BarCommand command) {
    return ui::FindMenuCommand(menu, static_cast<std::uint32_t>(command));
}

class FocusOps final : public ui::IMenuWindowOps {
public:
    HWND foreground = reinterpret_cast<HWND>(std::uintptr_t{0x10});
    HWND target = reinterpret_cast<HWND>(std::uintptr_t{0x20});
    bool valid = true, accept = true, actuallyRestore = true;
    unsigned focusCalls = 0;
    HWND Foreground() noexcept override { return foreground; }
    bool SetForeground(HWND window) noexcept override {
        ++focusCalls;
        if (accept && actuallyRestore) foreground = window;
        return accept;
    }
    bool IsValidWindow(HWND window) noexcept override { return valid && window == target; }
    LONG_PTR ExtendedStyle(HWND) noexcept override { return 0; }
    void SetExtendedStyle(HWND, LONG_PTR) noexcept override {}
    UINT Track(HMENU, UINT, POINT, HWND, const TPMPARAMS*) noexcept override { return 0; }
    void PostNull(HWND) noexcept override {}
};
}

AETHER_TEST("topbar menus: every item has a valid shared menu model without system actions") {
    for (const auto kind : {ItemKind::Logo, ItemKind::Weather, ItemKind::Keyboard, ItemKind::Network,
        ItemKind::Volume, ItemKind::Microphone, ItemKind::Battery, ItemKind::Clock, ItemKind::NotificationArea}) {
        const auto menu = BuildTopBarMenu(kind, {true, true, false}, true);
        REQUIRE(ui::ValidateMenu(menu).ok());
    }
    CHECK(Find(BuildTopBarMenu(ItemKind::Microphone, {}, false), BarCommand::MicrophoneSettings) != nullptr);
    CHECK(Find(BuildTopBarMenu(ItemKind::Volume, {}, false), BarCommand::SoundSettings) != nullptr);
}

AETHER_TEST("topbar menus: runtime reveal and resume follow availability without changing preferences") {
    auto menu = BuildTopBarMenu(ItemKind::Logo, {}, false);
    CHECK(Find(menu, BarCommand::RevealWindowsTaskbar) == nullptr);
    CHECK(Find(menu, BarCommand::ResumeReplacement) == nullptr);
    menu = BuildTopBarMenu(ItemKind::Logo, {true, true, false}, false);
    CHECK(Find(menu, BarCommand::RevealWindowsTaskbar) != nullptr);
    CHECK(Find(menu, BarCommand::ResumeReplacement) == nullptr);
    menu = BuildTopBarMenu(ItemKind::Logo, {true, false, true}, false);
    CHECK(Find(menu, BarCommand::RevealWindowsTaskbar) == nullptr);
    CHECK(Find(menu, BarCommand::ResumeReplacement) != nullptr);
    // D45: the hidden-icons chevron opens Windows' overflow; taskbar recovery stays in the logo menu.
    for (const auto state : {TaskbarReplacementState{}, TaskbarReplacementState{true, true, false}, TaskbarReplacementState{true, false, true}}) {
        menu = BuildTopBarMenu(ItemKind::NotificationArea, state, false);
        REQUIRE_EQ(menu.size(), 1u);
        CHECK(Find(menu, BarCommand::HiddenIcons) != nullptr);
        CHECK(Find(menu, BarCommand::RevealWindowsTaskbar) == nullptr);
        CHECK(Find(menu, BarCommand::ResumeReplacement) == nullptr);
    }
}

AETHER_TEST("topbar menus: the clock offers notifications with the calendar and the calendar on its own") {
    const auto menu = BuildTopBarMenu(ItemKind::Clock, {}, false);
    REQUIRE(ui::ValidateMenu(menu).ok());
    CHECK(Find(menu, BarCommand::Clock) != nullptr);
    CHECK(Find(menu, BarCommand::Calendar) != nullptr);
}

AETHER_TEST("topbar menus: update commands cannot remain in a cleared menu") {
    CHECK(Find(BuildTopBarMenu(ItemKind::Logo, {}, true), BarCommand::ViewUpdate) != nullptr);
    CHECK(Find(BuildTopBarMenu(ItemKind::Logo, {}, false), BarCommand::ViewUpdate) == nullptr);
}

AETHER_TEST("topbar voice typing: microphone menu retains mute privacy and the explicit Windows command") {
    const auto menu = BuildTopBarMenu(ItemKind::Microphone, {}, false);
    CHECK(Find(menu, BarCommand::ToggleMicrophone) != nullptr);
    CHECK(Find(menu, BarCommand::MicrophoneSettings) != nullptr);
    const auto* voice = Find(menu, BarCommand::VoiceTyping);
    REQUIRE(voice != nullptr);
    CHECK(voice->enabled);
    CHECK(voice->text.find(L"Win+H") != std::wstring::npos);
    CHECK(Find(BuildTopBarMenu(ItemKind::Logo, {}, false), BarCommand::VoiceTyping) == nullptr);
}

AETHER_TEST("topbar voice typing: input target is restored and verified before requesting dictation") {
    FocusOps ops;
    const auto owner = ops.foreground;
    REQUIRE(RestoreVoiceTypingTarget(ops, owner, ops.target).ok());
    CHECK(ops.foreground == ops.target); CHECK_EQ(ops.focusCalls, 1u);
    REQUIRE(RestoreVoiceTypingTarget(ops, owner, ops.target).ok());
    CHECK_EQ(ops.focusCalls, 1u); // already restored: no extra foreground mutation
}

AETHER_TEST("topbar voice typing: missing stale rejected and unverified targets never authorize dictation") {
    FocusOps ops;
    const auto owner = ops.foreground;
    CHECK_FALSE(RestoreVoiceTypingTarget(ops, owner, nullptr).ok());
    CHECK_FALSE(RestoreVoiceTypingTarget(ops, owner, owner).ok());
    ops.valid = false;
    CHECK_FALSE(RestoreVoiceTypingTarget(ops, owner, ops.target).ok());
    ops.valid = true; ops.foreground = reinterpret_cast<HWND>(std::uintptr_t{0x30});
    CHECK_FALSE(RestoreVoiceTypingTarget(ops, owner, ops.target).ok());
    CHECK_EQ(ops.focusCalls, 0u); // a third foreground window must never be overridden
    ops.foreground = owner; ops.accept = false;
    CHECK_FALSE(RestoreVoiceTypingTarget(ops, owner, ops.target).ok());
    ops.accept = true; ops.actuallyRestore = false;
    CHECK_FALSE(RestoreVoiceTypingTarget(ops, owner, ops.target).ok());
}

AETHER_TEST("topbar geometry: reservation contains the panel and exterior shadow at every supported height") {
    for (const UINT dpi : {96u, 144u, 192u}) for (const int height : {24, 32, 48}) {
        const RECT area{-1920, -200, 0, 880};
        const auto placement = ComputeTopBarPlacement(area, dpi, height, 6, 24);
        REQUIRE(placement.ok());
        const auto& p = placement.value();
        const auto panelBottom = p.windowBoundsPx.top + p.panelBoundsPx.bottom;
        CHECK(p.windowBoundsPx.bottom <= area.top + p.reservationHeightPx);
        CHECK(panelBottom < p.windowBoundsPx.bottom);
        CHECK_EQ(p.panelBoundsPx.bottom - p.panelBoundsPx.top, height * static_cast<int>(dpi) / 96);
        CHECK(p.windowBoundsPx.left + p.panelBoundsPx.left > area.left);
        CHECK(p.windowBoundsPx.right > p.windowBoundsPx.left);
    }
}

AETHER_TEST("topbar geometry: malformed inputs cannot produce invalid HWND bounds") {
    const RECT area{0, 0, 1920, 1080};
    CHECK_FALSE(ComputeTopBarPlacement(area, 0, 32, 6, 24).ok());
    CHECK_FALSE(ComputeTopBarPlacement(area, 96, 23, 6, 24).ok());
    CHECK_FALSE(ComputeTopBarPlacement(area, 96, 49, 6, 24).ok());
    CHECK_FALSE(ComputeTopBarPlacement(area, 96, 32, -1, 24).ok());
    CHECK_FALSE(ComputeTopBarPlacement(RECT{0, 0, 20, 100}, 96, 32, 0, 24).ok());
}

AETHER_TEST("topbar menus: the logo menu keeps its commands, order and labels and gains its glass icons") {
    const auto menu = BuildTopBarMenu(ItemKind::Logo, {true, true, false}, true);
    const std::vector<std::pair<BarCommand, graphics::Glyph>> expected{
        {BarCommand::Search, graphics::Glyph::Search},       {BarCommand::Settings, graphics::Glyph::Settings},
        {BarCommand::RevealWindowsTaskbar, graphics::Glyph::Taskbar}, {BarCommand::ViewUpdate, graphics::Glyph::Download},
        {BarCommand::CheckUpdates, graphics::Glyph::Updates}, {BarCommand::About, graphics::Glyph::About},
        {BarCommand::Quit, graphics::Glyph::Power}};
    std::vector<const ui::MenuEntry*> commands;
    for (const auto& entry : menu) if (!entry.separator) commands.push_back(&entry);
    REQUIRE_EQ(commands.size(), expected.size());
    for (std::size_t i = 0; i < expected.size(); ++i) {
        CHECK_EQ(commands[i]->id, static_cast<std::uint32_t>(expected[i].first));
        REQUIRE(commands[i]->glyph.has_value());
        CHECK(*commands[i]->glyph == expected[i].second);
        CHECK(commands[i]->enabled);
    }
    // The exact recovery wording and timeout are policy, not decoration.
    CHECK_EQ(Find(menu, BarCommand::RevealWindowsTaskbar)->text, std::wstring(L"Show Windows &notification area for 60 seconds"));
    CHECK_EQ(Find(menu, BarCommand::Quit)->text, std::wstring(L"&Quit Aether Shell"));
    // A separator still divides the commands from About/Quit; item menus stay icon-free.
    CHECK(std::any_of(menu.begin(), menu.end(), [](const ui::MenuEntry& entry) { return entry.separator; }));
    for (const auto& entry : BuildTopBarMenu(ItemKind::Volume, {}, false)) CHECK_FALSE(entry.glyph.has_value());
    const auto resume = BuildTopBarMenu(ItemKind::Logo, {true, false, true}, false);
    REQUIRE(Find(resume, BarCommand::ResumeReplacement) != nullptr);
    CHECK(Find(resume, BarCommand::ResumeReplacement)->glyph == graphics::Glyph::Taskbar);
}

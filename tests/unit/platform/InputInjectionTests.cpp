// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
//
// Key-chord construction and partial-insertion recovery. Only a fake injector is used: these tests
// never inject global input.
#include "AetherTest.h"

#include "platform/windows/InputInjection.h"

#include <deque>
#include <initializer_list>
#include <vector>

using namespace aether;
using namespace aether::platform;

namespace {

struct KeyEvent {
    WORD key = 0;
    bool up = false;
    bool extended = false;
    bool operator==(const KeyEvent&) const = default;
};

class FakeInjector final : public IKeyboardInjector {
public:
    std::vector<std::vector<KeyEvent>> calls;
    std::deque<UINT> results;  // inserted counts to report; when empty, everything is inserted

    UINT Send(std::span<INPUT> inputs) noexcept override {
        std::vector<KeyEvent> events;
        for (const auto& input : inputs) {
            events.push_back({input.ki.wVk, (input.ki.dwFlags & KEYEVENTF_KEYUP) != 0,
                              (input.ki.dwFlags & KEYEVENTF_EXTENDEDKEY) != 0});
        }
        calls.push_back(std::move(events));
        if (results.empty()) return static_cast<UINT>(inputs.size());
        const UINT result = results.front();
        results.pop_front();
        return result;
    }
};

std::vector<KeyEvent> Events(const std::vector<INPUT>& inputs) {
    std::vector<KeyEvent> events;
    for (const auto& input : inputs) {
        events.push_back({input.ki.wVk, (input.ki.dwFlags & KEYEVENTF_KEYUP) != 0,
                          (input.ki.dwFlags & KEYEVENTF_EXTENDEDKEY) != 0});
    }
    return events;
}

}  // namespace

AETHER_TEST("input injection: chords press in order and release in reverse with extended Windows keys") {
    const WORD taskView[] = {VK_LWIN, VK_TAB};
    auto inputs = BuildChordInputs(taskView);
    REQUIRE(inputs.ok());
    REQUIRE_EQ(inputs->size(), 4u);
    for (const auto& input : *inputs) {
        CHECK_EQ(input.type, static_cast<DWORD>(INPUT_KEYBOARD));
        CHECK_EQ(input.ki.wScan, static_cast<WORD>(0));
        CHECK_EQ(input.ki.time, static_cast<DWORD>(0));
        CHECK_EQ(input.ki.dwExtraInfo, static_cast<ULONG_PTR>(0));
        CHECK_EQ(input.ki.dwFlags & ~static_cast<DWORD>(KEYEVENTF_KEYUP | KEYEVENTF_EXTENDEDKEY), static_cast<DWORD>(0));
    }
    const std::vector<KeyEvent> expected{
        {VK_LWIN, false, true}, {VK_TAB, false, false}, {VK_TAB, true, false}, {VK_LWIN, true, true}};
    CHECK(Events(*inputs) == expected);

    const WORD soundOutput[] = {VK_LWIN, VK_CONTROL, 'V'};
    auto three = BuildChordInputs(soundOutput);
    REQUIRE(three.ok());
    const std::vector<KeyEvent> expectedThree{{VK_LWIN, false, true}, {VK_CONTROL, false, false}, {'V', false, false},
                                              {'V', true, false}, {VK_CONTROL, true, false}, {VK_LWIN, true, true}};
    CHECK(Events(*three) == expectedThree);
}

AETHER_TEST("input injection: extended keys follow the keyboard's E0 set") {
    for (const WORD key : std::initializer_list<WORD>{VK_LWIN, VK_RWIN, VK_APPS, VK_RCONTROL, VK_RMENU, VK_INSERT,
             VK_DELETE, VK_HOME, VK_END, VK_PRIOR, VK_NEXT, VK_LEFT, VK_UP, VK_RIGHT, VK_DOWN, VK_DIVIDE, VK_NUMLOCK,
             VK_SNAPSHOT}) {
        CHECK(IsExtendedKey(key));
    }
    for (const WORD key : std::initializer_list<WORD>{VK_CONTROL, VK_LCONTROL, VK_SHIFT, VK_MENU, VK_SPACE, VK_TAB,
             VK_RETURN, 'N', 'A', VK_F10}) {
        CHECK_FALSE(IsExtendedKey(key));
    }
}

AETHER_TEST("input injection: invalid chords are rejected before anything is sent") {
    FakeInjector injector;
    CHECK_EQ(BuildChordInputs({}).error().code, ErrorCode::InvalidArgument);
    const WORD tooMany[] = {'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H', 'I'};
    CHECK_EQ(BuildChordInputs(tooMany).error().code, ErrorCode::InvalidArgument);
    const WORD eight[] = {'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H'};
    CHECK(BuildChordInputs(eight).ok());
    const WORD zero[] = {VK_LWIN, 0};
    CHECK_EQ(BuildChordInputs(zero).error().code, ErrorCode::InvalidArgument);
    const WORD outside[] = {VK_LWIN, 0x100};
    CHECK_EQ(BuildChordInputs(outside).error().code, ErrorCode::InvalidArgument);
    const WORD repeated[] = {VK_LWIN, 'N', VK_LWIN};
    CHECK_EQ(BuildChordInputs(repeated).error().code, ErrorCode::InvalidArgument);
    CHECK_EQ(SendKeyChord(injector, repeated).error().code, ErrorCode::InvalidArgument);
    CHECK_EQ(SendKeyChord(injector, std::span<const WORD>{}).error().code, ErrorCode::InvalidArgument);
    CHECK(injector.calls.empty());
}

AETHER_TEST("input injection: a fully inserted chord is one call and succeeds") {
    FakeInjector injector;
    REQUIRE(SendKeyChord(injector, {VK_LWIN, 'N'}).ok());
    REQUIRE_EQ(injector.calls.size(), 1u);
    const std::vector<KeyEvent> expected{{VK_LWIN, false, true}, {'N', false, false}, {'N', true, false}, {VK_LWIN, true, true}};
    CHECK(injector.calls[0] == expected);
    const std::vector<WORD> fromVector{VK_LWIN, 'A'};
    REQUIRE(SendKeyChord(injector, fromVector).ok());
    CHECK_EQ(injector.calls.size(), 2u);
}

AETHER_TEST("input injection: a partial insertion releases every key that is still down") {
    {
        // Only the Windows key went down.
        FakeInjector injector; injector.results = {1};
        const auto result = SendKeyChord(injector, {VK_LWIN, VK_TAB});
        REQUIRE_FALSE(result.ok()); CHECK_EQ(result.error().code, ErrorCode::Platform);
        REQUIRE_EQ(injector.calls.size(), 2u);
        const std::vector<KeyEvent> recovery{{VK_LWIN, true, true}};
        CHECK(injector.calls[1] == recovery);
    }
    {
        // Both keys went down and Tab came back up; the Windows key is still down.
        FakeInjector injector; injector.results = {3};
        CHECK_EQ(SendKeyChord(injector, {VK_LWIN, VK_TAB}).error().code, ErrorCode::Platform);
        REQUIRE_EQ(injector.calls.size(), 2u);
        const std::vector<KeyEvent> recovery{{VK_LWIN, true, true}};
        CHECK(injector.calls[1] == recovery);
    }
    {
        // Two of three keys went down: release them in reverse order.
        FakeInjector injector; injector.results = {2};
        CHECK_EQ(SendKeyChord(injector, {VK_LWIN, VK_CONTROL, 'V'}).error().code, ErrorCode::Platform);
        REQUIRE_EQ(injector.calls.size(), 2u);
        const std::vector<KeyEvent> recovery{{VK_CONTROL, true, false}, {VK_LWIN, true, true}};
        CHECK(injector.calls[1] == recovery);
    }
    {
        // All keys went down and one came up.
        FakeInjector injector; injector.results = {4};
        CHECK_EQ(SendKeyChord(injector, {VK_LWIN, VK_CONTROL, 'V'}).error().code, ErrorCode::Platform);
        REQUIRE_EQ(injector.calls.size(), 2u);
        const std::vector<KeyEvent> recovery{{VK_CONTROL, true, false}, {VK_LWIN, true, true}};
        CHECK(injector.calls[1] == recovery);
    }
}

AETHER_TEST("input injection: nothing inserted needs no recovery and a failed recovery retries each key once") {
    {
        FakeInjector injector; injector.results = {0};
        const auto result = SendKeyChord(injector, {VK_LWIN, 'S'});
        REQUIRE_FALSE(result.ok()); CHECK_EQ(result.error().code, ErrorCode::Platform);
        CHECK_EQ(injector.calls.size(), 1u);
    }
    {
        FakeInjector injector; injector.results = {3, 1, 1};
        CHECK_EQ(SendKeyChord(injector, {VK_LWIN, VK_CONTROL, 'V'}).error().code, ErrorCode::Platform);
        REQUIRE_EQ(injector.calls.size(), 4u);
        const std::vector<KeyEvent> recovery{{'V', true, false}, {VK_CONTROL, true, false}, {VK_LWIN, true, true}};
        CHECK(injector.calls[1] == recovery);
        CHECK(injector.calls[2] == (std::vector<KeyEvent>{{VK_CONTROL, true, false}}));
        CHECK(injector.calls[3] == (std::vector<KeyEvent>{{VK_LWIN, true, true}}));
    }
}

AETHER_TEST("input injection: the SendInput factory returns an injector without sending anything") {
    auto injector = MakeSendInputInjector();
    REQUIRE(injector.ok());
    CHECK(*injector != nullptr);
}

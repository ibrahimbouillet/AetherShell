// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
// Shell surface routes with a fake keyboard injector only: no input is ever injected, and the Start
// menu's window-message route is never exercised (it would reach the real shell).
#include "AetherTest.h"

#include "applications/SystemShellActions.h"

#include <cstddef>
#include <cstdint>
#include <iterator>
#include <string>
#include <vector>

using namespace aether;
using namespace aether::apps;

namespace {

class FakeInjector final : public platform::IKeyboardInjector {
public:
    std::vector<std::vector<INPUT>> calls;
    std::vector<UINT> results;  // per call; when exhausted, everything is accepted

    UINT Send(std::span<INPUT> inputs) noexcept override {
        calls.emplace_back(inputs.begin(), inputs.end());
        const std::size_t index = calls.size() - 1;
        return index < results.size() ? results[index] : static_cast<UINT>(inputs.size());
    }
};

struct Key {
    WORD virtualKey;
    bool up;
    bool extended;
};

std::vector<Key> Keys(const std::vector<INPUT>& inputs) {
    std::vector<Key> keys;
    for (const auto& input : inputs) {
        CHECK_EQ(input.type, DWORD{INPUT_KEYBOARD});
        keys.push_back(Key{input.ki.wVk, (input.ki.dwFlags & KEYEVENTF_KEYUP) != 0,
                           (input.ki.dwFlags & KEYEVENTF_EXTENDEDKEY) != 0});
    }
    return keys;
}

void CheckChord(const std::vector<INPUT>& inputs, std::initializer_list<WORD> chord) {
    const auto keys = Keys(inputs);
    REQUIRE_EQ(keys.size(), chord.size() * 2);
    std::size_t index = 0;
    for (const WORD key : chord) {  // downs in order
        CHECK_EQ(keys[index].virtualKey, key);
        CHECK_FALSE(keys[index].up);
        ++index;
    }
    for (auto key = std::rbegin(chord); key != std::rend(chord); ++key) {  // ups in reverse
        CHECK_EQ(keys[index].virtualKey, *key);
        CHECK(keys[index].up);
        ++index;
    }
    for (const auto& key : keys) CHECK_EQ(key.extended, key.virtualKey == VK_LWIN);
}

}  // namespace

AETHER_TEST("shell actions: Task View is Win+Tab in one call") {
    FakeInjector injector;
    REQUIRE(OpenTaskView(injector).ok());
    REQUIRE_EQ(injector.calls.size(), std::size_t{1});
    CheckChord(injector.calls[0], {static_cast<WORD>(VK_LWIN), static_cast<WORD>(VK_TAB)});
}

AETHER_TEST("shell actions: Windows Search is Win+S in one call") {
    FakeInjector injector;
    REQUIRE(OpenWindowsSearch(injector).ok());
    REQUIRE_EQ(injector.calls.size(), std::size_t{1});
    CheckChord(injector.calls[0], {static_cast<WORD>(VK_LWIN), static_cast<WORD>('S')});
}

AETHER_TEST("shell actions: Start falls back to the Windows key without a usable window") {
    FakeInjector byRoute;
    REQUIRE(OpenStartMenu(nullptr, byRoute, StartMenuRoute::WindowsKey).ok());
    REQUIRE_EQ(byRoute.calls.size(), std::size_t{1});
    CheckChord(byRoute.calls[0], {static_cast<WORD>(VK_LWIN)});

    // The window-message route needs a window of the calling thread; a null window takes the key route.
    FakeInjector withoutWindow;
    REQUIRE(OpenStartMenu(nullptr, withoutWindow).ok());
    REQUIRE_EQ(withoutWindow.calls.size(), std::size_t{1});
    CheckChord(withoutWindow.calls[0], {static_cast<WORD>(VK_LWIN)});

    // A handle that is not a window is treated the same way.
    FakeInjector invalid;
    REQUIRE(OpenStartMenu(reinterpret_cast<HWND>(static_cast<std::uintptr_t>(0x7FF0)), invalid).ok());
    CHECK_EQ(invalid.calls.size(), std::size_t{1});
}

AETHER_TEST("shell actions: a partial chord releases the keys that went down and reports Platform") {
    FakeInjector injector;
    injector.results = {1};  // only the Windows key went down
    const auto result = OpenTaskView(injector);
    REQUIRE_FALSE(result.ok());
    CHECK_EQ(result.error().code, ErrorCode::Platform);
    CHECK_EQ(result.error().message, std::string("Task View could not be opened"));
    REQUIRE_EQ(injector.calls.size(), std::size_t{2});
    const auto recovery = Keys(injector.calls[1]);
    REQUIRE_EQ(recovery.size(), std::size_t{1});
    CHECK_EQ(recovery[0].virtualKey, WORD{VK_LWIN});
    CHECK(recovery[0].up);

    FakeInjector blocked;
    blocked.results = {0};
    const auto none = OpenStartMenu(nullptr, blocked, StartMenuRoute::WindowsKey);
    REQUIRE_FALSE(none.ok());
    CHECK_EQ(none.error().code, ErrorCode::Platform);
    CHECK_EQ(none.error().message, std::string("Windows Start could not be opened"));
    CHECK_EQ(blocked.calls.size(), std::size_t{1});  // nothing went down, nothing to release

    FakeInjector search;
    search.results = {3};  // both downs and the 'S' up: only the Windows key is still down
    REQUIRE_FALSE(OpenWindowsSearch(search).ok());
    REQUIRE_EQ(search.calls.size(), std::size_t{2});
    const auto released = Keys(search.calls[1]);
    REQUIRE_EQ(released.size(), std::size_t{1});
    CHECK_EQ(released[0].virtualKey, WORD{VK_LWIN});
}

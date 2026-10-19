// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
//
// Synthesized keyboard shortcuts for shell routes that Windows documents only as a key
// combination (for example Win+Tab for Task View or Win+N for notifications).
//
// Product code only, and only in direct response to a user's click or key press. Tests use a fake
// IKeyboardInjector and never inject global input.
//
// UIPI: SendInput is subject to User Interface Privilege Isolation. Input is silently dropped when
// the foreground window belongs to a process of higher integrity (for example an elevated
// application); neither the return value nor GetLastError reports that case, so callers cannot
// detect it and must not retry.
#pragma once

#include "core/Result.h"

#include <windows.h>

#include <cstddef>
#include <initializer_list>
#include <memory>
#include <span>
#include <vector>

namespace aether::platform {

inline constexpr std::size_t kMaxChordKeys = 8;

// Inserts keyboard events. The production implementation calls SendInput.
class IKeyboardInjector {
public:
    virtual ~IKeyboardInjector() = default;
    // Returns how many of `inputs` were inserted, with SendInput semantics (0 when blocked).
    virtual UINT Send(std::span<INPUT> inputs) noexcept = 0;
};

[[nodiscard]] Result<std::unique_ptr<IKeyboardInjector>> MakeSendInputInjector();
// Process-wide SendInput injector (stateless), used by the convenience SendKeyChord overloads.
[[nodiscard]] IKeyboardInjector& SystemKeyboardInjector() noexcept;

// Keys that Windows reports with the extended-key flag (E0 prefix): the Windows and Apps keys,
// right Ctrl/Alt, the navigation block, arrows, numeric-keypad divide, Num Lock and Print Screen.
[[nodiscard]] bool IsExtendedKey(WORD virtualKey) noexcept;

// Pure. Key-downs in the given order followed by key-ups in reverse order, with
// KEYEVENTF_EXTENDEDKEY on extended keys such as VK_LWIN. Rejects an empty chord, more than
// kMaxChordKeys keys, a key outside 0x01..0xFE and repeated keys.
[[nodiscard]] Result<std::vector<INPUT>> BuildChordInputs(std::span<const WORD> keys);

// Sends the chord in one call. When Windows inserts only part of it, the key-ups of every key that
// went down are sent again (one retry per key) so no key stays pressed, and Platform is returned.
// Nothing inserted at all also returns Platform. See the UIPI note above.
[[nodiscard]] Result<void> SendKeyChord(IKeyboardInjector& injector, std::span<const WORD> keys);
[[nodiscard]] inline Result<void> SendKeyChord(IKeyboardInjector& injector, std::initializer_list<WORD> keys) {
    return SendKeyChord(injector, std::span<const WORD>(keys.begin(), keys.size()));
}
[[nodiscard]] inline Result<void> SendKeyChord(std::span<const WORD> keys) {
    return SendKeyChord(SystemKeyboardInjector(), keys);
}
[[nodiscard]] inline Result<void> SendKeyChord(std::initializer_list<WORD> keys) {
    return SendKeyChord(SystemKeyboardInjector(), keys);
}

}  // namespace aether::platform

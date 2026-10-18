// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
//
// Input language provider. The language shown is the input language of the thread that owns the
// foreground window, i.e. the language the user is typing in:
//   GetKeyboardLayout(GetWindowThreadProcessId(GetForegroundWindow(), nullptr))
//
// A dedicated thread with a GetMessage loop re-reads it
//  * on EVENT_SYSTEM_FOREGROUND (SetWinEventHook, WINEVENT_OUTOFCONTEXT, delivered to that thread), and
//  * on a lightweight periodic timer (1 s by default, coalescable) that only compares the HKL. Windows
//    has no documented system-wide notification when another process changes its input language
//    (WM_INPUTLANGCHANGE only reaches the windows of the thread that switched), so a cheap comparison
//    is the only reliable way to notice a switch made with Win+Space or Alt+Shift in another app.
// The (comparatively expensive) description of a layout is cached per HKL; snapshots are published
// only when they change.
//
// Limitations (also in docs/modules/status.md):
//  * Console windows: the window belongs to conhost.exe or to the terminal application; the language
//    read is that host's thread language, which normally but not always matches the console's input.
//  * Elevated windows: the HKL can be read, but foreground events from higher-integrity processes may
//    not be delivered, so a change there can take up to one timer period to show.
//  * IME conversion modes (for example Japanese Hiragana/Katakana, Chinese/English mode) are not
//    represented: only the input language and keyboard layout are.
//  * "Let me use a different input method for each app window" (per-app input setting): when it is off
//    (the default) Windows applies one language to every app, and the foreground thread may only pick
//    up the new language once it processes input; when it is on, the value follows the foreground app.
//  * Secure desktop / no foreground window: falls back to the provider thread's own layout with
//    fromForegroundWindow = false.
#pragma once

#include "status/StatusTypes.h"

#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>

namespace aether::status {

// One reading of the active input locale.
struct InputLocaleReading {
    std::uintptr_t hkl = 0;             // the HKL value; 0 when no input locale could be read
    bool fromForegroundWindow = false;  // false: fell back to the calling thread's own layout
    bool operator==(const InputLocaleReading&) const = default;
};

// The input locale of the thread that owns the foreground window; without a foreground window (secure
// desktop, focus in transition) the calling thread's own layout with fromForegroundWindow = false.
[[nodiscard]] InputLocaleReading ReadForegroundInputLocale() noexcept;

struct KeyboardProviderOptions {
    // Period of the HKL comparison (clamped to 50 ms .. 60 s). The timer is coalescable (tolerance = a
    // quarter of the period).
    std::chrono::milliseconds checkInterval{1'000};
    // Source of the readings, called on the provider thread. Empty means ReadForegroundInputLocale; tests
    // inject a fake to drive the provider deterministically.
    std::function<InputLocaleReading()> readInputLocale;
};

class KeyboardProvider final : public IStatusProvider<KeyboardSnapshot> {
public:
    explicit KeyboardProvider(KeyboardProviderOptions options = {});
    ~KeyboardProvider() override;
    KeyboardProvider(const KeyboardProvider&) = delete;
    KeyboardProvider& operator=(const KeyboardProvider&) = delete;

    Result<void> Start(Listener listener) override;
    void Stop() noexcept override;
    [[nodiscard]] KeyboardSnapshot Current() const override;

    // Thread-safe. Re-reads the language now and drops cached layout names. Call on
    // WM_SETTINGCHANGE (display language or language list changed) and on WM_INPUTLANGCHANGE received
    // by Aether Shell's own windows.
    void Refresh() noexcept;

private:
    struct Impl;
    std::shared_ptr<Impl> impl_;
};

}  // namespace aether::status

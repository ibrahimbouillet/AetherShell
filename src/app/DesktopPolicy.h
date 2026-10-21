// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
#pragma once

#include "app/TaskbarController.h"

#include <string_view>
#include <cstdint>
#include <optional>

namespace aether::app {

// Pure desktop-ownership rules used by ShellApplication; no Win32 calls, so they are unit-tested.

// The single-instance name of the installed application. Only a session under this name may hide
// the real Windows taskbar (D23 makes replacement the default): own-window tests and diagnostics
// run under their own instance names and never reach the native taskbar, whatever their settings.
inline constexpr std::wstring_view kProductionInstanceName = L"AetherShell";

[[nodiscard]] bool MayAccessNativeTaskbar(std::wstring_view instanceName, bool requested) noexcept;

// Whether the Dock keeps the replacement-mode AppBar reservation: during activation, its retries,
// an active session and its restoration, but not after the controller gave up (the Windows taskbar
// is back and reserves its own space), not while replacement is suspended, and never again after
// the Dock reservation itself failed. The reserved height always follows the Dock's own layout.
[[nodiscard]] bool ShouldForceDockReservation(bool reservationFailed, bool replacementWanted,
                                              const TaskbarUpdate& update) noexcept;

// Whether the application's poll timer must keep asking the controller to observe its session.
// It stops once the controller reports that nothing more will happen without a new request
// (for example after the activation attempts are exhausted), and is armed again by the next one.
[[nodiscard]] bool ShouldObserveTaskbar(const TaskbarUpdate& update) noexcept;

[[nodiscard]] bool ShouldSuppressDock(bool fullscreen, const TaskbarUpdate& update, bool attentionPeek = false) noexcept;
// A queued stale timer may not expire a newer attention peek. Pure monotonic deadline policy.
[[nodiscard]] std::optional<std::uint64_t> AttentionPeekDelay(bool armed, bool closing,
    bool hasAttention, std::uint64_t now, std::uint64_t deadline) noexcept;
[[nodiscard]] bool ShouldAutomaticallyImportPins(bool replacement, bool sync, bool imported,
                                                bool explorerApplied) noexcept;

// A killed Win32 timer may still have a queued message. Only an armed reveal may resume, and a
// message from an earlier reveal must wait for the current monotonic deadline (milliseconds).
[[nodiscard]] std::optional<std::uint64_t> TaskbarRevealDelay(bool automatic, bool armed,
    bool replacement, bool closing, const TaskbarUpdate& update,
    std::uint64_t now, std::uint64_t deadline) noexcept;

}  // namespace aether::app

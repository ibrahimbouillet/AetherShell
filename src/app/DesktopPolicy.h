// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
#pragma once

#include "app/TaskbarController.h"

#include <string_view>

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

}  // namespace aether::app

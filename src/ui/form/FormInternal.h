// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
//
// Module-private helpers shared by FormSurface, FormScrollView and ConfirmDialog.
#pragma once

#include <windows.h>

namespace aether::ui::form::detail {

// Posted by FormSurface to every ancestor of a registered control that receives keyboard focus
// while no mouse button is down; a FormScrollView viewport answers by revealing the focus.
[[nodiscard]] UINT RevealFocusMessage() noexcept;
// Sent synchronously to registered containers after a form policy update.
[[nodiscard]] UINT PolicyChangedMessage() noexcept;

// True when the window's UI state shows keyboard focus cues (UISF_HIDEFOCUS clear).
[[nodiscard]] bool ShowsFocusCues(HWND hwnd) noexcept;
// True when access-key underlines are shown (UISF_HIDEACCEL clear).
[[nodiscard]] bool ShowsAccelerators(HWND hwnd) noexcept;

}  // namespace aether::ui::form::detail

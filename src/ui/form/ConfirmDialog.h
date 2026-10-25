// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
//
// The shared Aether confirmation: a small modal window on the FormSurface material with a status
// glyph, a title, a message, an optional detail and up to three native push buttons. It replaces
// stock message boxes for healthy-state confirmations (reset, quit with unsaved settings, ...).
//
// Behavior: the owner is disabled while the dialog is open (like MessageBox) and re-enabled and
// reactivated afterwards; Enter chooses the default button, Escape and the close box choose Cancel,
// Tab/Shift+Tab move between the buttons, Alt+mnemonic works through the button labels. A WM_QUIT
// received during the dialog ends it with Cancel and is re-posted. The buttons and texts are native
// windows, so screen readers read and invoke them through their standard providers.
//
// When the surface cannot be created (no renderer, window creation failure) the same question is
// asked with MessageBoxW and the closest button set, so a confirmation is never silently skipped.
// Threading: UI thread only.
#pragma once

#include "core/Result.h"
#include "graphics/Animation.h"
#include "graphics/Theme.h"
#include "ui/form/FormSurface.h"

#include <windows.h>

#include <functional>
#include <string>

namespace aether::graphics {
class FrameScheduler;
}

namespace aether::ui::form {

enum class ConfirmChoice { Primary, Secondary, Cancel };

struct ConfirmRequest {
    HWND owner = nullptr;             // may be null (centered on the primary work area)
    std::wstring windowTitle = L"Aether Shell";
    std::wstring title;               // short question or statement
    std::wstring message;             // one or two sentences
    std::wstring detail;              // optional secondary text (helper style)
    Severity severity = Severity::Info;
    std::wstring primary;             // required, e.g. L"&Reset"
    std::wstring secondary;           // optional
    std::wstring cancel = L"Cancel";  // empty: no cancel button (Escape then chooses the last button)
    bool dangerousPrimary = false;    // Danger style for destructive primary actions
    ConfirmChoice defaultChoice = ConfirmChoice::Cancel;
};

// Fallback MessageBox flags and the mapping back to a choice (exposed for tests).
[[nodiscard]] UINT FallbackMessageBoxFlags(const ConfirmRequest& request) noexcept;
[[nodiscard]] ConfirmChoice ChoiceFromMessageBox(const ConfirmRequest& request, int result) noexcept;

// Shows the confirmation modally and returns the choice. Errors only for invalid requests (no
// primary label); presentation failures fall back to MessageBoxW as described above.
[[nodiscard]] Result<ConfirmChoice> ShowConfirmation(const ConfirmRequest& request, const graphics::DesignTokens& tokens,
                                                     const graphics::MotionPolicy& motion,
                                                     graphics::FrameScheduler* scheduler = nullptr);

// Test seam: when set, ShowConfirmation creates the dialog, calls the hook with the dialog window
// once it is visible (the hook may post input to it) and continues the modal loop. Null restores
// the default. UI thread only.
void SetConfirmationTestHook(std::function<void(HWND dialog)> hook);

}  // namespace aether::ui::form

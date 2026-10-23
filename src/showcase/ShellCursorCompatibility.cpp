// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
#include "showcase/ShellCursorCompatibility.h"

namespace aether::showcase {
namespace {
bool IsBounded(const ShellCursorBounds& bounds) noexcept {
    const auto width = static_cast<std::int64_t>(bounds.right) - bounds.left;
    const auto height = static_cast<std::int64_t>(bounds.bottom) - bounds.top;
    return width > 0 && height > 0 && width <= 5 && height <= 5;
}
}

Result<bool> IsCompatibleShellCursorHelper(const ShellCursorSnapshot& snapshot,
    std::size_t alreadyAcceptedHelpers) noexcept {
    if (alreadyAcceptedHelpers != 0 || snapshot.windowClass != L"UIHubMouseHiderWindowClass" ||
        !snapshot.processId || snapshot.processId != snapshot.expectedShellProcessId ||
        !snapshot.shellImageVerified || !snapshot.shellProcessAlive || !snapshot.shellIsCurrent ||
        !snapshot.boundsKnown || !snapshot.frameBoundsKnown ||
        !IsBounded(snapshot.bounds) || !IsBounded(snapshot.frameBounds) ||
        snapshot.frameBounds.left < snapshot.bounds.left || snapshot.frameBounds.top < snapshot.bounds.top ||
        snapshot.frameBounds.right > snapshot.bounds.right || snapshot.frameBounds.bottom > snapshot.bounds.bottom ||
        !snapshot.styleKnown || !snapshot.extendedStyleKnown || !snapshot.classStyleKnown ||
        snapshot.style != 0x94000000U || snapshot.extendedStyle != 0x08080088U ||
        (snapshot.classStyle & 0x00020000U) != 0 || // CS_DROPSHADOW could exceed the bounded rectangle.
        !snapshot.layeredAttributesKnown || snapshot.layeredFlags != 0x00000002U || snapshot.alpha > 1)
        return false;
    return true;
}
} // namespace aether::showcase

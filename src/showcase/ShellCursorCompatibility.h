// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
#pragma once
#include "core/Result.h"
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace aether::showcase {
struct ShellCursorBounds final {
    std::int32_t left = 0, top = 0, right = 0, bottom = 0;
};

// An observed compatibility rule, not an invisibility test or a documented UIHub contract.
// The caller gathers fresh native attributes and retains the verified original shell process.
struct ShellCursorSnapshot final {
    std::wstring_view windowClass;
    std::uint32_t processId = 0, expectedShellProcessId = 0;
    bool shellImageVerified = false, shellProcessAlive = false, shellIsCurrent = false;
    bool boundsKnown = false, frameBoundsKnown = false;
    ShellCursorBounds bounds, frameBounds;
    bool styleKnown = false, extendedStyleKnown = false, classStyleKnown = false;
    std::uint32_t style = 0, extendedStyle = 0, classStyle = 0;
    bool layeredAttributesKnown = false;
    std::uint32_t layeredFlags = 0;
    std::uint8_t alpha = 255;
};

// Accepts at most one <=5x5 Explorer-owned helper at <=1/255 opacity in one scene check.
// A successful result does not mean the helper contributes zero pixels.
[[nodiscard]] Result<bool> IsCompatibleShellCursorHelper(const ShellCursorSnapshot& snapshot,
    std::size_t alreadyAcceptedHelpers) noexcept;
} // namespace aether::showcase

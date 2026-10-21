// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
#pragma once

#include <windows.h>

#include <cstddef>
#include <optional>
#include <unordered_set>

namespace aether::apps {

enum class WindowAttentionEvent { Flash, Activated, Destroyed, Redraw, Created };

// Registered SHELLHOOK messages contain the target HWND in lParam. Only HSHELL_FLASH establishes
// attention: ordinary REDRAW/CREATED events never imply that an application needs interaction.
[[nodiscard]] std::optional<WindowAttentionEvent> ClassifyShellAttentionEvent(WPARAM code) noexcept;

// Pure, bounded, UI-thread-owned latch. The caller validates native HWND eligibility and supplies
// foreground facts. No HWND API, content read, animation, activation or periodic polling occurs here.
class WindowAttentionState {
public:
    static constexpr std::size_t kMaximumRequests = 4096;
    [[nodiscard]] bool Observe(WindowAttentionEvent event, HWND window, bool foreground = false);
    void Retain(const std::unordered_set<HWND>& eligible);
    [[nodiscard]] bool Requested(HWND window) const noexcept;
    void Clear() noexcept;

private:
    std::unordered_set<HWND> requested_;
};

} // namespace aether::apps

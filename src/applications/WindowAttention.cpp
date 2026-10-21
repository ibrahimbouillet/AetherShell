// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
#include "applications/WindowAttention.h"

#include <algorithm>

namespace aether::apps {

std::optional<WindowAttentionEvent> ClassifyShellAttentionEvent(WPARAM code) noexcept {
    if (code == HSHELL_FLASH) return WindowAttentionEvent::Flash;
    if (code == HSHELL_WINDOWACTIVATED || code == HSHELL_RUDEAPPACTIVATED) return WindowAttentionEvent::Activated;
    if (code == HSHELL_WINDOWDESTROYED) return WindowAttentionEvent::Destroyed;
    if (code == HSHELL_REDRAW) return WindowAttentionEvent::Redraw;
    if (code == HSHELL_WINDOWCREATED) return WindowAttentionEvent::Created;
    return {};
}

bool WindowAttentionState::Observe(WindowAttentionEvent event, HWND window, bool foreground) {
    if (!window) return false;
    if (event == WindowAttentionEvent::Activated || event == WindowAttentionEvent::Destroyed ||
        event == WindowAttentionEvent::Created ||
        (event == WindowAttentionEvent::Flash && foreground)) return requested_.erase(window) != 0;
    if (event != WindowAttentionEvent::Flash || requested_.contains(window) || requested_.size() >= kMaximumRequests)
        return false;
    return requested_.insert(window).second;
}

void WindowAttentionState::Retain(const std::unordered_set<HWND>& eligible) {
    std::erase_if(requested_, [&](HWND window) { return !eligible.contains(window); });
}
bool WindowAttentionState::Requested(HWND window) const noexcept { return requested_.contains(window); }
void WindowAttentionState::Clear() noexcept { requested_.clear(); }

} // namespace aether::apps

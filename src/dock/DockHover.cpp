// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
#include "dock/DockHover.h"

#include "core/Strings.h"

#include <algorithm>
#include <cmath>

namespace aether::dock {
namespace {

bool Same(std::wstring_view a, std::wstring_view b) noexcept {
    return !a.empty() && !b.empty() && text::EqualsIgnoreCase(a, b);
}
double Finite(double now) noexcept { return std::isfinite(now) ? now : 0.0; }

}  // namespace

void DockHoverSession::PointerOnItem(std::wstring_view id, bool running, bool previewable, double now) {
    now = Finite(now);
    if (id.empty()) { LeaveItem(now); Recompute(); return; }
    if (Same(id, item_)) {
        itemRunning_ = running;
        itemPreviewable_ = previewable;
        if (!previewable) {
            dwellDeadline_.reset();
            if (Same(previewed_, id)) { previewed_.clear(); onPreview_ = false; keyboard_ = false; }
        }
        Recompute();
        return;
    }
    if (!Same(id, suppressed_)) suppressed_.clear();
    item_.assign(id);
    itemRunning_ = running;
    itemPreviewable_ = previewable;
    keyboard_ = false;
    graceDeadline_.reset();
    if (previewable && !Same(id, suppressed_)) {
        if (Same(previewed_, id)) {
            dwellDeadline_.reset();
        } else {
            dwellTarget_.assign(id);
            dwellDeadline_ = now + (previewed_.empty() ? timing_.dwell : timing_.warmSwitch);
        }
    } else {
        dwellDeadline_.reset();
        // The pointer explicitly rests on another entry that has no card: close the old card now.
        if (!previewed_.empty() && !onPreview_) previewed_.clear();
    }
    Recompute();
}

void DockHoverSession::LeaveItem(double now) {
    item_.clear();
    itemRunning_ = false;
    itemPreviewable_ = false;
    suppressed_.clear();
    dwellDeadline_.reset();
    StartGrace(now);
}

void DockHoverSession::StartGrace(double now) {
    if (previewed_.empty() || onPreview_ || keyboard_ || graceDeadline_) return;
    graceDeadline_ = now + timing_.grace;
}

void DockHoverSession::PointerOffDock(double now) {
    LeaveItem(Finite(now));
    Recompute();
}

void DockHoverSession::PointerOnPreview(double now) {
    (void)now;
    if (previewed_.empty()) return;  // a hiding card cannot restart the interaction
    onPreview_ = true;
    graceDeadline_.reset();
    Recompute();
}

void DockHoverSession::PointerOffPreview(double now) {
    if (!onPreview_) return;
    onPreview_ = false;
    if (!Same(item_, previewed_)) StartGrace(Finite(now));
    Recompute();
}

void DockHoverSession::OpenForKeyboard(std::wstring_view id) {
    if (id.empty()) return;
    previewed_.assign(id);
    keyboard_ = true;
    suppressed_.clear();
    dwellDeadline_.reset();
    graceDeadline_.reset();
    Recompute();
}

void DockHoverSession::Advance(double now) {
    now = Finite(now);
    if (dwellDeadline_ && now >= *dwellDeadline_) {
        dwellDeadline_.reset();
        if (Same(item_, dwellTarget_) && itemPreviewable_ && !Same(item_, suppressed_)) {
            previewed_ = item_;
            onPreview_ = false;
        }
    }
    if (graceDeadline_ && now >= *graceDeadline_) {
        graceDeadline_.reset();
        if (!onPreview_ && !keyboard_ && !Same(item_, previewed_)) previewed_.clear();
    }
    if (holdDeadline_ && now >= *holdDeadline_) { holdDeadline_.reset(); holdId_.clear(); }
    Recompute();
}

void DockHoverSession::Activated(std::wstring_view id, double now) {
    now = Finite(now);
    previewed_.clear();
    keyboard_ = false;
    onPreview_ = false;
    dwellDeadline_.reset();
    graceDeadline_.reset();
    suppressed_ = Same(item_, id) ? item_ : std::wstring();
    holdId_.assign(id);
    if (holdId_.empty()) holdDeadline_.reset();
    else holdDeadline_ = now + timing_.activationHold;
    Recompute();
}

void DockHoverSession::ActivationFailed() noexcept {
    holdId_.clear();
    holdDeadline_.reset();
}

void DockHoverSession::PreviewUnavailable(std::wstring_view id) {
    if (Same(previewed_, id)) {
        previewed_.clear();
        onPreview_ = false;
        keyboard_ = false;
        graceDeadline_.reset();
    }
    if (Same(item_, id)) {
        itemPreviewable_ = false;
        dwellDeadline_.reset();
    }
    Recompute();
}

void DockHoverSession::Reset() noexcept {
    item_.clear(); previewed_.clear(); emphasized_.clear(); suppressed_.clear(); dwellTarget_.clear(); holdId_.clear();
    itemRunning_ = itemPreviewable_ = onPreview_ = keyboard_ = false;
    dwellDeadline_.reset(); graceDeadline_.reset(); holdDeadline_.reset();
}

bool DockHoverSession::Emphasizes(std::wstring_view id) const noexcept {
    return Same(emphasized_, id) || Same(holdId_, id);
}

std::optional<double> DockHoverSession::Deadline() const noexcept {
    std::optional<double> next;
    for (const auto& deadline : {dwellDeadline_, graceDeadline_, holdDeadline_})
        if (deadline && (!next || *deadline < *next)) next = deadline;
    return next;
}

void DockHoverSession::Recompute() {
    // The card's application stays emphasized while the pointer is on its icon, on the card, crossing
    // between them, or while the keyboard explores it. Otherwise a running icon under the pointer is.
    if (!previewed_.empty() && (keyboard_ || onPreview_ || graceDeadline_ || Same(item_, previewed_)))
        emphasized_ = previewed_;
    else if (itemRunning_)
        emphasized_ = item_;
    else
        emphasized_.clear();
}

}  // namespace aether::dock

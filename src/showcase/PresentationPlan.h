// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
#pragma once

#include "core/Result.h"
#include <cstddef>
#include <functional>
#include <span>
#include <string_view>
#include <vector>

namespace aether::showcase {

enum class PresentationActionKind { OpenQuickControls = 1, OpenSettings, VolumeOffset, RestoreVolume,
    ClosePanels, SettingsPage, QuickControlsNextPage, OpenSearchExplorer, DockExplorerMenu, TopBarMenu };
struct PresentationAction final {
    double atSeconds = 0;
    PresentationActionKind kind = PresentationActionKind::ClosePanels;
    double value = 0;
};
struct PresentationPlan final {
    double durationSeconds = 5;
    double stillAtSeconds = 2;
    std::vector<PresentationAction> actions;
};

// Numeric-only plans contain no executable names, arbitrary UI selectors or private metadata.
// The format is AETHER_PRESENTATION_1, then duration/still/count, then time/kind/value rows.
[[nodiscard]] Result<PresentationPlan> MakePresentationPlan(std::string_view name) noexcept;
[[nodiscard]] Result<PresentationPlan> ParsePresentationPlan(std::string_view text) noexcept;
[[nodiscard]] Result<void> ValidatePresentationPlan(const PresentationPlan& plan) noexcept;
[[nodiscard]] bool IsPresentationShotAction(PresentationActionKind kind) noexcept;
[[nodiscard]] Result<void> ValidatePresentationObservations(const PresentationPlan& plan,
    std::span<const double> started, std::span<const double> completed, std::size_t appliedCount) noexcept;

class IPresentationActions {
public:
    virtual ~IPresentationActions() = default;
    [[nodiscard]] virtual Result<void> Prepare(bool changesVolume) = 0;
    [[nodiscard]] virtual Result<void> Apply(const PresentationAction& action) = 0;
    [[nodiscard]] virtual Result<void> Restore() noexcept = 0;
};

// Scheduler is independent of capture and native UI. Tests use a fake and never affect devices.
class PresentationSession final {
public:
    explicit PresentationSession(IPresentationActions& actions, bool restoreImmediatelyOnFailure = true) noexcept
        : actions_(actions), restoreImmediatelyOnFailure_(restoreImmediatelyOnFailure) {}
    ~PresentationSession();
    PresentationSession(const PresentationSession&) = delete;
    PresentationSession& operator=(const PresentationSession&) = delete;
    [[nodiscard]] Result<void> Begin(PresentationPlan plan, bool allowDeviceChanges) noexcept;
    // Observer receives index/start=true before Apply and index/start=false after acknowledgement.
    using ActionObserver = std::function<void(std::size_t index, bool starting)>;
    [[nodiscard]] Result<void> Tick(double elapsedSeconds, const ActionObserver& observer = {}) noexcept;
    [[nodiscard]] std::size_t AppliedActionCount() const noexcept { return next_; }
    [[nodiscard]] Result<void> Restore() noexcept;
private:
    IPresentationActions& actions_;
    PresentationPlan plan_;
    std::size_t next_ = 0;
    double previous_ = -1;
    bool active_ = false;
    bool restoreImmediatelyOnFailure_ = true;
};
}  // namespace aether::showcase

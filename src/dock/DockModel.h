// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
#pragma once

#include "applications/ApplicationTypes.h"
#include "applications/RunningPresentation.h"
#include "settings/Settings.h"

#include <span>

namespace aether::dock {

// Fixed system entries precede application items and never become reorder targets.
enum class DockItemKind { Search, Pinned, Running, Overflow, Start, TaskView };

// Only applications can be dragged or be drop targets; fixed entries never move.
[[nodiscard]] constexpr bool IsReorderable(DockItemKind kind) noexcept {
    return kind == DockItemKind::Pinned || kind == DockItemKind::Running;
}
// Items that stand for an application (launch, icon, pin and unpin).
[[nodiscard]] constexpr bool IsApplicationItem(DockItemKind kind) noexcept { return IsReorderable(kind); }
struct DockItem {
    DockItemKind kind = DockItemKind::Search;
    std::wstring id;
    std::wstring displayName;
    apps::ApplicationInfo application;
    std::optional<apps::RunningApplication> runningApplication;
    bool running = false;
    bool foreground = false;
    std::size_t windowCount = 0;
};
struct FittedDock {
    std::vector<DockItem> items;
    std::vector<DockItem> overflow;
    float iconSize = 48;
    float magnification = 1.45f;
};

// Everything the Dock model is built from. Further optional inputs (an application index for
// running-application presentation) are added here, so BuildDockItems keeps one signature.
struct DockModelInputs {
    const settings::DockSettings& settings;
    std::span<const apps::RunningApplication> running;
    // Session-only order of application ids; unknown/new applications follow in their default order.
    std::span<const std::wstring> sessionOrder;
    const apps::ApplicationIndex* catalog = nullptr;
};

Result<std::vector<DockItem>> BuildDockItems(const DockModelInputs& inputs);
// Forwards to the DockModelInputs overload.
Result<std::vector<DockItem>> BuildDockItems(const settings::DockSettings& settings,
                                           const std::vector<apps::RunningApplication>& running);
Result<FittedDock> FitDockItems(const std::vector<DockItem>& items, const settings::DockSettings& settings,
                               float availableWidthDip);
Result<settings::PinnedApp> PinFromApplication(const apps::ApplicationInfo& app);
Result<std::vector<settings::PinnedApp>> PinApplication(const std::vector<settings::PinnedApp>& pins,
                                                      const apps::ApplicationInfo& app);
Result<std::vector<settings::PinnedApp>> UnpinApplication(const std::vector<settings::PinnedApp>& pins,
                                                        std::wstring_view id);
Result<std::vector<settings::PinnedApp>> MovePin(const std::vector<settings::PinnedApp>& pins,
                                               std::wstring_view id, std::size_t destination);
struct DockReorderResult {
    std::vector<settings::PinnedApp> pins;
    std::vector<std::wstring> sessionOrder;
    bool pinsChanged = false;
};
// Every application can move in the session, including window-only activation groups. Only moving
// an existing pin changes persisted pin order; a gesture never pins an unpinned application.
Result<DockReorderResult> ReorderDockItems(const DockModelInputs& inputs,
                                         std::wstring_view draggedId, std::wstring_view destinationId);
// Reconcile an external Settings pin-order change while retaining the session's running slots.
Result<std::vector<std::wstring>> AlignSessionPins(std::span<const std::wstring> sessionOrder,
                                                  std::span<const settings::PinnedApp> pins);
// Compatibility helper returning only the persistent portion of ReorderDockItems.
Result<std::vector<settings::PinnedApp>> ReorderDockApplications(
    const settings::DockSettings& settings, const std::vector<apps::RunningApplication>& running,
    std::wstring_view draggedId, std::wstring_view destinationId);

}  // namespace aether::dock

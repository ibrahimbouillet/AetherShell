// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
#pragma once

#include "applications/ApplicationTypes.h"
#include "settings/Settings.h"

namespace aether::dock {

enum class DockItemKind { Search, Pinned, Running, Overflow };
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
// Moving an unpinned application makes it persistent. Earlier running items that must precede its
// new position become pins too, preserving the requested order under the pins-first model.
Result<std::vector<settings::PinnedApp>> ReorderDockApplications(
    const settings::DockSettings& settings, const std::vector<apps::RunningApplication>& running,
    std::wstring_view draggedId, std::wstring_view destinationId);

}  // namespace aether::dock

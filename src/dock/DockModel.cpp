// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
#include "dock/DockModel.h"

#include "applications/ApplicationIdentity.h"
#include "applications/KnownApplications.h"
#include "applications/IconResource.h"
#include "core/Strings.h"

#include <algorithm>
#include <cmath>
#include <set>
#include <map>

namespace aether::dock {
namespace {

apps::ApplicationInfo FromPin(const settings::PinnedApp& pin) {
    apps::ApplicationInfo app;
    app.id = pin.id; app.displayName = pin.displayName; app.parsingName = pin.parsingName;
    app.appUserModelId = pin.appUserModelId; app.executablePath = pin.executablePath; app.shortcutPath = pin.shortcutPath;
    app.arguments = pin.arguments;
    app.kind = apps::IsPackagedAumid(app.appUserModelId) ? apps::ApplicationKind::Packaged
        : app.executablePath.empty() ? apps::ApplicationKind::Other : apps::ApplicationKind::Desktop;
    return app;
}
float Width(std::size_t count, float size, float spacing, float magnification) {
    const float icons = static_cast<float>(count);
    return 24 + icons * size + std::max(0.0f, icons - 1) * spacing + 3.0f * size * (magnification - 1);
}

// Some desktop shortcuts expose an AppUserModelID that their windows omit. Match that missing
// side only through a unique, argument-free pin and the same validated full executable path.
// Explicit window/profile identities retain precedence; a browser binary alone is not a profile.
bool MatchesUnidentifiedDesktop(const settings::PinnedApp& pin, const apps::RunningApplication& running,
                               const DockModelInputs& inputs, const apps::ApplicationIndex& catalog) {
    const auto aumid = text::Trim(pin.appUserModelId);
    if (!apps::IsExplicitAumid(aumid) || apps::IsPackagedAumid(aumid) ||
        !text::Trim(running.appUserModelId).empty() || !text::Trim(pin.arguments).empty()) return false;
    const auto pinnedPath = apps::NormalizeExecutablePath(pin.executablePath);
    const auto runningPath = apps::NormalizeExecutablePath(running.executablePath);
    if (!pinnedPath || !runningPath || !text::EqualsIgnoreCase(*pinnedPath, *runningPath) ||
        catalog.HasExecutableWithOtherAumid(*pinnedPath, aumid)) return false;
    std::set<std::wstring> candidates;
    for (const auto& other : inputs.settings.pinned) {
        if (other.id.empty()) continue;
        const auto path = apps::NormalizeExecutablePath(other.executablePath);
        if (path && text::EqualsIgnoreCase(*path, *pinnedPath)) candidates.insert(text::ToLower(other.id));
    }
    if (candidates.size() != 1) return false;
    for (const auto& other : inputs.running) {
        const auto otherAumid = text::Trim(other.appUserModelId);
        if (otherAumid.empty() || text::EqualsIgnoreCase(otherAumid, aumid)) continue;
        const auto path = apps::NormalizeExecutablePath(other.executablePath);
        if (path && text::EqualsIgnoreCase(*path, *pinnedPath)) return false;
    }
    return true;
}

}  // namespace

Result<std::vector<DockItem>> BuildDockItems(const settings::DockSettings& settings,
                                           const std::vector<apps::RunningApplication>& running) {
    return BuildDockItems(DockModelInputs{settings, running, {}});
}

Result<std::vector<DockItem>> BuildDockItems(const DockModelInputs& inputs) {
    try {
        const auto& settings = inputs.settings;
        const auto running = inputs.running;
        std::vector<DockItem> items;
        if (settings.showStartButton) {
            DockItem start; start.kind = DockItemKind::Start; start.id = L"aether:start";
            start.displayName = L"Start"; items.push_back(std::move(start));
        }
        DockItem search;
        search.id = L"aether:search"; search.displayName = L"Search applications";
        items.push_back(std::move(search));
        if (settings.showTaskViewButton) {
            DockItem taskView; taskView.kind = DockItemKind::TaskView; taskView.id = L"aether:task-view";
            taskView.displayName = L"Task View"; items.push_back(std::move(taskView));
        }
        const auto fixedCount = items.size();
        const apps::ApplicationIndex emptyCatalog;
        const auto& catalog = inputs.catalog ? *inputs.catalog : emptyCatalog;
        std::set<std::wstring> used;
        for (const auto& pin : settings.pinned) {
            if (pin.id.empty() || !used.insert(text::ToLower(pin.id)).second) continue;
            DockItem item;
            item.kind = DockItemKind::Pinned; item.id = pin.id;
            item.displayName = pin.displayName.empty() ? L"Application" : pin.displayName;
            item.application = FromPin(pin);
            if (const auto* match = catalog.FindByAumid(pin.appUserModelId)) item.application.iconResource = match->iconResource;
            for (const auto& app : running) {
                // A generic executable pin must not swallow distinct profile groups merely because
                // they share a browser binary. An explicit window identity requires an exact match.
                const auto pinnedAumid = text::Trim(item.application.appUserModelId);
                const auto runningAumid = text::Trim(app.appUserModelId);
                const bool unidentifiedDesktop = MatchesUnidentifiedDesktop(pin, app, inputs, catalog);
                bool legacyExplorer = false;
                if (pinnedAumid.empty() && text::EqualsIgnoreCase(runningAumid, apps::kFileExplorerAumid) &&
                    item.application.arguments.empty()) {
                    const auto pinnedPath = apps::NormalizeExecutablePath(item.application.executablePath);
                    const auto runningPath = apps::NormalizeExecutablePath(app.executablePath);
                    legacyExplorer = pinnedPath && runningPath && text::EqualsIgnoreCase(*pinnedPath, *runningPath);
                }
                if ((!pinnedAumid.empty() || !runningAumid.empty()) && !text::EqualsIgnoreCase(item.id, app.id) &&
                    !legacyExplorer && !unidentifiedDesktop &&
                    (pinnedAumid.empty() || runningAumid.empty() || !text::EqualsIgnoreCase(pinnedAumid, runningAumid))) continue;
                if (!legacyExplorer && !unidentifiedDesktop && !apps::RefersToSameApplication(item.application, app)) continue;
                if (!app.relaunchIconResource.empty() && apps::ParseIconResource(app.relaunchIconResource))
                    item.application.iconResource = app.relaunchIconResource; // presentation only; never replace the pin's launch route
                item.running = true; item.foreground = item.foreground || app.foreground;
                item.attentionRequested = item.attentionRequested || app.attentionRequested;
                item.windowCount += app.windows.size();
                if (!item.runningApplication) item.runningApplication = app;
                else item.runningApplication->windows.insert(item.runningApplication->windows.end(), app.windows.begin(), app.windows.end());
                used.insert(text::ToLower(app.id));
            }
            items.push_back(std::move(item));
        }
        if (settings.showRunningApps) {
            for (const auto& app : running) {
                if (app.id.empty()) continue;
                if (!used.insert(text::ToLower(app.id)).second) {
                    const auto existing = std::find_if(items.begin(), items.end(), [&](const auto& item) {
                        return item.kind == DockItemKind::Running && text::EqualsIgnoreCase(item.id, app.id);
                    });
                    if (existing != items.end()) {
                        existing->foreground = existing->foreground || app.foreground;
                        existing->attentionRequested = existing->attentionRequested || app.attentionRequested;
                        existing->windowCount += app.windows.size();
                        existing->runningApplication->foreground = existing->foreground;
                        existing->runningApplication->windows.insert(existing->runningApplication->windows.end(), app.windows.begin(), app.windows.end());
                    }
                    continue;
                }
                DockItem item;
                item.kind = DockItemKind::Running; item.id = app.id;
                item.displayName = app.displayName.empty() ? L"Running application" : app.displayName;
                item.application = apps::ResolveRunningPresentation(app, catalog);
                item.runningApplication = app; item.running = true; item.foreground = app.foreground;
                item.attentionRequested = app.attentionRequested;
                item.windowCount = app.windows.size();
                items.push_back(std::move(item));
            }
        }
        for (auto& item : items) {
            // A group in front has been acknowledged, even if another duplicate input requested it.
            item.attentionRequested = item.running && !item.foreground && item.attentionRequested;
            if (item.runningApplication) {
                item.runningApplication->foreground = item.foreground;
                item.runningApplication->attentionRequested = item.attentionRequested;
            }
        }
        if (!inputs.sessionOrder.empty()) {
            std::map<std::wstring, std::size_t> positions;
            for (std::size_t i = 0; i < inputs.sessionOrder.size(); ++i)
                positions.try_emplace(text::ToLower(inputs.sessionOrder[i]), i);
            const auto rank = [&](const DockItem& item) {
                const auto found = positions.find(text::ToLower(item.id));
                return found == positions.end() ? inputs.sessionOrder.size() : found->second;
            };
            std::stable_sort(items.begin() + static_cast<std::ptrdiff_t>(fixedCount), items.end(),
                [&](const DockItem& a, const DockItem& b) { return rank(a) < rank(b); });
        }
        return items;
    } catch (...) { return MakeError(ErrorCode::Internal, "Dock model could not be constructed"); }
}

Result<FittedDock> FitDockItems(const std::vector<DockItem>& items, const settings::DockSettings& settings,
                               float availableWidthDip) {
    try {
        if (items.empty() || !std::isfinite(availableWidthDip) || availableWidthDip < 56 || !std::isfinite(settings.magnification)) {
            return MakeError(ErrorCode::InvalidArgument, "Dock work area is too narrow");
        }
        FittedDock result; result.items = items;
        const float spacing = static_cast<float>(std::clamp(settings.iconSpacing, 0, 32));
        const float magnification = static_cast<float>(std::clamp(settings.magnification, 1.0, 2.0));
        result.magnification = magnification;
        result.iconSize = static_cast<float>(std::clamp(settings.iconSize, 32, 96));
        while (result.iconSize > 32 && Width(result.items.size(), result.iconSize, spacing, magnification) > availableWidthDip) {
            result.iconSize -= 1;
        }
        if (Width(result.items.size(), result.iconSize, spacing, magnification) <= availableWidthDip) return result;
        // Running entries trail pins, so trimming the tail first preserves every pin where possible.
        while (result.items.size() > 1 && Width(result.items.size() + 1, result.iconSize, spacing, magnification) > availableWidthDip) {
            auto removed = std::prev(result.items.end());
            if (removed->kind == DockItemKind::Search) removed = result.items.begin();
            result.overflow.insert(result.overflow.begin(), std::move(*removed));
            result.items.erase(removed);
        }
        if (!result.overflow.empty() && Width(result.items.size() + 1, result.iconSize, spacing, magnification) <= availableWidthDip) {
            DockItem more; more.kind = DockItemKind::Overflow; more.id = L"aether:overflow";
            more.displayName = L"More applications";
            more.attentionRequested = std::any_of(result.overflow.begin(), result.overflow.end(),
                [](const auto& item) { return item.attentionRequested; });
            result.items.push_back(std::move(more));
        } else if (!result.overflow.empty()) {
            // Extreme width exposes the same application menu from Search's context menu.
            // Keep a visible/static cue for a real hidden request without changing any action.
            const auto search = std::find_if(result.items.begin(), result.items.end(),
                [](const auto& item) { return item.kind == DockItemKind::Search; });
            if (search != result.items.end()) search->attentionRequested =
                std::any_of(result.overflow.begin(), result.overflow.end(), [](const auto& item) { return item.attentionRequested; });
        }
        const float count = static_cast<float>(result.items.size());
        const float baseWidth = 24 + count * result.iconSize + std::max(0.0f, count - 1) * spacing;
        result.magnification = std::clamp(1 + (availableWidthDip - baseWidth) / (3 * result.iconSize), 1.0f, magnification);
        // At extreme widths only Search remains; its context menu still exposes the overflow.
        return result;
    } catch (...) { return MakeError(ErrorCode::Internal, "Dock items could not be fitted"); }
}

Result<settings::PinnedApp> PinFromApplication(const apps::ApplicationInfo& app) {
    try {
        if (app.id.empty() || text::ToLower(app.id).starts_with(L"window:") || !apps::HasLaunchRoute(app))
            return MakeError(ErrorCode::Unsupported, "application has no persistent launch route");
        settings::PinnedApp pin;
        pin.id = app.id; pin.displayName = app.displayName; pin.parsingName = app.parsingName;
        pin.appUserModelId = app.appUserModelId; pin.executablePath = app.executablePath;
        pin.shortcutPath = app.shortcutPath; pin.arguments = app.arguments;
        return pin;
    } catch (...) { return MakeError(ErrorCode::Internal, "application could not be pinned"); }
}
Result<std::vector<settings::PinnedApp>> PinApplication(const std::vector<settings::PinnedApp>& pins,
                                                      const apps::ApplicationInfo& app) {
    try {
        auto result = pins;
        for (const auto& pin : pins) if (text::EqualsIgnoreCase(pin.id, app.id)) return result;
        if (pins.size() >= 64) return MakeError(ErrorCode::InvalidState, "Dock supports at most 64 pinned applications");
        auto pin = PinFromApplication(app); if (!pin) return pin.error();
        result.push_back(std::move(*pin)); return result;
    } catch (...) { return MakeError(ErrorCode::Internal, "application could not be pinned"); }
}
Result<std::vector<settings::PinnedApp>> UnpinApplication(const std::vector<settings::PinnedApp>& pins,
                                                        std::wstring_view id) {
    try {
        auto result = pins;
        std::erase_if(result, [&](const auto& pin) { return text::EqualsIgnoreCase(pin.id, id); });
        return result;
    } catch (...) { return MakeError(ErrorCode::Internal, "application could not be unpinned"); }
}
Result<std::vector<settings::PinnedApp>> MovePin(const std::vector<settings::PinnedApp>& pins,
                                               std::wstring_view id, std::size_t destination) {
    try {
        auto result = pins;
        const auto found = std::find_if(result.begin(), result.end(), [&](const auto& pin) { return text::EqualsIgnoreCase(pin.id, id); });
        if (found == result.end()) return MakeError(ErrorCode::NotFound, "pinned application was not found");
        if (destination >= result.size()) return MakeError(ErrorCode::InvalidArgument, "pin destination is outside the Dock");
        auto pin = std::move(*found); result.erase(found);
        result.insert(result.begin() + static_cast<std::ptrdiff_t>(destination), std::move(pin)); return result;
    } catch (...) { return MakeError(ErrorCode::Internal, "application could not be reordered"); }
}

Result<std::vector<std::wstring>> AlignSessionPins(std::span<const std::wstring> sessionOrder,
                                                  std::span<const settings::PinnedApp> pins) {
    try {
        std::vector<std::wstring> result(sessionOrder.begin(), sessionOrder.end());
        if (result.empty() || pins.empty()) return result;
        std::vector<std::size_t> slots;
        for (std::size_t i = 0; i < result.size(); ++i)
            if (std::any_of(pins.begin(), pins.end(), [&](const auto& pin) { return text::EqualsIgnoreCase(pin.id, result[i]); })) slots.push_back(i);
        for (std::size_t i = 0; i < std::min(slots.size(), pins.size()); ++i) result[slots[i]] = pins[i].id;
        const auto insertion = slots.empty() ? 0 : slots.back() + 1;
        for (std::size_t i = slots.size(); i < pins.size(); ++i)
            result.insert(result.begin() + static_cast<std::ptrdiff_t>(insertion + i - slots.size()), pins[i].id);
        return result;
    } catch (...) { return MakeError(ErrorCode::Internal, "Dock session pin order could not be aligned"); }
}

Result<std::vector<settings::PinnedApp>> ReorderDockApplications(
    const settings::DockSettings& settings, const std::vector<apps::RunningApplication>& running,
    std::wstring_view draggedId, std::wstring_view destinationId) {
    auto reordered = ReorderDockItems(DockModelInputs{settings, running, {}}, draggedId, destinationId);
    if (!reordered) return reordered.error();
    return std::move(reordered->pins);
}

Result<DockReorderResult> ReorderDockItems(const DockModelInputs& inputs,
                                         std::wstring_view draggedId, std::wstring_view destinationId) {
    try {
        auto model = BuildDockItems(inputs); if (!model) return model.error();
        std::erase_if(*model, [](const DockItem& item) { return !IsReorderable(item.kind); });
        const auto find = [&](std::wstring_view id) {
            return std::find_if(model->begin(), model->end(), [&](const DockItem& item) {
                return text::EqualsIgnoreCase(item.id, id);
            });
        };
        const auto dragged = find(draggedId), target = find(destinationId);
        if (dragged == model->end() || target == model->end())
            return MakeError(ErrorCode::NotFound, "application is no longer available for reordering");
        DockReorderResult result;
        result.pins = inputs.settings.pinned;
        const bool movingPin = dragged->kind == DockItemKind::Pinned;
        const auto from = static_cast<std::size_t>(dragged - model->begin());
        const auto to = static_cast<std::size_t>(target - model->begin());
        DockItem moving = std::move((*model)[from]);
        model->erase(model->begin() + from); model->insert(model->begin() + to, std::move(moving));
        for (const auto& item : *model) result.sessionOrder.push_back(item.id);
        if (movingPin) {
            result.pins.clear();
            for (const auto& item : *model) {
                if (item.kind != DockItemKind::Pinned) continue;
                const auto existing = std::find_if(inputs.settings.pinned.begin(), inputs.settings.pinned.end(), [&](const auto& pin) {
                return text::EqualsIgnoreCase(pin.id, item.id);
                });
                if (existing != inputs.settings.pinned.end()) result.pins.push_back(*existing);
            }
            for (std::size_t i = 0; i < result.pins.size(); ++i)
                if (!text::EqualsIgnoreCase(result.pins[i].id, inputs.settings.pinned[i].id)) result.pinsChanged = true;
        }
        return result;
    } catch (...) { return MakeError(ErrorCode::Internal, "Dock application could not be reordered"); }
}

}  // namespace aether::dock

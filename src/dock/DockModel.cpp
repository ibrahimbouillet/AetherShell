// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
#include "dock/DockModel.h"

#include "applications/ApplicationIdentity.h"
#include "core/Strings.h"

#include <algorithm>
#include <cmath>
#include <set>

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

}  // namespace

Result<std::vector<DockItem>> BuildDockItems(const settings::DockSettings& settings,
                                           const std::vector<apps::RunningApplication>& running) {
    try {
        std::vector<DockItem> items;
        DockItem search;
        search.id = L"aether:search"; search.displayName = L"Search applications";
        items.push_back(std::move(search));
        std::set<std::wstring> used;
        for (const auto& pin : settings.pinned) {
            if (pin.id.empty() || !used.insert(text::ToLower(pin.id)).second) continue;
            DockItem item;
            item.kind = DockItemKind::Pinned; item.id = pin.id;
            item.displayName = pin.displayName.empty() ? L"Application" : pin.displayName;
            item.application = FromPin(pin);
            for (const auto& app : running) {
                if (!apps::RefersToSameApplication(item.application, app)) continue;
                item.running = true; item.foreground = item.foreground || app.foreground;
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
                        existing->windowCount += app.windows.size();
                        existing->runningApplication->foreground = existing->foreground;
                        existing->runningApplication->windows.insert(existing->runningApplication->windows.end(), app.windows.begin(), app.windows.end());
                    }
                    continue;
                }
                DockItem item;
                item.kind = DockItemKind::Running; item.id = app.id;
                item.displayName = app.displayName.empty() ? L"Running application" : app.displayName;
                item.application.id = app.id; item.application.displayName = item.displayName;
                item.application.executablePath = app.executablePath; item.application.appUserModelId = app.appUserModelId;
                item.application.kind = apps::IsPackagedAumid(app.appUserModelId) ? apps::ApplicationKind::Packaged
                    : app.executablePath.empty() ? apps::ApplicationKind::Other : apps::ApplicationKind::Desktop;
                item.runningApplication = app; item.running = true; item.foreground = app.foreground;
                item.windowCount = app.windows.size();
                items.push_back(std::move(item));
            }
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
            result.overflow.insert(result.overflow.begin(), std::move(result.items.back()));
            result.items.pop_back();
        }
        if (!result.overflow.empty() && Width(result.items.size() + 1, result.iconSize, spacing, magnification) <= availableWidthDip) {
            DockItem more; more.kind = DockItemKind::Overflow; more.id = L"aether:overflow";
            more.displayName = L"More applications"; result.items.push_back(std::move(more));
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
        if (app.id.empty() || text::ToLower(app.id).starts_with(L"window:")) return MakeError(ErrorCode::Unsupported, "application has no persistent launch identity");
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

Result<std::vector<settings::PinnedApp>> ReorderDockApplications(
    const settings::DockSettings& settings, const std::vector<apps::RunningApplication>& running,
    std::wstring_view draggedId, std::wstring_view destinationId) {
    try {
        auto model = BuildDockItems(settings, running); if (!model) return model.error();
        std::erase_if(*model, [](const DockItem& item) {
            return item.kind != DockItemKind::Pinned && item.kind != DockItemKind::Running;
        });
        const auto find = [&](std::wstring_view id) {
            return std::find_if(model->begin(), model->end(), [&](const DockItem& item) {
                return text::EqualsIgnoreCase(item.id, id);
            });
        };
        const auto dragged = find(draggedId), target = find(destinationId);
        if (dragged == model->end() || target == model->end())
            return MakeError(ErrorCode::NotFound, "application is no longer available for reordering");
        if (dragged == target) return settings.pinned;
        const auto from = static_cast<std::size_t>(dragged - model->begin());
        const auto to = static_cast<std::size_t>(target - model->begin());
        DockItem moving = std::move((*model)[from]);
        model->erase(model->begin() + from); model->insert(model->begin() + to, std::move(moving));
        std::size_t prefix = to + 1;
        for (std::size_t index = 0; index < model->size(); ++index)
            if ((*model)[index].kind == DockItemKind::Pinned) prefix = std::max(prefix, index + 1);
        if (prefix > 64) return MakeError(ErrorCode::InvalidState, "Dock supports at most 64 pinned applications");
        std::vector<settings::PinnedApp> pins;
        pins.reserve(prefix);
        for (std::size_t index = 0; index < prefix; ++index) {
            const auto& item = (*model)[index];
            const auto existing = std::find_if(settings.pinned.begin(), settings.pinned.end(), [&](const auto& pin) {
                return text::EqualsIgnoreCase(pin.id, item.id);
            });
            if (existing != settings.pinned.end()) pins.push_back(*existing);
            else {
                auto pin = PinFromApplication(item.application); if (!pin) return pin.error();
                pins.push_back(std::move(*pin));
            }
        }
        return pins;
    } catch (...) { return MakeError(ErrorCode::Internal, "Dock application could not be reordered"); }
}

}  // namespace aether::dock

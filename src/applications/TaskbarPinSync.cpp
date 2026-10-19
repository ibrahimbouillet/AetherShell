// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
#include "applications/TaskbarPinSync.h"

#include "applications/ApplicationIdentity.h"
#include "core/Strings.h"

#include <algorithm>

namespace aether::apps {
namespace {

PinIdentity IdentityOf(const ApplicationInfo& app) {
    return PinIdentity{app.id, app.appUserModelId, app.executablePath, app.arguments};
}

bool SameExecutableId(std::wstring_view left, std::wstring_view right) {
    if (text::Trim(left).empty() || text::Trim(right).empty()) return false;
    const auto a = MakeIdForExecutable(left);
    const auto b = MakeIdForExecutable(right);
    return a && b && text::EqualsIgnoreCase(a.value(), b.value());
}

// The dismissed id that refers to `app`, when any: the same id, or the executable/AUMID id that
// `app` would have had in another form (a pin dismissed as "exe:..." and imported again with an
// AUMID is still the application the user removed).
const std::wstring* FindDismissed(const ApplicationInfo& app, const std::vector<std::wstring>& dismissedIds) {
    for (const auto& dismissed : dismissedIds) {
        if (dismissed.empty()) continue;
        if (text::EqualsIgnoreCase(dismissed, app.id)) return &dismissed;
        switch (GetIdScheme(dismissed)) {
            case IdScheme::Executable:
                if (text::Trim(app.arguments).empty() && !app.executablePath.empty()) {
                    if (auto id = MakeIdForExecutable(app.executablePath); id && text::EqualsIgnoreCase(id.value(), dismissed)) {
                        return &dismissed;
                    }
                }
                break;
            case IdScheme::Aumid:
                if (!text::Trim(app.appUserModelId).empty()) {
                    if (auto id = MakeIdForAumid(app.appUserModelId); id && text::EqualsIgnoreCase(id.value(), dismissed)) {
                        return &dismissed;
                    }
                }
                break;
            default:
                break;
        }
    }
    return nullptr;
}

}  // namespace

bool SameLaunchIdentity(const PinIdentity& pin, const ApplicationInfo& app) {
    if (!text::Trim(pin.id).empty() && text::EqualsIgnoreCase(text::Trim(pin.id), text::Trim(app.id))) return true;
    if (text::Trim(pin.arguments) != text::Trim(app.arguments)) return false;
    const auto pinAumid = text::Trim(pin.appUserModelId);
    const auto appAumid = text::Trim(app.appUserModelId);
    if (!pinAumid.empty() && !appAumid.empty()) return text::EqualsIgnoreCase(pinAumid, appAumid);
    return SameExecutableId(pin.executablePath, app.executablePath);
}

Result<PinSyncPlan> PlanTaskbarPinSync(const PinSyncInput& input) {
    if (input.maxPins > kMaxDockPins) return MakeError(ErrorCode::InvalidArgument, "the Dock pin limit is out of range");
    try {
        PinSyncPlan plan;
        const auto inDock = [&](const ApplicationInfo& app) {
            return std::any_of(input.existing.begin(), input.existing.end(),
                               [&](const PinIdentity& pin) { return SameLaunchIdentity(pin, app); });
        };

        if (input.fileExplorer && !input.explorerDefaultApplied) {
            const auto& explorer = *input.fileExplorer;
            const bool inWindowsPins =
                std::any_of(input.windowsPins.begin(), input.windowsPins.end(),
                            [&](const ApplicationInfo& pin) { return SameLaunchIdentity(IdentityOf(pin), explorer); });
            if (inDock(explorer) || inWindowsPins || FindDismissed(explorer, input.dismissedIds)) {
                plan.markExplorerApplied = true;  // offered: already there, or the user removed it
            } else if (input.existing.size() < input.maxPins && !explorer.id.empty()) {
                plan.explorerAtFront = explorer;
                plan.markExplorerApplied = true;
            }
            // At the limit nothing is marked, so the default is offered again once there is room.
        }

        const std::size_t used = input.existing.size() + (plan.explorerAtFront ? 1u : 0u);
        const std::size_t capacity = used >= input.maxPins ? 0 : input.maxPins - used;
        for (const auto& pin : input.windowsPins) {
            if (text::Trim(pin.id).empty()) {
                ++plan.skippedInvalid;
                continue;
            }
            const bool repeated =
                inDock(pin) ||
                std::any_of(plan.append.begin(), plan.append.end(),
                            [&](const ApplicationInfo& added) { return SameLaunchIdentity(IdentityOf(added), pin); }) ||
                (plan.explorerAtFront && SameLaunchIdentity(IdentityOf(*plan.explorerAtFront), pin));
            if (repeated) {
                ++plan.skippedExisting;
                continue;
            }
            const std::wstring* dismissed = FindDismissed(pin, input.dismissedIds);
            if (dismissed && !input.ignoreDismissed) {
                ++plan.skippedDismissed;
                continue;
            }
            if (plan.append.size() >= capacity) {
                ++plan.skippedLimit;
                continue;
            }
            plan.append.push_back(pin);
            if (dismissed) plan.reinstatedIds.push_back(*dismissed);
        }
        plan.importedCount = plan.append.size();
        return plan;
    } catch (...) {
        return MakeError(ErrorCode::Internal, "the taskbar pin sync could not be planned");
    }
}

}  // namespace aether::apps

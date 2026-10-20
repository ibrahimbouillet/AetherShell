// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
//
// Additive synchronization of Windows taskbar pins into the Dock's pins. Worker-thread only:
// executable identity comparison may resolve 8.3 paths through the file system. The app owns the
// trigger, the settings write and the user feedback.
//
//  * Existing Dock pins are never reordered or removed: pins removed from the Windows taskbar stay.
//  * Windows pins that are not in the Dock yet are appended in Windows order.
//  * Pins the user removed in Aether (dock.dismissedPins) are never re-added automatically; a manual
//    import is an explicit request and reinstates them (reported in reinstatedIds).
//  * The default File Explorer pin is offered once (dock.explorerDefaultApplied): at the front, unless
//    the Dock or the Windows pins already contain File Explorer, or the user dismissed it.
//  * The pin limit is enforced and what it cut is reported. Running the plan again adds nothing.
#pragma once

#include "applications/ApplicationTypes.h"
#include "core/Result.h"

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace aether::apps {

inline constexpr std::size_t kMaxDockPins = 64;

// The launch identity of an existing Dock pin (settings::PinnedApp fields).
struct PinIdentity {
    std::wstring id;
    std::wstring appUserModelId;
    std::wstring executablePath;
    std::wstring arguments;
};

// Whether `pin` and `app` start the same application, so `app` must not be added again:
//  * the ids are equal, ignoring case; or
//  * both carry an AppUserModelID: equal AUMIDs (ignoring case) with equal trimmed arguments (two
//    AUMIDs of one executable are distinct applications, for example browser profiles); or
//  * at most one carries an AppUserModelID: equal executable ids (MakeIdForExecutable, which may
//    expand an 8.3 name through the file system) with equal trimmed arguments.
// Worker-thread only; no Windows pin changes or launches.
[[nodiscard]] bool SameLaunchIdentity(const PinIdentity& pin, const ApplicationInfo& app);

struct PinSyncInput {
    std::vector<PinIdentity> existing;         // current Dock pins, in Dock order
    std::vector<ApplicationInfo> windowsPins;  // DiscoverTaskbarPinSet().pins, in Windows order
    std::vector<std::wstring> dismissedIds;    // dock.dismissedPins
    bool ignoreDismissed = false;              // true for a manual import
    std::optional<ApplicationInfo> fileExplorer;  // MakeFileExplorerApplication(), when found
    bool explorerDefaultApplied = false;          // dock.explorerDefaultApplied
    std::size_t maxPins = kMaxDockPins;
};

struct PinSyncPlan {
    std::vector<ApplicationInfo> append;           // to append after the existing pins, in this order
    std::optional<ApplicationInfo> explorerAtFront;  // to insert at index 0
    std::size_t importedCount = 0;                 // append.size()
    std::size_t skippedDismissed = 0;              // Windows pins the user had removed (automatic sync)
    std::size_t skippedExisting = 0;               // already in the Dock (or repeated in the Windows list)
    std::size_t skippedLimit = 0;                  // cut by maxPins
    std::size_t skippedInvalid = 0;                // Windows pins without an id
    bool markExplorerApplied = false;              // set dock.explorerDefaultApplied
    std::vector<std::wstring> reinstatedIds;       // dismissed ids re-added by a manual import:
                                                   // remove them from dock.dismissedPins
};

// Worker-thread only: snapshot the settings on the UI thread, plan off-thread, and apply only if that
// snapshot is still current (otherwise replan). InvalidArgument when maxPins exceeds kMaxDockPins;
// Internal only when memory runs out. No Windows pin changes or launches.
[[nodiscard]] Result<PinSyncPlan> PlanTaskbarPinSync(const PinSyncInput& input);

}  // namespace aether::apps

// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
#pragma once

#include "applications/ApplicationTypes.h"
#include "applications/TaskbandScan.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace aether::apps {

struct TaskbarPinImportOptions {
    // Empty: FOLDERID_UserPinned / TaskBar. Overrides let isolated tests use only their own files.
    std::filesystem::path directory;
    std::size_t maxShortcutFiles = 512;
    // Optional already-discovered catalog. Reuses a matching identity without losing pin arguments,
    // and decides which packaged AppUserModelIDs of the Windows pin list are installed. Without a
    // catalog, packaged AUMIDs are confirmed through the Apps folder instead.
    std::vector<ApplicationInfo> catalog;
    // Read the Windows pin list (Taskband "Favorites") for Windows order and packaged pins.
    bool useTaskbandOrder = true;
    // Test seam for the pin list. The real HKCU reader is used only when `directory` is empty and no
    // reader is set, so tests with a directory override never read the registry.
    std::function<Result<std::vector<std::uint8_t>>()> taskbandReader;
    std::size_t maxTaskbandBytes = kDefaultMaxTaskbandBytes;
    // Empty: the system Windows directory (File Explorer's canonical executable). Test override.
    std::wstring windowsDirectory;
};

struct TaskbarPinSet {
    std::vector<ApplicationInfo> pins;  // Windows order when windowsOrder, else file-name order
    bool windowsOrder = false;          // the pin list matched at least one shortcut or packaged pin
    std::size_t fromTaskband = 0;       // packaged pins found only in the pin list
    std::size_t pidlShortcuts = 0;      // shortcuts that point to an Apps-folder item by ID list
    std::size_t skipped = 0;            // unreadable or unsupported shortcuts and unresolved packaged pins
};

enum class TaskbandSource {
    None,      // useTaskbandOrder is false, or a directory override without an injected reader
    Injected,  // options.taskbandReader
    Registry,  // HKCU ...\Explorer\Taskband "Favorites", read-only
};

// Pure: which pin-list source DiscoverTaskbarPinSet uses for `options`.
[[nodiscard]] TaskbandSource SelectTaskbandSource(const TaskbarPinImportOptions& options) noexcept;

// Blocking, worker-thread only. Reads local .lnk definitions without resolving, updating or launching
// them, and (see SelectTaskbandSource) one read-only registry value. No folder creation, registry
// write or Windows pin modification. A missing folder is empty; unreadable folders return an error.
// Malformed, URL, document and unsupported shortcuts are skipped.
//  * Shortcuts whose target is an Apps-folder item given only by ID list (no file target) are read
//    through that Apps-folder item and classified like a catalog entry; the .lnk stays attached.
//  * The Windows pin list is scanned without interpretation (TaskbandScan.h): shortcuts it names come
//    first in Windows order, the rest follow in file-name order; installed packaged pins found only in
//    the list are inserted at their position. A missing, unreadable or oversized list degrades to
//    file-name order without error.
//  * File Explorer forms are canonicalized (KnownApplications.h), then identities are deduplicated.
// Best effort: Windows 11 has no API that enumerates every pin. Existing catalog identities are
// reused only for an unambiguous match. The caller owns persistence and user-visible feedback; only
// counts are logged, never names, paths, AUMIDs or the pin list itself.
[[nodiscard]] Result<TaskbarPinSet> DiscoverTaskbarPinSet(const CancellationToken& cancel,
                                                          const TaskbarPinImportOptions& options = {}) noexcept;

// DiscoverTaskbarPinSet(cancel, options).pins.
[[nodiscard]] Result<std::vector<ApplicationInfo>> DiscoverTaskbarPins(
    const CancellationToken& cancel, const TaskbarPinImportOptions& options = {}) noexcept;

}  // namespace aether::apps

// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
#pragma once

#include "applications/ApplicationTypes.h"

#include <cstddef>
#include <filesystem>

namespace aether::apps {

struct TaskbarPinImportOptions {
    // Empty: FOLDERID_UserPinned / TaskBar. Overrides let isolated tests use only their own files.
    std::filesystem::path directory;
    std::size_t maxShortcutFiles = 512;
    // Optional already-discovered catalog. Reuses a matching identity without losing pin arguments.
    std::vector<ApplicationInfo> catalog;
};

// Blocking, worker-thread only. Reads local .lnk definitions without resolving, updating or launching
// them. No folder creation, registry access or Windows pin modification. A missing folder is empty;
// unreadable folders return an error. Malformed, URL, document and unsupported shortcuts are skipped.
//
// Best effort: this folder does not expose every Windows 11/package pin or Windows taskbar order.
// Results use deterministic filename order, deduplicated application identities and complete launch
// metadata. Existing catalog identities are reused only for an unambiguous match. The caller owns
// persistence and any user-visible import feedback; no user application names or paths are logged.
[[nodiscard]] Result<std::vector<ApplicationInfo>> DiscoverTaskbarPins(
    const CancellationToken& cancel, const TaskbarPinImportOptions& options = {}) noexcept;

}  // namespace aether::apps

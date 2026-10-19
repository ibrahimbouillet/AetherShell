// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
//
// How a running application that is not pinned is presented and launched: its running facts
// reconciled with the discovered catalog, without ever changing which application (or which
// browser profile) it is. Pure: no shell, registry or file-system access, so it may run on the UI
// thread.
//
// Running groups are formed by the window tracker (one group per window AppUserModelID, so each
// browser profile that sets its own AUMID is its own group). This module only decides the
// launch and icon metadata of such a group.
#pragma once

#include "applications/ApplicationTypes.h"

#include <cstddef>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace aether::apps {

// Catalog lookups by AppUserModelID and by executable, built once from the discovered catalog.
// Keys ignore case. Executables are compared after lexical normalization only (no 8.3 expansion, so
// building and querying never touch the file system).
class ApplicationIndex {
public:
    ApplicationIndex() = default;
    explicit ApplicationIndex(std::vector<ApplicationInfo> catalog);

    // The entry whose packaged or explicit AppUserModelID equals `aumid` (trimmed); the first one
    // when several share it. Null when none.
    [[nodiscard]] const ApplicationInfo* FindByAumid(std::wstring_view aumid) const;
    // The only entry that starts `executablePath` without arguments. Null when there is none or
    // more than one.
    [[nodiscard]] const ApplicationInfo* FindByExecutable(std::wstring_view executablePath) const;
    // Whether some entry starts `executablePath` (with or without arguments) under an
    // AppUserModelID other than `aumid`.
    [[nodiscard]] bool HasExecutableWithOtherAumid(std::wstring_view executablePath, std::wstring_view aumid) const;

    [[nodiscard]] std::size_t size() const noexcept { return entries_.size(); }
    [[nodiscard]] bool empty() const noexcept { return entries_.empty(); }

private:
    std::vector<ApplicationInfo> entries_;
    std::unordered_map<std::wstring, std::size_t> byAumid_;                   // lower-case AUMID -> entry
    std::unordered_map<std::wstring, std::vector<std::size_t>> byExecutable_;  // lower-case path -> entries
};

// The ApplicationInfo the Dock uses to launch, pin and draw an unpinned running group. `id` is
// always `running.id`, and the display name is the running one (the catalog's when empty).
//  (a) An explicit or packaged window AppUserModelID with an exact catalog match copies that entry's
//      launch and icon metadata (kind, parsing name, shortcut, arguments, executable).
//  (b) Without an AppUserModelID, exactly one catalog entry that starts the same executable without
//      arguments is copied the same way.
//  (c) Otherwise the result is built from the running facts; a packaged AUMID is launchable through
//      the Apps folder. The window's relaunch icon resource becomes `iconResource` only here, when
//      the AUMID is explicit, and only in the supported form (ParseIconResource).
//  (d) Safety: under (c), when the AUMID is explicit and the catalog starts the same executable under
//      another AUMID, the executable is cleared, so no bare-executable launch can open another
//      profile of the same browser. HasLaunchRoute is then false unless another route remains.
[[nodiscard]] ApplicationInfo ResolveRunningPresentation(const RunningApplication& running,
                                                         const ApplicationIndex& index);

// Whether the launcher has a route for `app`: an Apps-folder parsing name, a packaged AUMID, a
// shortcut, or an executable without arguments.
[[nodiscard]] bool HasLaunchRoute(const ApplicationInfo& app) noexcept;

// Short stable fingerprint (16 lower-case hex digits, FNV-1a 64 over the case-folded kind, parsing
// name, AUMID, shortcut, executable and icon resource) of everything icon extraction depends on.
// Icon cache keys that include it are refreshed when richer presentation metadata arrives later.
[[nodiscard]] std::wstring IconSourceFingerprint(const ApplicationInfo& app);

}  // namespace aether::apps

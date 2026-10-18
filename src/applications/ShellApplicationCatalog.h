// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
//
// IApplicationCatalog backed by the Windows shell:
//   1. shell:AppsFolder (FOLDERID_AppsFolder), the list behind Start's "All apps": packaged apps,
//      desktop shortcuts and a few shell objects (File Explorer, Control Panel...).
//   2. The Start-menu Programs folders (FOLDERID_CommonPrograms, FOLDERID_Programs), whose .lnk
//      files enrich Apps-folder entries with their shortcut path and add the shortcuts the Apps
//      folder does not list.
// Filtering and merging follow CatalogRules.h.
//
// Threading: DiscoverApplications blocks (typically 0.2-2 s) and must run on a worker thread. The
// work runs on a COM single-threaded apartment: inline when the calling thread already is an STA,
// after initializing one (and uninitializing it before returning) when COM is not initialized on the
// calling thread, and on a temporary STA thread when the caller lives in the MTA.
#pragma once

#include "applications/ApplicationTypes.h"

#include <cstddef>

namespace aether::apps {

class ShellApplicationCatalog final : public IApplicationCatalog {
public:
    struct Options {
        bool includeStartMenuShortcuts = true;
        int maxShortcutDepth = 6;              // directory levels below each Programs folder
        std::size_t maxShortcutFiles = 5000;   // .lnk files read in total
    };

    ShellApplicationCatalog() = default;
    explicit ShellApplicationCatalog(Options options) : options_(options) {}

    // Cancelled when `cancel` fires (checked between items); Unavailable when neither source could be
    // read. A failing source is logged and skipped when the other one works.
    Result<std::vector<ApplicationInfo>> DiscoverApplications(const CancellationToken& cancel) override;

private:
    Options options_;
};

}  // namespace aether::apps

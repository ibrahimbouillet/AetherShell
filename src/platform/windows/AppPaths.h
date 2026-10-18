// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
//
// Per-user writable locations. Nothing is ever written next to the program
// files: configuration, logs, caches and update staging live under
// %LOCALAPPDATA%\AetherShell (overridable for tests with --data-dir).
#pragma once

#include "core/Result.h"

#include <windows.h>

#include <filesystem>
#include <optional>

namespace aether::platform {

struct AppPaths {
    std::filesystem::path dataRoot;   // %LOCALAPPDATA%\AetherShell
    std::filesystem::path configDir;  // <dataRoot>\config   (settings.json, state.json)
    std::filesystem::path logDir;     // <dataRoot>\logs
    std::filesystem::path cacheDir;   // <dataRoot>\cache    (weather, release metadata)
    std::filesystem::path updatesDir; // <dataRoot>\updates  (downloads and staging)
    std::filesystem::path executablePath;
    std::filesystem::path executableDir;

    // Resolves (and creates) the directories. `overrideRoot` replaces %LOCALAPPDATA%\AetherShell.
    [[nodiscard]] static Result<AppPaths> Resolve(const std::optional<std::filesystem::path>& overrideRoot = std::nullopt);
};

[[nodiscard]] Result<std::filesystem::path> KnownFolder(const GUID& folderId);
[[nodiscard]] std::filesystem::path CurrentExecutablePath();

}  // namespace aether::platform

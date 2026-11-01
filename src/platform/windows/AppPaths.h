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

// The ordinary per-user data root, %LOCALAPPDATA%\AetherShell (created if missing).
[[nodiscard]] Result<std::filesystem::path> DefaultDataRoot();
// AS4-16/AS4-20: whether two absolute data roots name the same folder, compared lexically and
// case-insensitively (trailing separators and "." / ".." segments ignored). Different spellings of
// one folder (8.3 names, links) compare unequal, which callers treat as a private profile.
[[nodiscard]] bool SameDataRoot(const std::filesystem::path& first, const std::filesystem::path& second) noexcept;

}  // namespace aether::platform

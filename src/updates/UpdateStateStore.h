// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
//
// Persistent update state, <configDir>/update-state.json (schema version 1):
//
//   {"schemaVersion":1,
//    "lastCheckUtc":"2026-10-17T08:00:00Z",          last successful check (drives scheduling)
//    "etag":"W/\"...\"","cachedChannel":"stable",     validator and channel of the cached metadata
//    "cachedRelease":{...GitHub release fields...},   metadata reused on HTTP 304
//    "skippedVersion":"1.2.0"}                        version the user chose to skip
//
// Every member except schemaVersion is optional. The file is written atomically
// (fs::WriteFileAtomic). It is a cache, not a trust anchor: a cached release is
// re-validated when loaded and still has to pass the manifest signature check.
// Disk I/O: worker threads only.
#pragma once

#include "core/Result.h"
#include "updates/GitHubReleaseClient.h"
#include "updates/SemanticVersion.h"

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace aether::updates {

inline constexpr int kUpdateStateSchemaVersion = 1;
inline constexpr std::wstring_view kUpdateStateFileName = L"update-state.json";
inline constexpr std::uint64_t kMaxUpdateStateBytes = 512 * 1024;

struct PersistedUpdateState {
    std::optional<std::chrono::system_clock::time_point> lastCheckUtc;
    std::optional<ReleaseMetadataCache> cache;
    std::optional<SemanticVersion> skippedVersion;
};

[[nodiscard]] std::string SerializeUpdateState(const PersistedUpdateState& state);
// Strict: wrong types, unknown schema versions and invalid cached metadata are errors.
[[nodiscard]] Result<PersistedUpdateState> ParseUpdateState(std::string_view text, const GitHubRepository& repository);

// A missing file yields a default state; an unreadable or invalid one an error (callers start fresh).
[[nodiscard]] Result<PersistedUpdateState> LoadUpdateState(const std::filesystem::path& file,
                                                           const GitHubRepository& repository);
[[nodiscard]] Result<void> SaveUpdateState(const std::filesystem::path& file, const PersistedUpdateState& state);

}  // namespace aether::updates

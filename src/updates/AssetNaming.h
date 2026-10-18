// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
//
// Release asset naming convention (mirrored by scripts/release/release_common.py):
//
//   AetherShell-<version>-win-<arch>-setup.exe     installer, arch = x64 | arm64
//   AetherShell-<version>-manifest.json            signed release manifest (schema v1)
//   AetherShell-<version>-manifest.json.sig        base64 ECDSA P-256 signature of the manifest bytes
//   AetherShell-<version>-checksums.txt            sha256sum-compatible list (for people; not trusted here)
//
// <version> is the canonical SemVer text (SemanticVersion::ToString) without a leading "v".
#pragma once

#include "updates/SemanticVersion.h"
#include "updates/UpdateTypes.h"

#include <optional>
#include <string>
#include <string_view>

namespace aether::updates {

inline constexpr std::string_view kAssetProductPrefix = "AetherShell";
inline constexpr std::size_t kMaxAssetFileNameLength = 200;

[[nodiscard]] std::string InstallerAssetName(const SemanticVersion& version, Architecture architecture);
[[nodiscard]] std::string ManifestAssetName(const SemanticVersion& version);
[[nodiscard]] std::string SignatureAssetName(const SemanticVersion& version);
[[nodiscard]] std::string ChecksumsAssetName(const SemanticVersion& version);

// The architecture whose installer name for `version` is exactly `fileName`; nullopt otherwise.
[[nodiscard]] std::optional<Architecture> MatchInstallerAssetName(std::string_view fileName,
                                                                  const SemanticVersion& version);

// A single, portable file name: 1..kMaxAssetFileNameLength characters from [A-Za-z0-9._+-], not
// "." or "..", no "..", no leading or trailing dot. Rejects path separators, drive letters, ADS
// (':'), control characters and anything Windows would reinterpret.
[[nodiscard]] bool IsSafeAssetFileName(std::string_view fileName) noexcept;

}  // namespace aether::updates

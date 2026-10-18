// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
//
// Release manifest, schema version 1. The manifest is the document the release
// signature covers; it binds a version to the exact SHA-256 and size of each
// installer. Format (written by scripts/release/make_manifest.py):
//
//   {"schemaVersion":1,"product":"AetherShell","version":"1.2.0","channel":"stable",
//    "minimumWindowsBuild":19041,
//    "releaseNotesUrl":"https://github.com/<owner>/<repo>/releases/tag/v1.2.0",
//    "assets":[{"filename":"AetherShell-1.2.0-win-x64-setup.exe","architecture":"x64",
//               "format":"exe","sha256":"<64 lowercase hex>","size":12345678}]}
//
// Parse() must only be given bytes whose signature has already been verified,
// and validates strictly: unknown members are rejected (a new field requires a
// new schemaVersion), the version must equal the GitHub release tag, the
// channel must match the release's prerelease flag, digests must be real
// (not placeholders), sizes are bounded, and file names follow AssetNaming.h.
#pragma once

#include "core/Result.h"
#include "updates/SemanticVersion.h"
#include "updates/Sha256.h"
#include "updates/UpdateTypes.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace aether::updates {

inline constexpr int kManifestSchemaVersion = 1;
inline constexpr std::string_view kManifestProduct = "AetherShell";
inline constexpr std::size_t kMaxManifestBytes = 64 * 1024;
inline constexpr std::uint64_t kMaxInstallerBytes = 512ull * 1024 * 1024;
inline constexpr std::size_t kMaxManifestAssets = 8;
inline constexpr std::string_view kInstallerFormat = "exe";

struct ManifestAsset {
    std::string fileName;
    Architecture architecture = Architecture::X64;
    std::string format;  // "exe"
    Sha256Digest sha256{};
    std::uint64_t size = 0;
};

// What the GitHub release (already validated by GitHubReleaseClient) says about this manifest.
struct ManifestExpectations {
    SemanticVersion releaseVersion;  // parsed from the release tag
    bool prerelease = false;         // the release's prerelease flag
    GitHubRepository repository = GitHubRepository::Official();
};

struct UpdateManifest {
    int schemaVersion = kManifestSchemaVersion;
    std::string product;
    SemanticVersion version;
    UpdateChannel channel = UpdateChannel::Stable;
    std::uint32_t minimumWindowsBuild = 0;
    std::string releaseNotesUrl;  // signed, so preferred over the release's html_url for "release notes" links
    std::vector<ManifestAsset> assets;

    // Errors: Parse (not JSON / wrong types / unknown members), Validation (rule violations).
    [[nodiscard]] static Result<UpdateManifest> Parse(std::string_view bytes, const ManifestExpectations& expected);

    // The installer for `architecture`. ErrorCode::Unsupported when `osBuild` is below
    // minimumWindowsBuild or the release has no installer for that architecture.
    [[nodiscard]] Result<ManifestAsset> SelectAsset(Architecture architecture, std::uint32_t osBuild) const;
};

// True for digests that no real release asset has: a short repeating pattern (all zeros, "deadbeef"
// repeated, "0123456789abcdef" repeated, ...) or the digest of empty input. Input is 64 hex chars.
[[nodiscard]] bool IsPlaceholderDigest(std::string_view hex) noexcept;

}  // namespace aether::updates

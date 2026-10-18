// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
//
// Shared vocabulary of the update module: channels, architectures, the
// official release repository and the release metadata parsed from GitHub.
// Everything in ReleaseInfo comes from the network and is untrusted until the
// signed manifest has been verified (see docs/decisions/0004-update-trust-model.md).
#pragma once

#include "updates/SemanticVersion.h"

#include <chrono>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace aether::updates {

inline constexpr std::string_view kLogComponent = "updates";

enum class UpdateChannel { Stable, Prerelease };

[[nodiscard]] std::string_view ToString(UpdateChannel channel) noexcept;           // "stable" | "prerelease"
[[nodiscard]] std::optional<UpdateChannel> ParseChannel(std::string_view text) noexcept;
// Channel configured at build time (aether::build::kDefaultUpdateChannel); Stable if unrecognized.
[[nodiscard]] UpdateChannel DefaultChannel() noexcept;

enum class Architecture { X64, Arm64 };

[[nodiscard]] std::string_view ToString(Architecture architecture) noexcept;        // "x64" | "arm64"
[[nodiscard]] std::optional<Architecture> ParseArchitecture(std::string_view text) noexcept;
// Architecture this binary was compiled for. An x64 build running under emulation on ARM64 keeps
// updating to the x64 package; switching architectures is a reinstall decision, not an update.
[[nodiscard]] Architecture CurrentArchitecture() noexcept;

// The GitHub repository that publishes official releases. Owner and name are compared
// case-insensitively, as GitHub does.
struct GitHubRepository {
    std::string owner;
    std::string name;

    // aether::build::kUpdateRepositoryOwner / kUpdateRepositoryName.
    [[nodiscard]] static GitHubRepository Official();

    // Owner: [A-Za-z0-9-]{1,39}; name: [A-Za-z0-9._-]{1,100} and not "." or "..".
    [[nodiscard]] bool IsValid() const noexcept;
    [[nodiscard]] std::string ApiBaseUrl() const;  // https://api.github.com/repos/<owner>/<name>
    [[nodiscard]] std::string WebPrefix() const;   // https://github.com/<owner>/<name>/
};

// True when `url` is https://github.com/<owner>/<name>/<path> with a conservative path character set
// (no "..", no empty segments, no query or fragment).
[[nodiscard]] bool IsRepositoryWebUrl(std::string_view url, const GitHubRepository& repository);
// True when `url` is https://github.com/<owner>/<name>/releases/download/<tag>/<file>.
[[nodiscard]] bool IsReleaseDownloadUrl(std::string_view url, const GitHubRepository& repository);

// True when `url` is an https:// URL whose host (case-insensitive, no user info, default port only)
// is one of `hosts`. Used to double-check HttpResponse::finalUrl after redirects: the transport
// already enforces HttpRequest::allowedRedirectHosts, this is defence in depth.
[[nodiscard]] bool IsHttpsUrlOnHost(std::string_view url, std::span<const std::string_view> hosts);

struct ReleaseAsset {
    std::uint64_t id = 0;
    std::string name;
    std::uint64_t size = 0;
    std::string contentType;
    std::string browserDownloadUrl;  // validated with IsReleaseDownloadUrl
};

inline constexpr std::size_t kMaxReleaseBodyBytes = 64 * 1024;
inline constexpr std::size_t kMaxReleaseAssets = 100;

struct ReleaseInfo {
    std::string tagName;
    SemanticVersion version;  // parsed from tagName (one optional leading "v")
    std::string name;         // untrusted display text
    std::string body;         // untrusted Markdown text, capped at kMaxReleaseBodyBytes; sanitize before display
    std::string htmlUrl;      // validated with IsRepositoryWebUrl
    bool prerelease = false;
    bool draft = false;
    std::optional<std::chrono::system_clock::time_point> publishedAt;  // null for drafts
    // Assets whose names are not plain file names (IsSafeAssetFileName) are left out: they can never
    // match the naming convention, so they are irrelevant rather than suspicious.
    std::vector<ReleaseAsset> assets;

    [[nodiscard]] const ReleaseAsset* FindAsset(std::string_view assetName) const noexcept;
};

}  // namespace aether::updates

// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
//
// Semantic Versioning 2.0.0 (https://semver.org) parsing and precedence.
//
//   * MAJOR.MINOR.PATCH are numeric identifiers without leading zeros that fit in 64 bits.
//   * Prerelease identifiers ("-alpha.1") are non-empty [0-9A-Za-z-]; numeric ones have no
//     leading zeros and compare numerically, below any alphanumeric identifier.
//   * Build metadata ("+build.5") is non-empty [0-9A-Za-z-] identifiers and is IGNORED for
//     precedence: 1.0.0+a and 1.0.0+b are equivalent (==) but not identical (IsIdenticalTo).
//
// Git tags are normalized with ParseTag, which accepts exactly one optional leading "v" or "V"
// ("v1.2.0" and "1.2.0" are accepted; "vv1.2.0", " v1.2.0" and "release-1.2.0" are not).
#pragma once

#include "core/Result.h"

#include <compare>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace aether::updates {

// Inputs longer than this are rejected before parsing (defensive bound for untrusted tags).
inline constexpr std::size_t kMaxSemanticVersionLength = 256;

class SemanticVersion {
public:
    SemanticVersion() = default;  // 0.0.0
    SemanticVersion(std::uint64_t major, std::uint64_t minor, std::uint64_t patch) noexcept
        : major_(major), minor_(minor), patch_(patch) {}

    // Strict SemVer 2.0.0; no leading "v", no surrounding whitespace. Errors use ErrorCode::Parse.
    [[nodiscard]] static Result<SemanticVersion> Parse(std::string_view text);
    // Release tag: one optional leading "v"/"V", then Parse.
    [[nodiscard]] static Result<SemanticVersion> ParseTag(std::string_view tag);

    [[nodiscard]] std::uint64_t Major() const noexcept { return major_; }
    [[nodiscard]] std::uint64_t Minor() const noexcept { return minor_; }
    [[nodiscard]] std::uint64_t Patch() const noexcept { return patch_; }
    [[nodiscard]] const std::vector<std::string>& PrereleaseIdentifiers() const noexcept { return prerelease_; }
    [[nodiscard]] const std::vector<std::string>& BuildMetadata() const noexcept { return build_; }
    [[nodiscard]] bool IsPrerelease() const noexcept { return !prerelease_.empty(); }

    // Canonical text. Parse(text).ToString() == text for every valid input.
    [[nodiscard]] std::string ToString() const;

    // Same precedence and same build metadata.
    [[nodiscard]] bool IsIdenticalTo(const SemanticVersion& other) const noexcept;

    // Precedence (SemVer section 11); build metadata ignored.
    friend std::weak_ordering operator<=>(const SemanticVersion& a, const SemanticVersion& b) noexcept;
    friend bool operator==(const SemanticVersion& a, const SemanticVersion& b) noexcept {
        return (a <=> b) == 0;
    }

private:
    std::uint64_t major_ = 0;
    std::uint64_t minor_ = 0;
    std::uint64_t patch_ = 0;
    std::vector<std::string> prerelease_;
    std::vector<std::string> build_;
};

}  // namespace aether::updates

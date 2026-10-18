// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
#include "updates/UpdateTypes.h"

#include "aether/BuildInfo.h"
#include "core/Strings.h"

#include <algorithm>

namespace aether::updates {
namespace {

bool IsAlnum(char c) noexcept { return (c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z'); }

// Characters allowed in URL path segments we accept from GitHub: unreserved plus '%' (escapes) and '+'.
bool IsSafePathChar(char c) noexcept {
    return IsAlnum(c) || c == '-' || c == '.' || c == '_' || c == '~' || c == '%' || c == '+';
}

bool IsSafeSegment(std::string_view segment) noexcept {
    return !segment.empty() && segment != "." && segment != ".." &&
           std::all_of(segment.begin(), segment.end(), IsSafePathChar);
}

// Matches "https://github.com/<owner>/<name>/" (owner/name case-insensitively) and returns the rest.
std::optional<std::string_view> StripRepositoryPrefix(std::string_view url, const GitHubRepository& repository) {
    constexpr std::string_view kOrigin = "https://github.com/";
    if (!repository.IsValid() || !url.starts_with(kOrigin)) return std::nullopt;
    url.remove_prefix(kOrigin.size());
    const std::size_t ownerEnd = repository.owner.size();
    const std::size_t nameEnd = ownerEnd + 1 + repository.name.size();
    if (url.size() <= nameEnd || url[ownerEnd] != '/' || url[nameEnd] != '/') return std::nullopt;
    if (!text::EqualsIgnoreCaseAscii(url.substr(0, ownerEnd), repository.owner) ||
        !text::EqualsIgnoreCaseAscii(url.substr(ownerEnd + 1, repository.name.size()), repository.name)) {
        return std::nullopt;
    }
    return url.substr(nameEnd + 1);
}

}  // namespace

std::string_view ToString(UpdateChannel channel) noexcept {
    return channel == UpdateChannel::Prerelease ? "prerelease" : "stable";
}

std::optional<UpdateChannel> ParseChannel(std::string_view text) noexcept {
    if (text == "stable") return UpdateChannel::Stable;
    if (text == "prerelease") return UpdateChannel::Prerelease;
    return std::nullopt;
}

UpdateChannel DefaultChannel() noexcept { return ParseChannel(build::kDefaultUpdateChannel).value_or(UpdateChannel::Stable); }

std::string_view ToString(Architecture architecture) noexcept {
    return architecture == Architecture::Arm64 ? "arm64" : "x64";
}

std::optional<Architecture> ParseArchitecture(std::string_view text) noexcept {
    if (text == "x64") return Architecture::X64;
    if (text == "arm64") return Architecture::Arm64;
    return std::nullopt;
}

Architecture CurrentArchitecture() noexcept {
#if defined(_M_ARM64) || defined(_M_ARM64EC)
    return Architecture::Arm64;
#else
    return Architecture::X64;
#endif
}

GitHubRepository GitHubRepository::Official() {
    return GitHubRepository{std::string(build::kUpdateRepositoryOwner), std::string(build::kUpdateRepositoryName)};
}

bool GitHubRepository::IsValid() const noexcept {
    const bool ownerOk = !owner.empty() && owner.size() <= 39 && owner.front() != '-' &&
                         std::all_of(owner.begin(), owner.end(), [](char c) { return IsAlnum(c) || c == '-'; });
    const bool nameOk = !name.empty() && name.size() <= 100 && name != "." && name != ".." &&
                        std::all_of(name.begin(), name.end(),
                                    [](char c) { return IsAlnum(c) || c == '-' || c == '.' || c == '_'; });
    return ownerOk && nameOk;
}

std::string GitHubRepository::ApiBaseUrl() const { return "https://api.github.com/repos/" + owner + "/" + name; }

std::string GitHubRepository::WebPrefix() const { return "https://github.com/" + owner + "/" + name + "/"; }

bool IsRepositoryWebUrl(std::string_view url, const GitHubRepository& repository) {
    const auto rest = StripRepositoryPrefix(url, repository);
    if (!rest || rest->empty()) return false;
    std::string_view path = *rest;
    if (path.back() == '/') path.remove_suffix(1);  // a trailing slash is harmless
    std::size_t start = 0;
    for (;;) {
        const auto slash = path.find('/', start);
        if (!IsSafeSegment(path.substr(start, slash == std::string_view::npos ? std::string_view::npos : slash - start))) {
            return false;
        }
        if (slash == std::string_view::npos) return true;
        start = slash + 1;
    }
}

bool IsReleaseDownloadUrl(std::string_view url, const GitHubRepository& repository) {
    constexpr std::string_view kDownload = "releases/download/";
    const auto rest = StripRepositoryPrefix(url, repository);
    if (!rest || !rest->starts_with(kDownload)) return false;
    const auto path = rest->substr(kDownload.size());
    const auto slash = path.find('/');
    if (slash == std::string_view::npos) return false;
    return IsSafeSegment(path.substr(0, slash)) && IsSafeSegment(path.substr(slash + 1));
}

bool IsHttpsUrlOnHost(std::string_view url, std::span<const std::string_view> hosts) {
    constexpr std::string_view kScheme = "https://";
    if (url.size() < kScheme.size() || !text::EqualsIgnoreCaseAscii(url.substr(0, kScheme.size()), kScheme)) {
        return false;
    }
    url.remove_prefix(kScheme.size());
    std::string_view authority = url.substr(0, url.find_first_of("/?#"));
    if (authority.find('@') != std::string_view::npos) return false;  // user info is never legitimate here
    if (const auto colon = authority.find(':'); colon != std::string_view::npos) {
        if (authority.substr(colon + 1) != "443") return false;
        authority = authority.substr(0, colon);
    }
    if (authority.empty()) return false;
    return std::any_of(hosts.begin(), hosts.end(),
                       [&](std::string_view host) { return text::EqualsIgnoreCaseAscii(authority, host); });
}

const ReleaseAsset* ReleaseInfo::FindAsset(std::string_view assetName) const noexcept {
    for (const auto& asset : assets) {
        if (asset.name == assetName) return &asset;
    }
    return nullptr;
}

}  // namespace aether::updates

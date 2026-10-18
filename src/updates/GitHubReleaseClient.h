// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
//
// Reads release metadata of the official repository from the GitHub REST API
// (unauthenticated: a token is never sent).
//
//   stable      GET https://api.github.com/repos/{owner}/{repo}/releases/latest
//               (GitHub's newest non-draft, non-prerelease release)
//   prerelease  GET https://api.github.com/repos/{owner}/{repo}/releases?per_page=30
//               newest non-draft release by SemVer precedence among valid tags, prereleases
//               included; entries with invalid tags or metadata are skipped
//
// Headers: Accept: application/vnd.github+json, X-GitHub-Api-Version: 2022-11-28, User-Agent,
// and If-None-Match when a cached ETag for the same channel exists (304 reuses the cache).
// Responses are limited to 2 MiB and redirects are allowed to api.github.com only.
//
// Status mapping: 200 parsed strictly; 304 cached release; 404 NotFound ("no releases published
// yet"); 403/429 with x-ratelimit-remaining: 0 or retry-after RateLimited (429 always), with the
// reset time from x-ratelimit-reset (epoch seconds) or retry-after (delta seconds); other 403
// AccessDenied; 5xx Unavailable; anything else Http. Transport errors are passed through.
//
// Release metadata is untrusted: it only tells where to find the signed manifest. URLs must point
// into the official repository (html_url under https://github.com/{owner}/{repo}/, asset
// browser_download_url under .../releases/download/), and a malformed asset (wrong types, URL
// outside the repository, duplicate name) invalidates the release. Assets whose names are not plain
// file names are ignored (they can never match AssetNaming.h). A response whose finalUrl is not on
// api.github.com is rejected (defence in depth on top of the transport's redirect allow-list).
// Blocking: call from worker threads only.
#pragma once

#include "core/Cancellation.h"
#include "core/Http.h"
#include "core/Json.h"
#include "core/Result.h"
#include "core/Time.h"
#include "updates/UpdateTypes.h"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace aether::updates {

inline constexpr std::uint64_t kMaxApiResponseBytes = 2ull * 1024 * 1024;
inline constexpr int kPrereleaseListPageSize = 30;
inline constexpr std::string_view kGitHubApiHost = "api.github.com";
inline constexpr std::string_view kGitHubApiVersion = "2022-11-28";
inline constexpr std::size_t kMaxReleaseNameBytes = 1024;
inline constexpr std::size_t kMaxEtagLength = 256;
// Rate-limit hints further away than this are clamped (a bogus header must not stop checks for days).
inline constexpr std::chrono::hours kMaxRetryHint{6};

// What the last successful query returned, persisted in the update state file.
struct ReleaseMetadataCache {
    UpdateChannel channel = UpdateChannel::Stable;
    std::string etag;     // validator for If-None-Match; empty when the server sent none
    ReleaseInfo release;  // release selected for `channel`
};

struct ReleaseQueryResult {
    ReleaseInfo release;
    std::string etag;
    bool fromCache = false;  // true after 304 Not Modified
};

// Filled when a query fails with ErrorCode::RateLimited.
struct RetryHint {
    std::optional<std::chrono::system_clock::time_point> retryAt;
};

class GitHubReleaseClient {
public:
    // `http` and `time` must outlive the client.
    GitHubReleaseClient(net::IHttpClient& http, const ITimeSource& time, GitHubRepository repository,
                        std::string userAgent);

    // `cache` may be null; it is used only when its channel equals `channel`.
    [[nodiscard]] Result<ReleaseQueryResult> FetchLatest(UpdateChannel channel, const ReleaseMetadataCache* cache,
                                                         const CancellationToken& cancel,
                                                         RetryHint* retryHint = nullptr) const;

    [[nodiscard]] const GitHubRepository& Repository() const noexcept { return repository_; }

private:
    net::IHttpClient& http_;
    const ITimeSource& time_;
    GitHubRepository repository_;
    std::string userAgent_;
};

// "AetherShell/<version> (+https://github.com/<owner>/<repo>)"
[[nodiscard]] std::string DefaultUserAgent();

// Strict parse of one GitHub release object (also used for the cached copy in the state file).
// ErrorCode::Parse for wrong types, ErrorCode::Validation for rule violations (invalid tag, URL
// outside the repository, bad asset).
[[nodiscard]] Result<ReleaseInfo> ParseRelease(const json::Value& release, const GitHubRepository& repository);
// Inverse of ParseRelease (GitHub field names).
[[nodiscard]] json::Value ReleaseToJson(const ReleaseInfo& release);

// Body of /releases/latest. NotFound for a draft or prerelease (never a stable answer).
[[nodiscard]] Result<ReleaseInfo> ParseLatestReleaseResponse(std::string_view body, const GitHubRepository& repository);
// Body of /releases: newest valid non-draft release by SemVer precedence; NotFound when none.
[[nodiscard]] Result<ReleaseInfo> SelectNewestRelease(std::string_view body, const GitHubRepository& repository);

// Printable ASCII without quotes problems for a header value, at most kMaxEtagLength characters.
[[nodiscard]] bool IsAcceptableEtag(std::string_view etag) noexcept;

}  // namespace aether::updates

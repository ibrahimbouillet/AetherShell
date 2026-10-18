// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
#include "updates/GitHubReleaseClient.h"

#include "aether/BuildInfo.h"
#include "core/Strings.h"
#include "diagnostics/Logger.h"
#include "updates/AssetNaming.h"
#include "updates/Encoding.h"

#include <algorithm>
#include <array>
#include <format>

namespace aether::updates {
namespace {

Error Malformed(std::string message) {
    return MakeError(ErrorCode::Parse, "malformed release metadata: " + std::move(message));
}
Error Invalid(std::string message) {
    return MakeError(ErrorCode::Validation, "invalid release metadata: " + std::move(message));
}

// Cuts `text` to at most `maxBytes` without splitting a UTF-8 sequence (input is valid UTF-8: the
// JSON parser guarantees it).
void TruncateUtf8(std::string& text, std::size_t maxBytes) {
    if (text.size() <= maxBytes) return;
    std::size_t cut = maxBytes;
    while (cut > 0 && (static_cast<unsigned char>(text[cut]) & 0xC0) == 0x80) --cut;
    text.resize(cut);
}

// A string member, or "" when it is null or absent. Any other type is malformed.
Result<std::string> OptionalString(const json::Value& object, std::string_view key) {
    const auto* value = object.Find(key);
    if (value == nullptr || value->IsNull()) return std::string();
    if (!value->IsString()) return Malformed(std::format("{} must be a string or null", key));
    return *value->AsString();
}

Result<bool> RequireBool(const json::Value& object, std::string_view key) {
    const auto* value = object.Find(key);
    const auto flag = value != nullptr ? value->AsBool() : std::nullopt;
    if (!flag) return Malformed(std::format("{} must be a boolean", key));
    return *flag;
}

// nullopt (no error) for an asset whose name is not a plain file name: such an asset can never be
// one of ours (AssetNaming.h), so it is ignored instead of invalidating the whole release.
Result<std::optional<ReleaseAsset>> ParseAsset(const json::Value& entry, const GitHubRepository& repository) {
    if (!entry.IsObject()) return Malformed("an asset is not an object");
    ReleaseAsset asset;
    const auto id = entry.Find("id") ? entry.Find("id")->AsInt64() : std::nullopt;
    if (!id || *id <= 0) return Malformed("asset id must be a positive integer");
    asset.id = static_cast<std::uint64_t>(*id);

    const auto* name = entry.Find("name");
    if (name == nullptr || !name->IsString()) return Malformed("asset name must be a string");
    if (!IsSafeAssetFileName(*name->AsString())) {
        AETHER_LOG_DEBUG(kLogComponent, "ignoring release asset with an unusual name '{}'",
                         text::SanitizeForLog(*name->AsString(), 80));
        return std::optional<ReleaseAsset>();
    }
    asset.name = *name->AsString();

    const auto size = entry.Find("size") ? entry.Find("size")->AsInt64() : std::nullopt;
    if (!size || *size < 0) return Malformed("asset size must be a non-negative integer");
    asset.size = static_cast<std::uint64_t>(*size);

    auto contentType = OptionalString(entry, "content_type");
    if (!contentType) return contentType.error();
    asset.contentType = std::move(contentType).value();
    TruncateUtf8(asset.contentType, 255);

    const auto* url = entry.Find("browser_download_url");
    if (url == nullptr || !url->IsString()) return Malformed("asset browser_download_url must be a string");
    if (!IsReleaseDownloadUrl(*url->AsString(), repository)) {
        return Invalid("an asset download URL is outside " + repository.WebPrefix() + "releases/download/");
    }
    asset.browserDownloadUrl = *url->AsString();
    return std::optional<ReleaseAsset>(std::move(asset));
}

// Digits only, at most 18 of them (fits in int64 without overflow checks).
std::optional<std::int64_t> ParseDecimal(std::string_view text) {
    text = text::Trim(text);
    if (text.empty() || text.size() > 18) return std::nullopt;
    std::int64_t value = 0;
    for (const char c : text) {
        if (c < '0' || c > '9') return std::nullopt;
        value = value * 10 + (c - '0');
    }
    return value;
}

std::string DescribeTime(std::chrono::system_clock::time_point time) { return encoding::FormatUtcTimestamp(time); }

}  // namespace

std::string DefaultUserAgent() {
    return std::string(build::kProductId) + "/" + std::string(build::kVersion) + " (+" + std::string(build::kProjectUrl) +
           ")";
}

bool IsAcceptableEtag(std::string_view etag) noexcept {
    return !etag.empty() && etag.size() <= kMaxEtagLength &&
           std::all_of(etag.begin(), etag.end(), [](char c) { return c >= 0x21 && c <= 0x7E; });
}

Result<ReleaseInfo> ParseRelease(const json::Value& value, const GitHubRepository& repository) {
    if (!value.IsObject()) return Malformed("a release is not an object");
    ReleaseInfo release;

    const auto* tag = value.Find("tag_name");
    if (tag == nullptr || !tag->IsString()) return Malformed("tag_name must be a string");
    if (tag->AsString()->size() > kMaxSemanticVersionLength + 1) return Invalid("the release tag is too long");
    auto version = SemanticVersion::ParseTag(*tag->AsString());
    if (!version) {
        return Invalid("release tag '" + text::SanitizeForLog(*tag->AsString(), 64) + "' is not v<semver>");
    }
    release.tagName = *tag->AsString();
    release.version = std::move(version).value();

    auto name = OptionalString(value, "name");
    if (!name) return name.error();
    release.name = std::move(name).value();
    TruncateUtf8(release.name, kMaxReleaseNameBytes);

    auto body = OptionalString(value, "body");
    if (!body) return body.error();
    release.body = std::move(body).value();
    TruncateUtf8(release.body, kMaxReleaseBodyBytes);

    const auto* htmlUrl = value.Find("html_url");
    if (htmlUrl == nullptr || !htmlUrl->IsString()) return Malformed("html_url must be a string");
    if (!IsRepositoryWebUrl(*htmlUrl->AsString(), repository)) {
        return Invalid("html_url is outside " + repository.WebPrefix());
    }
    release.htmlUrl = *htmlUrl->AsString();

    auto prerelease = RequireBool(value, "prerelease");
    if (!prerelease) return prerelease.error();
    release.prerelease = *prerelease;
    auto draft = RequireBool(value, "draft");
    if (!draft) return draft.error();
    release.draft = *draft;

    auto published = OptionalString(value, "published_at");
    if (!published) return published.error();
    if (!published->empty()) {
        auto time = encoding::ParseUtcTimestamp(*published);
        if (!time) return Malformed("published_at is not a UTC timestamp");
        release.publishedAt = *time;
    }

    const auto* assets = value.Find("assets");
    if (assets == nullptr || !assets->IsArray()) return Malformed("assets must be an array");
    if (assets->AsArray()->size() > kMaxReleaseAssets) return Invalid("the release has too many assets");
    for (const auto& entry : *assets->AsArray()) {
        auto asset = ParseAsset(entry, repository);
        if (!asset) return asset.error();
        if (!asset->has_value()) continue;
        if (release.FindAsset((*asset)->name) != nullptr) return Invalid("two assets have the same name");
        release.assets.push_back(std::move(**asset));
    }
    return release;
}

json::Value ReleaseToJson(const ReleaseInfo& release) {
    auto assets = json::Value::MakeArray();
    for (const auto& asset : release.assets) {
        auto entry = json::Value::MakeObject();
        entry.Set("id", json::Value(asset.id));
        entry.Set("name", asset.name);
        entry.Set("size", json::Value(asset.size));
        entry.Set("content_type", asset.contentType);
        entry.Set("browser_download_url", asset.browserDownloadUrl);
        assets.Append(std::move(entry));
    }
    auto object = json::Value::MakeObject();
    object.Set("tag_name", release.tagName);
    object.Set("name", release.name);
    object.Set("body", release.body);
    object.Set("html_url", release.htmlUrl);
    object.Set("prerelease", release.prerelease);
    object.Set("draft", release.draft);
    object.Set("published_at",
               release.publishedAt ? json::Value(encoding::FormatUtcTimestamp(*release.publishedAt)) : json::Value());
    object.Set("assets", std::move(assets));
    return object;
}

Result<ReleaseInfo> ParseLatestReleaseResponse(std::string_view body, const GitHubRepository& repository) {
    auto document = json::Parse(body, json::ParseOptions{kMaxApiResponseBytes, 32});
    if (!document) return Malformed(document.error().message);
    auto release = ParseRelease(*document, repository);
    if (!release) return release.error();
    if (release->draft || release->prerelease) {
        return MakeError(ErrorCode::NotFound, "no stable release published yet");
    }
    return release;
}

Result<ReleaseInfo> SelectNewestRelease(std::string_view body, const GitHubRepository& repository) {
    auto document = json::Parse(body, json::ParseOptions{kMaxApiResponseBytes, 32});
    if (!document) return Malformed(document.error().message);
    if (!document->IsArray()) return Malformed("the release list is not an array");

    std::optional<ReleaseInfo> newest;
    std::size_t skipped = 0;
    for (const auto& entry : *document->AsArray()) {
        auto release = ParseRelease(entry, repository);
        if (!release) {
            ++skipped;
            AETHER_LOG_DEBUG(kLogComponent, "skipping release: {}", release.error().Describe());
            continue;
        }
        if (release->draft) continue;
        // GitHub lists newest first; on equal precedence (build metadata only) keep the newer entry.
        if (!newest || release->version > newest->version) newest = std::move(release).value();
    }
    if (skipped > 0) AETHER_LOG_INFO(kLogComponent, "ignored {} release(s) with invalid tags or metadata", skipped);
    if (!newest) return MakeError(ErrorCode::NotFound, "no releases published yet");
    return std::move(*newest);
}

GitHubReleaseClient::GitHubReleaseClient(net::IHttpClient& http, const ITimeSource& time, GitHubRepository repository,
                                         std::string userAgent)
    : http_(http), time_(time), repository_(std::move(repository)), userAgent_(std::move(userAgent)) {
    if (userAgent_.empty()) userAgent_ = DefaultUserAgent();
}

Result<ReleaseQueryResult> GitHubReleaseClient::FetchLatest(UpdateChannel channel, const ReleaseMetadataCache* cache,
                                                            const CancellationToken& cancel,
                                                            RetryHint* retryHint) const {
    if (retryHint != nullptr) retryHint->retryAt.reset();
    if (!repository_.IsValid()) return MakeError(ErrorCode::InvalidArgument, "the update repository is not valid");

    const bool conditional = cache != nullptr && cache->channel == channel && IsAcceptableEtag(cache->etag);
    net::HttpRequest request;
    request.url = repository_.ApiBaseUrl() +
                  (channel == UpdateChannel::Stable ? std::string("/releases/latest")
                                                    : std::format("/releases?per_page={}", kPrereleaseListPageSize));
    request.headers = {{"Accept", "application/vnd.github+json"},
                       {"X-GitHub-Api-Version", std::string(kGitHubApiVersion)},
                       {"User-Agent", userAgent_}};
    if (conditional) request.headers.push_back({"If-None-Match", cache->etag});
    request.maxResponseBytes = kMaxApiResponseBytes;
    request.allowedRedirectHosts = {std::string(kGitHubApiHost)};

    auto response = http_.Send(request, cancel);
    if (!response) return response.error();
    const int status = response->status;
    AETHER_LOG_INFO(kLogComponent, "GitHub {} release query: HTTP {}", ToString(channel), status);
    constexpr std::array<std::string_view, 1> kApiHosts{kGitHubApiHost};
    if (!response->finalUrl.empty() && !IsHttpsUrlOnHost(response->finalUrl, kApiHosts)) {
        // The transport enforces allowedRedirectHosts; reaching this means it did not.
        return MakeError(ErrorCode::Validation, "the GitHub API response came from an untrusted host");
    }

    if (status == 200) {
        if (response->body.size() > kMaxApiResponseBytes) {
            return MakeError(ErrorCode::Integrity, "the GitHub response exceeds 2 MiB");
        }
        auto release = channel == UpdateChannel::Stable ? ParseLatestReleaseResponse(response->body, repository_)
                                                        : SelectNewestRelease(response->body, repository_);
        if (!release) return release.error();
        ReleaseQueryResult result{std::move(release).value(), {}, false};
        if (const auto* etag = response->FindHeader("ETag"); etag != nullptr && IsAcceptableEtag(*etag)) {
            result.etag = *etag;
        }
        return result;
    }
    if (status == 304) {
        if (!conditional) return MakeError(ErrorCode::Http, "unexpected 304 for an unconditional request", status);
        return ReleaseQueryResult{cache->release, cache->etag, true};
    }
    if (status == 404) return MakeError(ErrorCode::NotFound, "no releases published yet", status);
    if (status == 403 || status == 429) {
        const auto now = time_.SystemNow();
        const auto* remaining = response->FindHeader("x-ratelimit-remaining");
        const auto* retryAfterText = response->FindHeader("retry-after");
        const auto* resetText = response->FindHeader("x-ratelimit-reset");
        const auto retryAfter = retryAfterText ? ParseDecimal(*retryAfterText) : std::nullopt;  // delta-seconds only
        const auto reset = resetText ? ParseDecimal(*resetText) : std::nullopt;
        const bool exhausted = remaining != nullptr && text::Trim(*remaining) == "0";
        if (status == 429 || exhausted || retryAfter) {
            // Header values (up to 18 digits) are clamped in whole seconds BEFORE they become time points:
            // converting e.g. 10^17 s to system_clock ticks (100 ns) would overflow int64.
            const std::int64_t maxHintSeconds = std::chrono::duration_cast<std::chrono::seconds>(kMaxRetryHint).count();
            const std::int64_t nowSeconds =
                std::chrono::floor<std::chrono::seconds>(now).time_since_epoch().count();
            std::optional<std::chrono::system_clock::time_point> retryAt;
            if (retryAfter) retryAt = now + std::chrono::seconds(std::min(*retryAfter, maxHintSeconds + 1));
            if (reset && (exhausted || !retryAfter)) {
                const auto resetAt = std::chrono::system_clock::time_point(
                    std::chrono::seconds(std::min(*reset, nowSeconds + maxHintSeconds + 1)));
                retryAt = retryAt ? std::max(*retryAt, resetAt) : resetAt;
            }
            if (retryAt && *retryAt <= now) retryAt.reset();
            if (retryAt) retryAt = std::min<std::chrono::system_clock::time_point>(*retryAt, now + kMaxRetryHint);
            if (retryHint != nullptr) retryHint->retryAt = retryAt;
            return MakeError(ErrorCode::RateLimited,
                             retryAt ? "GitHub API rate limit reached; retry after " + DescribeTime(*retryAt)
                                     : std::string("GitHub API rate limit reached"),
                             status);
        }
        return MakeError(ErrorCode::AccessDenied, "GitHub refused the request (HTTP 403)", status);
    }
    if (status >= 500 && status <= 599) {
        return MakeError(ErrorCode::Unavailable, std::format("GitHub is unavailable (HTTP {})", status), status);
    }
    return MakeError(ErrorCode::Http, std::format("unexpected HTTP status {} from GitHub", status), status);
}

}  // namespace aether::updates

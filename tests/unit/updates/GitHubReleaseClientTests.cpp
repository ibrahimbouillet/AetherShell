// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
#include "AetherTest.h"
#include "UpdateTestSupport.h"

#include "core/Time.h"
#include "updates/Encoding.h"
#include "updates/GitHubReleaseClient.h"

#include <chrono>
#include <string>

using namespace aether;
using namespace aether::updates;
using namespace aether::updates::testing;
using namespace std::chrono_literals;

namespace {

struct ClientHarness {
    FakeHttpClient http;
    FakeTimeSource time;
    GitHubReleaseClient client{http, time, kTestRepository, "AetherShellTests/1.0"};

    Result<ReleaseQueryResult> Fetch(UpdateChannel channel = UpdateChannel::Stable,
                                     const ReleaseMetadataCache* cache = nullptr, RetryHint* hint = nullptr) {
        return client.FetchLatest(channel, cache, CancellationToken{}, hint);
    }
};

TestRelease Simple(std::string tag, bool prerelease = false, bool draft = false) {
    TestRelease release;
    release.tag = std::move(tag);
    release.prerelease = prerelease;
    release.draft = draft;
    release.body = "notes";
    return release;
}

}  // namespace

AETHER_TEST("github: the stable channel queries releases/latest with the documented headers") {
    ClientHarness h;
    auto release = FixtureRelease(fixtures::kStable_1_2_0);
    h.http.Set(std::string(kLatestUrl), Ok200(ReleaseBody(release), {{"ETag", "W/\"etag-1\""}}));

    auto result = h.Fetch();
    REQUIRE(result.ok());
    CHECK_FALSE(result->fromCache);
    CHECK_EQ(result->etag, std::string("W/\"etag-1\""));

    const auto request = h.http.LastRequestFor(kLatestUrl);
    REQUIRE(request.has_value());
    CHECK_EQ(request->method, std::string("GET"));
    CHECK(HasHeader(*request, "Accept", "application/vnd.github+json"));
    CHECK(HasHeader(*request, "X-GitHub-Api-Version", "2022-11-28"));
    CHECK(HasHeader(*request, "User-Agent", "AetherShellTests/1.0"));
    CHECK_FALSE(HasHeaderNamed(*request, "Authorization"));  // never a token
    CHECK_FALSE(HasHeaderNamed(*request, "If-None-Match"));
    CHECK_EQ(request->maxResponseBytes, 2ull * 1024 * 1024);
    REQUIRE_EQ(request->allowedRedirectHosts.size(), 1u);
    CHECK_EQ(request->allowedRedirectHosts[0], std::string("api.github.com"));
    CHECK_FALSE(request->allowInsecureLoopback);

    const auto& info = result->release;
    CHECK_EQ(info.tagName, std::string("v1.2.0"));
    CHECK_EQ(info.version.ToString(), std::string("1.2.0"));
    CHECK_EQ(info.name, std::string("Aether Shell 1.2.0"));
    CHECK_EQ(info.htmlUrl, std::string("https://github.com/ibrahimbouillet/AetherShell/releases/tag/v1.2.0"));
    CHECK_FALSE(info.prerelease);
    CHECK_FALSE(info.draft);
    REQUIRE(info.publishedAt.has_value());
    CHECK_EQ(encoding::FormatUtcTimestamp(*info.publishedAt), std::string("2026-10-01T12:00:00Z"));
    REQUIRE_EQ(info.assets.size(), 4u);
    const auto* installer = info.FindAsset("AetherShell-1.2.0-win-x64-setup.exe");
    REQUIRE(installer != nullptr);
    CHECK_EQ(installer->size, 300000u);
    CHECK(installer->id > 0);
    CHECK_EQ(installer->contentType, std::string("application/octet-stream"));
    CHECK_EQ(installer->browserDownloadUrl,
             std::string("https://github.com/ibrahimbouillet/AetherShell/releases/download/v1.2.0/"
                         "AetherShell-1.2.0-win-x64-setup.exe"));
}

AETHER_TEST("github: a cached ETag is sent and 304 reuses the cached release") {
    ClientHarness h;
    h.http.Set(std::string(kLatestUrl), Ok200(ReleaseBody(Simple("v1.2.0")), {{"etag", "\"abc\""}}));
    auto first = h.Fetch();
    REQUIRE(first.ok());
    const ReleaseMetadataCache cache{UpdateChannel::Stable, first->etag, first->release};

    h.http.Set(std::string(kLatestUrl), Status(304));
    auto second = h.Fetch(UpdateChannel::Stable, &cache);
    REQUIRE(second.ok());
    CHECK(second->fromCache);
    CHECK_EQ(second->release.tagName, std::string("v1.2.0"));
    CHECK_EQ(second->etag, std::string("\"abc\""));
    const auto request = h.http.LastRequestFor(kLatestUrl);
    REQUIRE(request.has_value());
    CHECK(HasHeader(*request, "If-None-Match", "\"abc\""));

    // A cache of the other channel is not used, so 304 cannot be accepted for it.
    auto mismatch = h.Fetch(UpdateChannel::Prerelease, &cache);
    CHECK_FALSE(mismatch.ok());
    CHECK_FALSE(HasHeaderNamed(*h.http.LastRequestFor(kListUrl), "If-None-Match"));
    // 304 without a conditional request is a protocol error.
    auto unexpected = h.Fetch();
    REQUIRE_FALSE(unexpected.ok());
    CHECK_EQ(unexpected.error().code, ErrorCode::Http);
}

AETHER_TEST("github: 404 means no release is published yet") {
    ClientHarness h;
    h.http.Set(std::string(kLatestUrl), Status(404));
    auto result = h.Fetch();
    REQUIRE_FALSE(result.ok());
    CHECK_EQ(result.error().code, ErrorCode::NotFound);
    CHECK(result.error().message.find("no releases published yet") != std::string::npos);
}

AETHER_TEST("github: rate limits report the reset time") {
    ClientHarness h;
    const auto now = h.time.SystemNow();
    const auto resetEpoch = std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch()).count() + 1800;

    h.http.Set(std::string(kLatestUrl), Status(403, {{"x-ratelimit-remaining", "0"},
                                                     {"x-ratelimit-reset", std::to_string(resetEpoch)}}));
    RetryHint hint;
    auto limited = h.Fetch(UpdateChannel::Stable, nullptr, &hint);
    REQUIRE_FALSE(limited.ok());
    CHECK_EQ(limited.error().code, ErrorCode::RateLimited);
    CHECK_EQ(limited.error().platformCode, 403);
    REQUIRE(hint.retryAt.has_value());
    CHECK(*hint.retryAt == std::chrono::system_clock::time_point(std::chrono::seconds(resetEpoch)));

    h.http.Set(std::string(kLatestUrl), Status(429, {{"Retry-After", "120"}}));
    auto tooMany = h.Fetch(UpdateChannel::Stable, nullptr, &hint);
    REQUIRE_FALSE(tooMany.ok());
    CHECK_EQ(tooMany.error().code, ErrorCode::RateLimited);
    REQUIRE(hint.retryAt.has_value());
    CHECK(*hint.retryAt == now + 120s);

    // Secondary rate limit: 403 with retry-after.
    h.http.Set(std::string(kLatestUrl), Status(403, {{"retry-after", "60"}}));
    auto secondary = h.Fetch(UpdateChannel::Stable, nullptr, &hint);
    REQUIRE_FALSE(secondary.ok());
    CHECK_EQ(secondary.error().code, ErrorCode::RateLimited);
    CHECK(*hint.retryAt == now + 60s);

    // 429 without hints is still a rate limit, without a retry time.
    h.http.Set(std::string(kLatestUrl), Status(429));
    auto bare = h.Fetch(UpdateChannel::Stable, nullptr, &hint);
    REQUIRE_FALSE(bare.ok());
    CHECK_EQ(bare.error().code, ErrorCode::RateLimited);
    CHECK_FALSE(hint.retryAt.has_value());

    // An absurd reset time is clamped to 6 hours; one in the past is dropped.
    h.http.Set(std::string(kLatestUrl), Status(403, {{"x-ratelimit-remaining", "0"},
                                                     {"x-ratelimit-reset", std::to_string(resetEpoch + 86400 * 30)}}));
    (void)h.Fetch(UpdateChannel::Stable, nullptr, &hint);
    REQUIRE(hint.retryAt.has_value());
    CHECK(*hint.retryAt == now + 6h);
    h.http.Set(std::string(kLatestUrl), Status(403, {{"x-ratelimit-remaining", "0"}, {"x-ratelimit-reset", "100"}}));
    (void)h.Fetch(UpdateChannel::Stable, nullptr, &hint);
    CHECK_FALSE(hint.retryAt.has_value());

    // Huge values are clamped in seconds before they become system_clock ticks (100 ns, int64):
    // 922337203686 s is the first value whose tick count exceeds INT64_MAX (it used to wrap into the
    // past and the hint vanished), 18 digits is the most ParseDecimal accepts.
    for (const std::string huge : {"922337203686", "999999999999999999"}) {
        h.http.Set(std::string(kLatestUrl), Status(429, {{"retry-after", huge}}));
        auto hugeDelay = h.Fetch(UpdateChannel::Stable, nullptr, &hint);
        CHECK_FALSE(hugeDelay.ok());
        CHECK(hint.retryAt.has_value() && *hint.retryAt == now + 6h);
        h.http.Set(std::string(kLatestUrl), Status(403, {{"x-ratelimit-remaining", "0"}, {"x-ratelimit-reset", huge}}));
        auto hugeReset = h.Fetch(UpdateChannel::Stable, nullptr, &hint);
        CHECK_FALSE(hugeReset.ok());
        CHECK(hint.retryAt.has_value() && *hint.retryAt == now + 6h);
    }

    // A 403 that is not about rate limits (remaining > 0, no retry-after).
    h.http.Set(std::string(kLatestUrl), Status(403, {{"x-ratelimit-remaining", "42"}}));
    auto denied = h.Fetch(UpdateChannel::Stable, nullptr, &hint);
    REQUIRE_FALSE(denied.ok());
    CHECK_EQ(denied.error().code, ErrorCode::AccessDenied);
    // Malformed headers are ignored rather than trusted.
    h.http.Set(std::string(kLatestUrl), Status(403, {{"retry-after", "Wed, 21 Oct 2026 07:28:00 GMT"}}));
    auto httpDate = h.Fetch(UpdateChannel::Stable, nullptr, &hint);
    REQUIRE_FALSE(httpDate.ok());
    CHECK_EQ(httpDate.error().code, ErrorCode::AccessDenied);
}

AETHER_TEST("github: server errors are Unavailable and transport errors pass through") {
    ClientHarness h;
    for (const int status : {500, 502, 503, 504}) {
        h.http.Set(std::string(kLatestUrl), Status(status));
        auto result = h.Fetch();
        REQUIRE_FALSE(result.ok());
        CHECK_EQ(result.error().code, ErrorCode::Unavailable);
        CHECK_EQ(result.error().platformCode, status);
    }
    h.http.Set(std::string(kLatestUrl), TransportError(ErrorCode::Network, "no route to host"));
    auto offline = h.Fetch();
    REQUIRE_FALSE(offline.ok());
    CHECK_EQ(offline.error().code, ErrorCode::Network);
    h.http.Set(std::string(kLatestUrl), TransportError(ErrorCode::Timeout, "timed out"));
    CHECK_EQ(h.Fetch().error().code, ErrorCode::Timeout);
    h.http.Set(std::string(kLatestUrl), Status(418));
    CHECK_EQ(h.Fetch().error().code, ErrorCode::Http);
}

AETHER_TEST("github: redirects are limited to api.github.com") {
    ClientHarness h;
    // What the WinHTTP client reports when a redirect leaves the allow-list.
    h.http.Set(std::string(kLatestUrl),
               TransportError(ErrorCode::Validation, "redirect to untrusted host 'evil.example' rejected"));
    auto rejected = h.Fetch();
    REQUIRE_FALSE(rejected.ok());
    CHECK_EQ(rejected.error().code, ErrorCode::Validation);
    const auto request = h.http.LastRequestFor(kLatestUrl);
    REQUIRE(request.has_value());
    CHECK(request->allowedRedirectHosts == std::vector<std::string>{"api.github.com"});

    // Defence in depth: a response that claims to come from elsewhere is refused.
    auto response = Ok200(ReleaseBody(Simple("v1.2.0")));
    response.finalUrl = "https://evil.example/repos/ibrahimbouillet/AetherShell/releases/latest";
    h.http.Set(std::string(kLatestUrl), response);
    auto foreign = h.Fetch();
    REQUIRE_FALSE(foreign.ok());
    CHECK_EQ(foreign.error().code, ErrorCode::Validation);
    response.finalUrl = "https://API.GITHUB.COM:443/repos/ibrahimbouillet/AetherShell/releases/latest";
    h.http.Set(std::string(kLatestUrl), response);
    CHECK(h.Fetch().ok());
}

AETHER_TEST("github: responses are parsed strictly") {
    ClientHarness h;
    auto expectCode = [&](std::string body, ErrorCode code) {
        h.http.Set(std::string(kLatestUrl), Ok200(std::move(body)));
        auto result = h.Fetch();
        CHECK_FALSE(result.ok());
        if (!result) CHECK_EQ(result.error().code, code);
    };
    expectCode("not json", ErrorCode::Parse);
    expectCode("[]", ErrorCode::Parse);
    expectCode(R"({"tag_name":5})", ErrorCode::Parse);

    auto base = Simple("v1.2.0");
    auto withAssets = FixtureRelease(fixtures::kStable_1_2_0);

    auto edited = [](const TestRelease& release, const std::function<void(json::Value&)>& mutate) {
        auto value = ReleaseJson(release);
        mutate(value);
        return json::Serialize(value);
    };
    expectCode(edited(base, [](json::Value& v) { v.Set("tag_name", "nightly"); }), ErrorCode::Validation);
    expectCode(edited(base, [](json::Value& v) { v.Set("tag_name", "v" + std::string(300, '1')); }),
               ErrorCode::Validation);
    expectCode(edited(base, [](json::Value& v) { v.Set("html_url", "https://github.com/evil/AetherShell/releases/tag/v1.2.0"); }),
               ErrorCode::Validation);
    expectCode(edited(base, [](json::Value& v) { v.Set("html_url", "https://github.com.evil.example/x"); }),
               ErrorCode::Validation);
    expectCode(edited(base, [](json::Value& v) { v.Set("html_url", "https://github.com/ibrahimbouillet/AetherShell/../../x"); }),
               ErrorCode::Validation);
    expectCode(edited(base, [](json::Value& v) { v.Set("prerelease", "false"); }), ErrorCode::Parse);
    expectCode(edited(base, [](json::Value& v) { v.Set("draft", json::Value()); }), ErrorCode::Parse);
    expectCode(edited(base, [](json::Value& v) { v.Set("published_at", "yesterday"); }), ErrorCode::Parse);
    expectCode(edited(base, [](json::Value& v) { v.Set("assets", json::Value()); }), ErrorCode::Parse);
    expectCode(edited(base, [](json::Value& v) { v.Set("body", 7); }), ErrorCode::Parse);

    auto assetEdit = [&](const std::function<void(json::Value&)>& mutate) {
        return edited(withAssets, [&](json::Value& v) { mutate((*v.Find("assets")->AsArray())[0]); });
    };
    expectCode(assetEdit([](json::Value& a) {
                   a.Set("browser_download_url", "https://objects.githubusercontent.com/github-production-release-asset/x");
               }),
               ErrorCode::Validation);
    expectCode(assetEdit([](json::Value& a) {
                   a.Set("browser_download_url",
                         "https://github.com/evil/AetherShell/releases/download/v1.2.0/AetherShell-1.2.0-win-x64-setup.exe");
               }),
               ErrorCode::Validation);
    expectCode(assetEdit([](json::Value& a) {
                   a.Set("browser_download_url", "http://github.com/ibrahimbouillet/AetherShell/releases/download/v1.2.0/x.exe");
               }),
               ErrorCode::Validation);
    expectCode(assetEdit([](json::Value& a) { a.Set("size", -5); }), ErrorCode::Parse);
    expectCode(assetEdit([](json::Value& a) { a.Set("id", "1"); }), ErrorCode::Parse);
    expectCode(edited(withAssets,
                      [](json::Value& v) {
                          const auto copy = (*v.Find("assets")->AsArray())[0];
                          v.Find("assets")->Append(copy);
                      }),
               ErrorCode::Validation);

    // An asset whose name is not a plain file name is ignored, not fatal.
    h.http.Set(std::string(kLatestUrl), Ok200(assetEdit([](json::Value& a) { a.Set("name", "notes (draft).txt"); })));
    auto ignored = h.Fetch();
    REQUIRE(ignored.ok());
    CHECK_EQ(ignored->release.assets.size(), 3u);
}

AETHER_TEST("github: the release body is capped and drafts or prereleases are never a stable answer") {
    ClientHarness h;
    auto release = Simple("v1.2.0");
    release.body = std::string(kMaxReleaseBodyBytes - 1, 'a') + "\xE2\x82\xAC" + "tail";  // euro sign on the boundary
    h.http.Set(std::string(kLatestUrl), Ok200(ReleaseBody(release)));
    auto capped = h.Fetch();
    REQUIRE(capped.ok());
    CHECK(capped->release.body.size() <= kMaxReleaseBodyBytes);
    CHECK_EQ(capped->release.body.size(), kMaxReleaseBodyBytes - 1);  // the split code point is dropped whole
    CHECK(text::IsValidUtf8(capped->release.body));

    h.http.Set(std::string(kLatestUrl), Ok200(ReleaseBody(Simple("v1.3.0", true))));
    CHECK_EQ(h.Fetch().error().code, ErrorCode::NotFound);
    h.http.Set(std::string(kLatestUrl), Ok200(ReleaseBody(Simple("v1.3.0", false, true))));
    CHECK_EQ(h.Fetch().error().code, ErrorCode::NotFound);

    // Bodies above the 2 MiB response limit are refused by the transport.
    h.http.Set(std::string(kLatestUrl), Ok200(std::string(kMaxApiResponseBytes + 1, ' ')));
    CHECK_EQ(h.Fetch().error().code, ErrorCode::Integrity);
}

AETHER_TEST("github: the prerelease channel picks the highest valid non-draft version") {
    ClientHarness h;
    auto draft = Simple("v3.0.0", false, true);
    auto beta2 = FixtureRelease(fixtures::kPrerelease_2_0_0_beta_2);
    auto beta10 = Simple("v2.0.0-beta.10", true);
    auto stable = Simple("v1.2.0");
    auto nightly = Simple("nightly-2026-10-17", true);
    auto badUrl = Simple("v9.0.0");
    badUrl.htmlUrl = "https://github.com/someone-else/AetherShell/releases/tag/v9.0.0";
    // GitHub lists newest first; precedence, not order, decides.
    h.http.Set(std::string(kListUrl), Ok200(ReleaseListBody({draft, nightly, beta2, badUrl, beta10, stable})));

    auto result = h.Fetch(UpdateChannel::Prerelease);
    REQUIRE(result.ok());
    CHECK_EQ(result->release.tagName, std::string("v2.0.0-beta.10"));
    CHECK(result->release.prerelease);
    REQUIRE(h.http.LastRequestFor(kListUrl).has_value());
    CHECK_EQ(h.http.CountFor(kLatestUrl), 0u);

    // A stable release newer than every prerelease wins on the prerelease channel too.
    h.http.Set(std::string(kListUrl), Ok200(ReleaseListBody({beta2, Simple("v2.0.0")})));
    auto newerStable = h.Fetch(UpdateChannel::Prerelease);
    REQUIRE(newerStable.ok());
    CHECK_EQ(newerStable->release.tagName, std::string("v2.0.0"));

    h.http.Set(std::string(kListUrl), Ok200(ReleaseListBody({draft, nightly})));
    CHECK_EQ(h.Fetch(UpdateChannel::Prerelease).error().code, ErrorCode::NotFound);
    h.http.Set(std::string(kListUrl), Ok200("[]"));
    CHECK_EQ(h.Fetch(UpdateChannel::Prerelease).error().code, ErrorCode::NotFound);
    h.http.Set(std::string(kListUrl), Ok200("{}"));
    CHECK_EQ(h.Fetch(UpdateChannel::Prerelease).error().code, ErrorCode::Parse);
}

AETHER_TEST("github: release metadata round-trips through the cache format") {
    auto parsed = ParseRelease(ReleaseJson(FixtureRelease(fixtures::kStable_1_2_0)), kTestRepository);
    REQUIRE(parsed.ok());
    auto again = ParseRelease(ReleaseToJson(*parsed), kTestRepository);
    REQUIRE(again.ok());
    CHECK_EQ(again->tagName, parsed->tagName);
    CHECK_EQ(again->body, parsed->body);
    CHECK_EQ(again->assets.size(), parsed->assets.size());
    CHECK_EQ(again->assets[0].browserDownloadUrl, parsed->assets[0].browserDownloadUrl);
    CHECK(again->publishedAt == parsed->publishedAt);

    CHECK(IsAcceptableEtag("W/\"0123\""));
    CHECK_FALSE(IsAcceptableEtag(""));
    CHECK_FALSE(IsAcceptableEtag("a b"));
    CHECK_FALSE(IsAcceptableEtag("a\r\nInjected: 1"));
    CHECK_FALSE(IsAcceptableEtag(std::string(kMaxEtagLength + 1, 'a')));

    CHECK_EQ(DefaultUserAgent().rfind("AetherShell/", 0), 0u);
}

AETHER_TEST("github: repository URL checks are exact") {
    CHECK(IsRepositoryWebUrl("https://github.com/ibrahimbouillet/AetherShell/releases/tag/v1.2.0", kTestRepository));
    CHECK(IsRepositoryWebUrl("https://github.com/IbrahimBouillet/aethershell/releases/tag/v1.2.0", kTestRepository));
    CHECK_FALSE(IsRepositoryWebUrl("https://github.com/ibrahimbouillet/AetherShell", kTestRepository));
    CHECK_FALSE(IsRepositoryWebUrl("https://github.com/ibrahimbouillet/AetherShellX/releases", kTestRepository));
    CHECK_FALSE(IsRepositoryWebUrl("https://github.com/ibrahimbouillet/AetherShell/releases?x=1", kTestRepository));
    CHECK_FALSE(IsRepositoryWebUrl("https://github.com/ibrahimbouillet/AetherShell//releases", kTestRepository));
    CHECK_FALSE(IsRepositoryWebUrl("https://github.com/ibrahimbouillet/AetherShell/releases/#x", kTestRepository));
    CHECK(IsReleaseDownloadUrl(
        "https://github.com/ibrahimbouillet/AetherShell/releases/download/v1.2.0+b.1/AetherShell-1.2.0+b.1-manifest.json",
        kTestRepository));
    CHECK_FALSE(IsReleaseDownloadUrl("https://github.com/ibrahimbouillet/AetherShell/releases/download/v1.2.0",
                                     kTestRepository));
    CHECK_FALSE(IsReleaseDownloadUrl("https://github.com/ibrahimbouillet/AetherShell/releases/download/v1.2.0/a/b",
                                     kTestRepository));
    CHECK_FALSE(IsReleaseDownloadUrl("https://github.com/ibrahimbouillet/AetherShell/releases/download/../x/y",
                                     kTestRepository));
    CHECK_FALSE(IsReleaseDownloadUrl("https://github.com/ibrahimbouillet/AetherShell/archive/v1.2.0/x.zip",
                                     kTestRepository));

    const std::array<std::string_view, 2> hosts{"github.com", "objects.githubusercontent.com"};
    CHECK(IsHttpsUrlOnHost("https://github.com/x", hosts));
    CHECK(IsHttpsUrlOnHost("HTTPS://OBJECTS.githubusercontent.com:443/x?y#z", hosts));
    CHECK(IsHttpsUrlOnHost("https://github.com", hosts));
    CHECK_FALSE(IsHttpsUrlOnHost("http://github.com/x", hosts));
    CHECK_FALSE(IsHttpsUrlOnHost("https://github.com:8443/x", hosts));
    CHECK_FALSE(IsHttpsUrlOnHost("https://user@github.com/x", hosts));
    CHECK_FALSE(IsHttpsUrlOnHost("https://github.com.evil.example/x", hosts));
    CHECK_FALSE(IsHttpsUrlOnHost("https://evil.example/github.com", hosts));
    CHECK_FALSE(IsHttpsUrlOnHost("https:///x", hosts));
}

// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
#include "AetherTest.h"
#include "UpdateTestSupport.h"

#include "core/FileSystem.h"
#include "updates/Authenticode.h"
#include "updates/Sha256.h"
#include "updates/UpdateDownloader.h"

#include <windows.h>

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <thread>
#include <vector>

using namespace aether;
using namespace aether::updates;
using namespace aether::updates::testing;
using namespace std::chrono_literals;

namespace {

constexpr std::string_view kTag = "v1.2.0";

struct DownloadHarness {
    std::filesystem::path root = test::MakeTempDirectory("updates-downloader");
    FakeHttpClient http;
    std::uint64_t freeSpace = 1ull << 40;
    int diskQueries = 0;
    std::vector<std::chrono::milliseconds> waits;
    std::vector<std::pair<std::uint64_t, std::uint64_t>> progress;
    AuthenticodePolicy authenticode = AuthenticodePolicy::NotRequired;

    const fixtures::FixtureAsset& asset = fixtures::kStable_1_2_0.assets[0];
    const std::string payload = TestAssetBytes(asset.size, asset.seed);
    const std::string url = DownloadUrl(kTag, asset.fileName);

    UpdateDownloader Make() {
        UpdateDownloaderOptions options;
        options.updatesDir = root / L"updates";
        options.userAgent = "AetherShellTests/1.0";
        options.repository = kTestRepository;
        options.diskSpace = [this](const std::filesystem::path&) -> Result<std::uint64_t> {
            ++diskQueries;
            return freeSpace;
        };
        options.retryWait = [this](std::chrono::milliseconds delay, const CancellationToken& cancel) {
            waits.push_back(delay);
            return !cancel.IsCancelled();
        };
        options.authenticode = authenticode;
        return UpdateDownloader(http, std::move(options));
    }

    AssetDownloadRequest Request() const {
        AssetDownloadRequest request;
        request.url = url;
        request.fileName = std::string(asset.fileName);
        request.version = SemanticVersion::Parse("1.2.0").value();
        request.expectedSize = asset.size;
        request.expectedSha256 = Sha256::Hash(payload).value();
        return request;
    }

    Result<std::filesystem::path> Download(const CancellationToken& cancel = {}) {
        auto downloader = Make();
        return downloader.Download(
            Request(), [this](std::uint64_t received, std::uint64_t total) { progress.emplace_back(received, total); },
            cancel);
    }

    std::filesystem::path VersionDir() const { return root / L"updates" / L"downloads" / L"1.2.0"; }
    std::filesystem::path FinalPath() const { return VersionDir() / std::wstring(asset.fileName.begin(), asset.fileName.end()); }
    std::filesystem::path PartialPath() const {
        auto partial = FinalPath();
        partial += L".partial";
        return partial;
    }
    bool NothingLeft() const {
        std::error_code ec;
        return !std::filesystem::exists(FinalPath(), ec) && !std::filesystem::exists(PartialPath(), ec);
    }
};

std::string ReadAll(const std::filesystem::path& file) {
    auto contents = fs::ReadFile(file, 1u << 30);
    return contents ? *contents : std::string();
}

}  // namespace

AETHER_TEST("downloader: streams, verifies and atomically places the installer") {
    DownloadHarness h;
    h.http.Set(h.url, Ok200(h.payload));
    auto result = h.Download();
    REQUIRE(result.ok());
    CHECK(*result == h.FinalPath());
    CHECK(ReadAll(h.FinalPath()) == h.payload);
    CHECK_FALSE(std::filesystem::exists(h.PartialPath()));
    CHECK_EQ(h.diskQueries, 1);
    CHECK(h.waits.empty());

    const auto request = h.http.LastRequestFor(h.url);
    REQUIRE(request.has_value());
    CHECK(HasHeader(*request, "Accept", "application/octet-stream"));
    CHECK(HasHeader(*request, "User-Agent", "AetherShellTests/1.0"));
    CHECK_FALSE(HasHeaderNamed(*request, "Authorization"));
    CHECK_EQ(request->maxResponseBytes, h.asset.size);
    CHECK(request->allowedRedirectHosts == (std::vector<std::string>{"github.com", "objects.githubusercontent.com",
                                                                      "release-assets.githubusercontent.com"}));

    REQUIRE_FALSE(h.progress.empty());
    CHECK(h.progress.front() == std::make_pair(std::uint64_t{0}, h.asset.size));
    CHECK(h.progress.back() == std::make_pair(h.asset.size, h.asset.size));
    for (std::size_t i = 1; i < h.progress.size(); ++i) CHECK(h.progress[i].first >= h.progress[i - 1].first);
}

AETHER_TEST("downloader: an interrupted transfer is retried from byte zero") {
    DownloadHarness h;
    FakeResponse interrupted = Ok200(h.payload);
    interrupted.failAfter = 100'000;  // connection drops mid-way
    h.http.Add(h.url, interrupted);
    h.http.Add(h.url, Ok200(h.payload));

    auto result = h.Download();
    REQUIRE(result.ok());
    CHECK(ReadAll(h.FinalPath()) == h.payload);
    CHECK_EQ(h.http.CountFor(h.url), 2u);
    REQUIRE_EQ(h.waits.size(), 1u);
    CHECK(h.waits[0] == 2s);
    // Progress restarts at zero for the second attempt.
    std::size_t restarts = 0;
    for (const auto& [received, total] : h.progress) restarts += received == 0 ? 1 : 0;
    CHECK_EQ(restarts, 2u);
}

AETHER_TEST("downloader: retries are bounded and leave nothing behind") {
    DownloadHarness h;
    FakeResponse interrupted = Ok200(h.payload);
    interrupted.failAfter = 50'000;
    h.http.Set(h.url, interrupted);
    auto result = h.Download();
    REQUIRE_FALSE(result.ok());
    CHECK_EQ(result.error().code, ErrorCode::Network);
    CHECK_EQ(h.http.CountFor(h.url), 3u);
    CHECK(h.waits == (std::vector<std::chrono::milliseconds>{2s, 8s}));
    CHECK(h.NothingLeft());

    // A body that ends early with HTTP 200 is treated like an interruption.
    DownloadHarness truncated;
    FakeResponse shortResponse = Ok200(truncated.payload.substr(0, 1000));
    shortResponse.announceLength = false;  // chunked transfer: the length is only known at the end
    truncated.http.Set(truncated.url, shortResponse);
    auto shortBody = truncated.Download();
    REQUIRE_FALSE(shortBody.ok());
    CHECK_EQ(shortBody.error().code, ErrorCode::Network);
    CHECK_EQ(truncated.http.CountFor(truncated.url), 3u);
    CHECK(truncated.NothingLeft());

    // Server errors are retried, a missing asset is not.
    // The transport streams error pages through the sink like any body; the status decides.
    DownloadHarness unavailable;
    unavailable.http.Set(unavailable.url, Status(503));  // the page announces its own length
    CHECK_EQ(unavailable.Download().error().code, ErrorCode::Unavailable);
    CHECK_EQ(unavailable.http.CountFor(unavailable.url), 3u);
    CHECK(unavailable.NothingLeft());
    DownloadHarness chunkedPage;
    FakeResponse page = Status(500);
    page.announceLength = false;  // no Content-Length: the page is written to the partial file first
    chunkedPage.http.Set(chunkedPage.url, page);
    CHECK_EQ(chunkedPage.Download().error().code, ErrorCode::Unavailable);
    CHECK(chunkedPage.NothingLeft());
    DownloadHarness missing;
    missing.http.Set(missing.url, Status(404));
    CHECK_EQ(missing.Download().error().code, ErrorCode::NotFound);
    CHECK_EQ(missing.http.CountFor(missing.url), 1u);
    DownloadHarness limited;
    limited.http.Set(limited.url, Status(403));
    CHECK_EQ(limited.Download().error().code, ErrorCode::RateLimited);
    CHECK(limited.NothingLeft());
}

AETHER_TEST("downloader: a corrupted download fails the digest check and is deleted") {
    DownloadHarness h;
    auto corrupted = h.payload;
    corrupted[corrupted.size() / 2] = static_cast<char>(corrupted[corrupted.size() / 2] ^ 0x5A);
    h.http.Set(h.url, Ok200(corrupted));
    auto result = h.Download();
    REQUIRE_FALSE(result.ok());
    CHECK_EQ(result.error().code, ErrorCode::Integrity);
    CHECK_EQ(h.http.CountFor(h.url), 1u);  // integrity failures are not retried
    CHECK(h.NothingLeft());
}

AETHER_TEST("downloader: a body larger than the signed size is aborted") {
    for (const bool transportEnforces : {false, true}) {
        DownloadHarness h;
        FakeResponse oversized = Ok200(h.payload + "EXTRA BYTES");
        oversized.enforceLimit = transportEnforces;  // false: the downloader's own sink must catch it
        oversized.announceLength = false;
        h.http.Set(h.url, oversized);
        auto result = h.Download();
        REQUIRE_FALSE(result.ok());
        CHECK_EQ(result.error().code, ErrorCode::Integrity);
        CHECK_EQ(h.http.CountFor(h.url), 1u);
        CHECK(h.NothingLeft());
        // Never more than the expected size was reported.
        for (const auto& [received, total] : h.progress) CHECK(received <= h.asset.size);
    }

    // A Content-Length that contradicts the manifest is refused before anything is written.
    DownloadHarness announced;
    FakeResponse wrongLength = Ok200(announced.payload.substr(0, 1000));
    wrongLength.enforceLimit = false;
    announced.http.Set(announced.url, wrongLength);
    auto contradicted = announced.Download();
    REQUIRE_FALSE(contradicted.ok());
    CHECK_EQ(contradicted.error().code, ErrorCode::Integrity);
    CHECK(announced.NothingLeft());
}

AETHER_TEST("downloader: free disk space is checked before downloading") {
    DownloadHarness h;
    h.http.Set(h.url, Ok200(h.payload));
    const auto required = RequiredDiskSpace(h.asset.size);
    CHECK_EQ(required, h.asset.size * 3 + 50ull * 1024 * 1024);
    h.freeSpace = required - 1;
    auto result = h.Download();
    REQUIRE_FALSE(result.ok());
    CHECK_EQ(result.error().code, ErrorCode::InsufficientSpace);
    CHECK_EQ(h.http.CountFor(h.url), 0u);
    CHECK(h.NothingLeft());

    h.freeSpace = required;
    CHECK(h.Download().ok());

    CHECK_EQ(RequiredDiskSpace(~std::uint64_t{0}), ~std::uint64_t{0});  // saturates

    DownloadHarness failing;
    UpdateDownloaderOptions options;
    options.updatesDir = failing.root / L"updates";
    options.repository = kTestRepository;
    options.diskSpace = [](const std::filesystem::path&) -> Result<std::uint64_t> {
        return MakeError(ErrorCode::Io, "volume unavailable");
    };
    UpdateDownloader downloader(failing.http, std::move(options));
    auto noQuery = downloader.Download(failing.Request(), {}, {});
    REQUIRE_FALSE(noQuery.ok());
    CHECK_EQ(noQuery.error().code, ErrorCode::Io);
}

AETHER_TEST("downloader: only official URLs and trusted redirect hosts are accepted") {
    DownloadHarness h;
    auto downloader = h.Make();
    auto request = h.Request();
    for (const std::string& url :
         {std::string("https://evil.example/AetherShell-1.2.0-win-x64-setup.exe"),
          std::string("https://github.com/someone-else/AetherShell/releases/download/v1.2.0/x.exe"),
          std::string("http://github.com/ibrahimbouillet/AetherShell/releases/download/v1.2.0/x.exe"),
          std::string("https://objects.githubusercontent.com/github-production-release-asset/1/2")}) {
        request.url = url;
        auto result = downloader.Download(request, {}, {});
        REQUIRE_FALSE(result.ok());
        CHECK_EQ(result.error().code, ErrorCode::InvalidArgument);
    }
    CHECK(h.http.Requests().empty());

    // The transport refuses a redirect outside the allow-list (what WinHttpClient reports).
    h.http.Set(h.url, TransportError(ErrorCode::Validation, "redirect to untrusted host 'evil.example' rejected"));
    auto redirected = h.Download();
    REQUIRE_FALSE(redirected.ok());
    CHECK_EQ(redirected.error().code, ErrorCode::Validation);
    CHECK_EQ(h.http.CountFor(h.url), 1u);  // not retried
    CHECK(h.NothingLeft());

    // Defence in depth: a transfer reporting a final URL on another host is discarded.
    FakeResponse foreign = Ok200(h.payload);
    foreign.finalUrl = "https://evil.example/AetherShell-1.2.0-win-x64-setup.exe";
    h.http.Set(h.url, foreign);
    auto served = h.Download();
    REQUIRE_FALSE(served.ok());
    CHECK_EQ(served.error().code, ErrorCode::Validation);
    CHECK(h.NothingLeft());

    // GitHub's real redirect target is fine.
    FakeResponse cdn = Ok200(h.payload);
    cdn.finalUrl = "https://release-assets.githubusercontent.com/github-production-release-asset/123/456?sig=abc";
    h.http.Set(h.url, cdn);
    CHECK(h.Download().ok());
}

AETHER_TEST("downloader: file names and directories cannot escape the downloads folder") {
    DownloadHarness h;
    auto downloader = h.Make();
    for (const std::string_view name : {"..\\evil.exe", "../evil.exe", "sub\\x.exe", "C:x.exe", "x.exe:ads", "", "CON"}) {
        auto request = h.Request();
        request.fileName = std::string(name);
        auto result = downloader.Download(request, {}, {});
        REQUIRE_FALSE(result.ok());
        CHECK_EQ(result.error().code, ErrorCode::InvalidArgument);
    }
    auto zero = h.Request();
    zero.expectedSize = 0;
    CHECK_EQ(downloader.Download(zero, {}, {}).error().code, ErrorCode::InvalidArgument);

    UpdateDownloaderOptions relative;
    relative.updatesDir = L"relative\\updates";
    relative.repository = kTestRepository;
    UpdateDownloader relativeDownloader(h.http, std::move(relative));
    CHECK_EQ(relativeDownloader.Download(h.Request(), {}, {}).error().code, ErrorCode::InvalidArgument);
    CHECK(h.http.Requests().empty());

    auto directory = downloader.VersionDirectory(SemanticVersion::Parse("1.2.0+build.7").value());
    REQUIRE(directory.ok());
    CHECK(fs::IsPathWithin(downloader.DownloadsRoot(), *directory));
    CHECK(directory->filename() == L"1.2.0+build.7");
}

AETHER_TEST("downloader: cancellation stops the transfer and removes the partial file") {
    {
        DownloadHarness h;
        h.http.Set(h.url, Ok200(h.payload));
        CancellationSource before;
        before.Cancel();
        CHECK_EQ(h.Download(before.Token()).error().code, ErrorCode::Cancelled);
        CHECK(h.http.Requests().empty());
    }
    {
        DownloadHarness h;
        h.http.Set(h.url, Ok200(h.payload));
        CancellationSource midway;
        h.http.onChunk = [&](std::uint64_t delivered) {
            if (delivered >= 64 * 1024) midway.Cancel();
        };
        auto result = h.Download(midway.Token());
        REQUIRE_FALSE(result.ok());
        CHECK_EQ(result.error().code, ErrorCode::Cancelled);
        CHECK_EQ(h.http.CountFor(h.url), 1u);
        CHECK(h.NothingLeft());
    }
    {
        // Cancelled while waiting to retry.
        DownloadHarness h;
        FakeResponse interrupted = Ok200(h.payload);
        interrupted.failAfter = 10;
        h.http.Set(h.url, interrupted);
        CancellationSource during;
        UpdateDownloaderOptions options;
        options.updatesDir = h.root / L"updates";
        options.repository = kTestRepository;
        options.diskSpace = [](const std::filesystem::path&) -> Result<std::uint64_t> { return 1ull << 40; };
        options.retryWait = [&](std::chrono::milliseconds, const CancellationToken&) {
            during.Cancel();
            return false;
        };
        UpdateDownloader cancelling(h.http, std::move(options));
        auto result = cancelling.Download(h.Request(), {}, during.Token());
        REQUIRE_FALSE(result.ok());
        CHECK_EQ(result.error().code, ErrorCode::Cancelled);
        CHECK_EQ(h.http.CountFor(h.url), 1u);
        CHECK(h.NothingLeft());
    }
}

AETHER_TEST("downloader: a verified earlier download is reused, a stale one replaced") {
    DownloadHarness h;
    h.http.Set(h.url, Ok200(h.payload));
    REQUIRE(h.Download().ok());
    CHECK_EQ(h.http.CountFor(h.url), 1u);
    REQUIRE(h.Download().ok());
    CHECK_EQ(h.http.CountFor(h.url), 1u);  // reused after re-hashing, not downloaded again

    // A corrupted file under the final name is never trusted.
    auto damaged = h.payload;
    damaged[0] = static_cast<char>(damaged[0] ^ 1);
    REQUIRE(fs::WriteFileAtomic(h.FinalPath(), damaged).ok());
    REQUIRE(h.Download().ok());
    CHECK_EQ(h.http.CountFor(h.url), 2u);
    CHECK(ReadAll(h.FinalPath()) == h.payload);
}

AETHER_TEST("downloader: the Authenticode policy is enforced before the file gets its final name") {
    // The test payload is not a signed PE file: Required refuses it, RequiredIfPresent accepts it.
    DownloadHarness required;
    required.authenticode = AuthenticodePolicy::Required;
    required.http.Set(required.url, Ok200(required.payload));
    auto refused = required.Download();
    REQUIRE_FALSE(refused.ok());
    CHECK_EQ(refused.error().code, ErrorCode::Signature);
    CHECK(required.NothingLeft());

    DownloadHarness ifPresent;
    ifPresent.authenticode = AuthenticodePolicy::RequiredIfPresent;
    ifPresent.http.Set(ifPresent.url, Ok200(ifPresent.payload));
    CHECK(ifPresent.Download().ok());

    // A file placed earlier under the final name is re-checked against the policy as well.
    DownloadHarness reuse;
    reuse.http.Set(reuse.url, Ok200(reuse.payload));
    REQUIRE(reuse.Download().ok());
    reuse.authenticode = AuthenticodePolicy::Required;
    CHECK_EQ(reuse.Download().error().code, ErrorCode::Signature);
    CHECK(reuse.NothingLeft());
}

AETHER_TEST("authenticode: unsigned files and policies") {
    const auto directory = test::MakeTempDirectory("updates-authenticode");
    const auto text = directory / L"payload.bin";
    REQUIRE(fs::WriteFileAtomic(text, "not a portable executable").ok());
    auto plain = CheckAuthenticode(text);
    REQUIRE(plain.ok());
    CHECK(plain->status == AuthenticodeStatus::NotSigned);

    // System binaries are catalog-signed; a copy has no embedded signature, which is all we check.
    wchar_t system[MAX_PATH] = {};
    REQUIRE(::GetSystemDirectoryW(system, MAX_PATH) > 0);
    const auto copy = directory / L"copy.exe";
    std::error_code ec;
    std::filesystem::copy_file(std::filesystem::path(system) / L"notepad.exe", copy, ec);
    if (!ec) {
        auto unsignedCopy = CheckAuthenticode(copy);
        REQUIRE(unsignedCopy.ok());
        CHECK(unsignedCopy->status == AuthenticodeStatus::NotSigned);
    } else {
        TEST_NOTE("notepad.exe not available; skipped the PE copy check");
    }

    CHECK(EnforceAuthenticodePolicy(text, AuthenticodePolicy::NotRequired).ok());
    CHECK(EnforceAuthenticodePolicy(text, AuthenticodePolicy::RequiredIfPresent).ok());
    auto required = EnforceAuthenticodePolicy(text, AuthenticodePolicy::Required);
    REQUIRE_FALSE(required.ok());
    CHECK_EQ(required.error().code, ErrorCode::Signature);

    auto missing = CheckAuthenticode(directory / L"missing.exe");
    REQUIRE_FALSE(missing.ok());
    CHECK_EQ(missing.error().code, ErrorCode::NotFound);

    CHECK_EQ(ToString(AuthenticodePolicy::RequiredIfPresent), std::string_view("required-if-present"));
    CHECK_EQ(ToString(AuthenticodeStatus::Invalid), std::string_view("invalid"));
}

AETHER_TEST("authenticode: an embedded signature is validated when a signed file is provided") {
    // Opt-in: no file with an embedded Authenticode signature is guaranteed to exist on every machine
    // (Windows binaries are catalog-signed), so set AETHER_TEST_SIGNED_PE to one, for example
    // python.exe from python.org. Revocation checking may need the network.
    char* value = nullptr;
    std::size_t length = 0;
    if (_dupenv_s(&value, &length, "AETHER_TEST_SIGNED_PE") != 0 || value == nullptr) {
        TEST_NOTE("AETHER_TEST_SIGNED_PE is not set; skipped");
        return;
    }
    const std::filesystem::path signedFile = text::Widen(value);
    std::free(value);

    const auto directory = test::MakeTempDirectory("updates-authenticode-signed");
    // The check works on the content, so the ".partial" name used before the rename is fine.
    const auto partial = directory / L"installer.exe.partial";
    std::filesystem::copy_file(signedFile, partial);
    auto checked = CheckAuthenticode(partial);
    REQUIRE(checked.ok());
    CHECK(checked->status == AuthenticodeStatus::Valid);
    CHECK(EnforceAuthenticodePolicy(partial, AuthenticodePolicy::Required).ok());

    // Flip one byte in the middle of the image: the signature no longer matches.
    auto bytes = fs::ReadFile(partial, 1u << 30).value();
    bytes[bytes.size() / 2] = static_cast<char>(bytes[bytes.size() / 2] ^ 0xFF);
    const auto tampered = directory / L"tampered.exe";
    REQUIRE(fs::WriteFileAtomic(tampered, bytes).ok());
    auto broken = CheckAuthenticode(tampered);
    REQUIRE(broken.ok());
    CHECK(broken->status == AuthenticodeStatus::Invalid);
    CHECK_FALSE(EnforceAuthenticodePolicy(tampered, AuthenticodePolicy::RequiredIfPresent).ok());
}

AETHER_TEST("downloader: superseded version directories are pruned conservatively") {
    DownloadHarness h;
    auto downloader = h.Make();
    const auto root = downloader.DownloadsRoot();
    for (const wchar_t* name : {L"1.0.0", L"1.1.0-beta.1", L"1.2.0", L"not-a-version", L"01.0.0"}) {
        REQUIRE(fs::EnsureDirectory(root / name).ok());
        REQUIRE(fs::WriteFileAtomic(root / name / L"file.bin", "x").ok());
    }
    REQUIRE(fs::WriteFileAtomic(root / L"1.0.1", "a file named like a version").ok());

    CHECK_EQ(downloader.PruneOtherVersions(SemanticVersion::Parse("1.2.0").value()), 2u);
    CHECK_FALSE(std::filesystem::exists(root / L"1.0.0"));
    CHECK_FALSE(std::filesystem::exists(root / L"1.1.0-beta.1"));
    CHECK(std::filesystem::exists(root / L"1.2.0" / L"file.bin"));
    CHECK(std::filesystem::exists(root / L"not-a-version"));
    CHECK(std::filesystem::exists(root / L"01.0.0"));
    CHECK(std::filesystem::exists(root / L"1.0.1"));

    UpdateDownloaderOptions fresh;
    fresh.updatesDir = h.root / L"never-created";
    fresh.repository = kTestRepository;
    CHECK_EQ(UpdateDownloader(h.http, std::move(fresh)).PruneOtherVersions(SemanticVersion(1, 0, 0)), 0u);
}

AETHER_TEST("downloader: small assets are fetched with the same rules") {
    DownloadHarness h;
    auto downloader = h.Make();
    const auto manifestUrl = DownloadUrl(kTag, ManifestName("1.2.0"));
    h.http.Set(manifestUrl, Ok200(std::string(fixtures::kStable_1_2_0.manifest)));
    auto fetched = downloader.FetchSmallAsset(manifestUrl, 64 * 1024, {});
    REQUIRE(fetched.ok());
    CHECK(*fetched == fixtures::kStable_1_2_0.manifest);
    const auto request = h.http.LastRequestFor(manifestUrl);
    REQUIRE(request.has_value());
    CHECK_EQ(request->maxResponseBytes, 64u * 1024);
    CHECK_EQ(request->allowedRedirectHosts.size(), 3u);

    CHECK_EQ(downloader.FetchSmallAsset(manifestUrl, 10, {}).error().code, ErrorCode::Integrity);
    CHECK_EQ(downloader.FetchSmallAsset("https://evil.example/m.json", 1024, {}).error().code,
             ErrorCode::InvalidArgument);
    h.http.Set(manifestUrl, Status(404));
    CHECK_EQ(downloader.FetchSmallAsset(manifestUrl, 1024, {}).error().code, ErrorCode::NotFound);
    FakeResponse foreign = Ok200("{}");
    foreign.finalUrl = "https://evil.example/m.json";
    h.http.Set(manifestUrl, foreign);
    CHECK_EQ(downloader.FetchSmallAsset(manifestUrl, 1024, {}).error().code, ErrorCode::Validation);
}

AETHER_TEST("downloader: the default retry wait is cancellable") {
    CHECK(WaitCancellable(1ms, {}));
    CancellationSource source;
    std::thread canceller([&] {
        std::this_thread::sleep_for(20ms);
        source.Cancel();
    });
    const auto started = std::chrono::steady_clock::now();
    CHECK_FALSE(WaitCancellable(10s, source.Token()));
    CHECK(std::chrono::steady_clock::now() - started < 5s);
    canceller.join();
    CHECK_FALSE(WaitCancellable(1ms, source.Token()));
}

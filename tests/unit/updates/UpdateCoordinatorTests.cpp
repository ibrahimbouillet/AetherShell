// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
#include "AetherTest.h"
#include "UpdateTestSupport.h"

#include "core/Dispatcher.h"
#include "core/FileSystem.h"
#include "core/Time.h"
#include "updates/UpdateCoordinator.h"
#include "updates/UpdateStateStore.h"

#include <array>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <vector>

using namespace aether;
using namespace aether::updates;
using namespace aether::updates::testing;
using namespace std::chrono_literals;

namespace {

struct CoordinatorHarness {
    std::filesystem::path root = test::MakeTempDirectory("updates-coordinator");
    FakeHttpClient http;
    FakeTimeSource time;
    ManualDispatcher dispatcher;
    std::shared_ptr<FakeUpdateTimer> timer = std::make_shared<FakeUpdateTimer>();
    std::atomic<std::uint64_t> freeSpace{1ull << 40};
    std::vector<UpdateSnapshot> published;
    bool listenersOnUiThread = true;
    Connection connection;
    std::unique_ptr<UpdateCoordinator> coordinator;  // declared last: destroyed first

    ~CoordinatorHarness() {
        if (coordinator) coordinator->Stop();
    }

    UpdateCoordinatorOptions Options() {
        UpdateCoordinatorOptions options;
        options.configDir = root / L"config";
        options.updatesDir = root / L"updates";
        options.repository = kTestRepository;
        options.currentVersion = "1.1.0";
        options.architecture = Architecture::X64;
        options.osBuild = 22631;
        options.userAgent = "AetherShellTests/1.0";
        options.timer = timer;
        options.diskSpace = [this](const std::filesystem::path&) -> Result<std::uint64_t> { return freeSpace.load(); };
        options.downloadRetryWait = [](std::chrono::milliseconds, const CancellationToken& cancel) {
            return !cancel.IsCancelled();
        };
        return options;
    }

    void Create(std::optional<EcdsaP256Verifier> verifier = std::nullopt,
                const std::function<void(UpdateCoordinatorOptions&)>& tweak = {}) {
        auto options = Options();
        if (tweak) tweak(options);
        auto created = UpdateCoordinator::Create(http, time, dispatcher, verifier ? std::move(*verifier) : TestVerifier(),
                                                 std::move(options));
        REQUIRE(created.ok());
        coordinator = std::move(created).value();
        connection = coordinator->Subscribe([this](const UpdateSnapshot& snapshot) {
            listenersOnUiThread = listenersOnUiThread && dispatcher.IsUiThread();
            published.push_back(snapshot);
        });
    }

    // Applies a policy the way the application does at startup (automatic checks off unless asked).
    void Configure(UpdateChannel channel = UpdateChannel::Stable, bool enabled = false,
                   std::chrono::hours interval = kDefaultCheckInterval,
                   std::optional<SemanticVersion> skipped = std::nullopt) {
        UpdatePolicy policy;
        policy.enabled = enabled;
        policy.channel = channel;
        policy.interval = interval;
        policy.skippedVersion = std::move(skipped);
        REQUIRE(coordinator->StartAutomaticChecks(policy).ok());
        Settle();
        published.clear();  // tests look at what happens after the configuration
    }

    void Settle() {
        REQUIRE(coordinator->WaitUntilIdle(30s));
        dispatcher.RunPending();
    }

    void CheckNow() {
        REQUIRE(coordinator->CheckNow().ok());
        Settle();
    }

    // Moves the fake clock to the next automatic check and lets the timer fire.
    void RunScheduledCheck() {
        const auto next = coordinator->Snapshot().nextCheck;
        REQUIRE(next.has_value());
        const auto now = time.SystemNow();
        if (*next > now) time.Advance(*next - now);
        REQUIRE(timer->Fire());
        Settle();
    }

    void Restart() {
        coordinator->Stop();
        connection.Disconnect();
        coordinator.reset();
        published.clear();
        Create();
    }

    [[nodiscard]] UpdateSnapshot Snap() const { return coordinator->Snapshot(); }

    [[nodiscard]] std::vector<UpdateState> States() const {
        std::vector<UpdateState> states;
        for (const auto& snapshot : published) states.push_back(snapshot.state);
        return states;
    }

    void ServeStable(const fixtures::SignedRelease& fixture, std::vector<net::HttpHeader> headers = {}) {
        const auto release = FixtureRelease(fixture);
        http.Set(std::string(kLatestUrl), Ok200(ReleaseBody(release), std::move(headers)));
        ServeFixtureAssets(http, fixture, release.tag);
    }

    [[nodiscard]] std::filesystem::path StateFile() const { return root / L"config" / L"update-state.json"; }
    [[nodiscard]] std::filesystem::path DownloadsDir() const { return root / L"updates" / L"downloads"; }
};

ErrorCode FailureCode(const UpdateSnapshot& snapshot) {
    return snapshot.failure ? snapshot.failure->error.code : ErrorCode::Unknown;
}

// Unknown for success, so a wrong outcome fails the check instead of aborting the whole test run.
ErrorCode CodeOf(const Result<void>& result) { return result ? ErrorCode::Unknown : result.error().code; }

std::string ReadAll(const std::filesystem::path& file) {
    auto contents = fs::ReadFile(file, 1u << 30);
    return contents ? *contents : std::string("<unreadable>");
}

SemanticVersion Version(std::string_view text) { return SemanticVersion::Parse(text).value(); }

}  // namespace

// --------------------------------------------------------------------------- check results

AETHER_TEST("coordinator: no published release means up to date") {
    CoordinatorHarness h;
    h.Create();
    CHECK(h.Snap().state == UpdateState::Disabled);
    CHECK_EQ(h.Snap().currentVersion.ToString(), std::string("1.1.0"));
    h.Configure();
    h.http.Set(std::string(kLatestUrl), Status(404));
    h.CheckNow();

    const auto snapshot = h.Snap();
    CHECK(snapshot.state == UpdateState::UpToDate);
    CHECK_FALSE(snapshot.latestVersion.has_value());
    CHECK_FALSE(snapshot.failure.has_value());
    REQUIRE(snapshot.lastCheck.has_value());
    CHECK(*snapshot.lastCheck == h.time.SystemNow());
    CHECK(h.States().back() == UpdateState::UpToDate);
    CHECK(h.listenersOnUiThread);

    auto persisted = LoadUpdateState(h.StateFile(), kTestRepository);
    REQUIRE(persisted.ok());
    REQUIRE(persisted->lastCheckUtc.has_value());
    CHECK(*persisted->lastCheckUtc == h.time.SystemNow());
}

AETHER_TEST("coordinator: the current or an older release is up to date") {
    for (const std::string_view current : {"1.2.0", "1.3.0", "1.2.0+local.build"}) {
        CoordinatorHarness h;
        h.Create(std::nullopt, [&](UpdateCoordinatorOptions& options) { options.currentVersion = std::string(current); });
        h.Configure();
        h.ServeStable(fixtures::kStable_1_2_0);
        h.CheckNow();
        const auto snapshot = h.Snap();
        CHECK(snapshot.state == UpdateState::UpToDate);
        REQUIRE(snapshot.latestVersion.has_value());
        CHECK_EQ(snapshot.latestVersion->ToString(), std::string("1.2.0"));
        // Nothing beyond the metadata is fetched when no update is offered.
        CHECK_EQ(h.http.CountFor(DownloadUrl("v1.2.0", ManifestName("1.2.0"))), 0u);
    }
}

AETHER_TEST("coordinator: a newer signed release is offered, downloaded and staged") {
    CoordinatorHarness h;
    h.Create();
    h.Configure();
    h.ServeStable(fixtures::kStable_1_2_0);
    h.CheckNow();

    auto offered = h.Snap();
    REQUIRE(offered.state == UpdateState::UpdateAvailable);
    CHECK(h.States() == (std::vector<UpdateState>{UpdateState::Checking, UpdateState::UpdateAvailable}));
    REQUIRE(offered.release != nullptr);
    CHECK_EQ(offered.release->tagName, std::string("v1.2.0"));
    REQUIRE(offered.manifest != nullptr);
    CHECK_EQ(offered.manifest->version.ToString(), std::string("1.2.0"));
    REQUIRE(offered.downloadSize.has_value());
    CHECK_EQ(*offered.downloadSize, 300000u);
    CHECK_EQ(offered.releaseNotes, std::string("## Changes\n\n* Faster dock"));  // sanitized
    REQUIRE(offered.latestVersion.has_value());
    CHECK_EQ(offered.latestVersion->ToString(), std::string("1.2.0"));
    CHECK_FALSE(offered.prepared.has_value());
    // The installer itself is downloaded only on request.
    CHECK_EQ(h.http.CountFor(DownloadUrl("v1.2.0", fixtures::kStable_1_2_0.assets[0].fileName)), 0u);

    REQUIRE(h.coordinator->DownloadAndPrepare().ok());
    h.Settle();
    const auto ready = h.Snap();
    REQUIRE(ready.state == UpdateState::ReadyToInstall);
    REQUIRE(ready.prepared.has_value());
    CHECK_EQ(ready.prepared->version.ToString(), std::string("1.2.0"));
    CHECK_EQ(ready.progress.received, 300000u);
    CHECK_EQ(ready.progress.total, 300000u);
    const auto versionDir = h.DownloadsDir() / L"1.2.0";
    CHECK(ready.prepared->packagePath == versionDir / L"AetherShell-1.2.0-win-x64-setup.exe");
    CHECK(ready.prepared->manifestPath == versionDir / L"AetherShell-1.2.0-manifest.json");
    CHECK(ready.prepared->signaturePath == versionDir / L"AetherShell-1.2.0-manifest.json.sig");
    CHECK(ReadAll(ready.prepared->packagePath) == TestAssetBytes(300000, 1));
    // The updater gets the exact signed bytes so it can verify them again independently.
    CHECK(ReadAll(ready.prepared->manifestPath) == fixtures::kStable_1_2_0.manifest);
    CHECK(ReadAll(ready.prepared->signaturePath) == fixtures::kStable_1_2_0.signature);
    CHECK(TestVerifier().VerifySignatureFile(ReadAll(ready.prepared->manifestPath),
                                             ReadAll(ready.prepared->signaturePath)).ok());

    bool sawDownloading = false;
    for (const auto& snapshot : h.published) sawDownloading = sawDownloading || snapshot.state == UpdateState::Downloading;
    CHECK(sawDownloading);
    CHECK(h.States().back() == UpdateState::ReadyToInstall);
    CHECK(h.listenersOnUiThread);

    // Already staged: nothing more to do.
    const auto before = h.http.Requests().size();
    REQUIRE(h.coordinator->DownloadAndPrepare().ok());
    h.Settle();
    CHECK_EQ(h.http.Requests().size(), before);
}

AETHER_TEST("coordinator: an unavailable API or no network is a retryable error") {
    CoordinatorHarness h;
    h.Create();
    h.Configure();
    h.http.Set(std::string(kLatestUrl), Status(503));
    h.CheckNow();
    auto snapshot = h.Snap();
    CHECK(snapshot.state == UpdateState::Error);
    CHECK_EQ(FailureCode(snapshot), ErrorCode::Unavailable);
    REQUIRE(snapshot.failure.has_value());
    CHECK(snapshot.failure->retryable);
    CHECK_FALSE(snapshot.lastCheck.has_value());  // only successful checks count

    h.http.Set(std::string(kLatestUrl), TransportError(ErrorCode::Network, "The server name could not be resolved"));
    h.CheckNow();
    snapshot = h.Snap();
    CHECK(snapshot.state == UpdateState::Error);
    CHECK_EQ(FailureCode(snapshot), ErrorCode::Network);
    CHECK(snapshot.failure->retryable);

    // A later success clears the error.
    h.http.Set(std::string(kLatestUrl), Status(404));
    h.CheckNow();
    CHECK(h.Snap().state == UpdateState::UpToDate);
    CHECK_FALSE(h.Snap().failure.has_value());
}

AETHER_TEST("coordinator: rate limits are retryable and delay the next automatic check") {
    CoordinatorHarness h;
    h.Create();
    h.Configure(UpdateChannel::Stable, true);
    const auto start = h.time.SystemNow();
    const auto resetEpoch =
        std::chrono::duration_cast<std::chrono::seconds>((start + 2min + 30min).time_since_epoch()).count();
    h.http.Set(std::string(kLatestUrl), Status(403, {{"x-ratelimit-remaining", "0"},
                                                     {"x-ratelimit-reset", std::to_string(resetEpoch)}}));
    h.RunScheduledCheck();
    auto snapshot = h.Snap();
    CHECK(snapshot.state == UpdateState::Error);
    CHECK_EQ(FailureCode(snapshot), ErrorCode::RateLimited);
    REQUIRE(snapshot.failure.has_value());
    CHECK(snapshot.failure->retryable);
    REQUIRE(snapshot.failure->retryAfter.has_value());
    const auto resetAt = std::chrono::system_clock::time_point(std::chrono::seconds(resetEpoch));
    CHECK(*snapshot.failure->retryAfter == resetAt);
    REQUIRE(snapshot.nextCheck.has_value());
    CHECK(*snapshot.nextCheck == resetAt);  // later than the 15 min back-off

    h.http.Set(std::string(kLatestUrl), Status(429, {{"retry-after", "7200"}}));
    h.RunScheduledCheck();
    snapshot = h.Snap();
    CHECK_EQ(FailureCode(snapshot), ErrorCode::RateLimited);
    CHECK(*snapshot.nextCheck == h.time.SystemNow() + 2h);
}

AETHER_TEST("coordinator: 304 Not Modified reuses the cached release, also after a restart") {
    CoordinatorHarness h;
    h.Create();
    h.Configure();
    h.ServeStable(fixtures::kStable_1_2_0, {{"ETag", "\"release-etag\""}});
    h.CheckNow();
    REQUIRE(h.Snap().state == UpdateState::UpdateAvailable);

    h.http.Set(std::string(kLatestUrl), Status(304));
    h.CheckNow();
    CHECK(h.Snap().state == UpdateState::UpdateAvailable);
    auto request = h.http.LastRequestFor(kLatestUrl);
    REQUIRE(request.has_value());
    CHECK(HasHeader(*request, "If-None-Match", "\"release-etag\""));

    auto persisted = LoadUpdateState(h.StateFile(), kTestRepository);
    REQUIRE(persisted.ok());
    REQUIRE(persisted->cache.has_value());
    CHECK_EQ(persisted->cache->etag, std::string("\"release-etag\""));
    CHECK_EQ(persisted->cache->release.tagName, std::string("v1.2.0"));

    h.Restart();
    h.Configure();
    h.CheckNow();
    CHECK(h.Snap().state == UpdateState::UpdateAvailable);
    request = h.http.LastRequestFor(kLatestUrl);
    REQUIRE(request.has_value());
    CHECK(HasHeader(*request, "If-None-Match", "\"release-etag\""));
}

// ----------------------------------------------------------------------- trust and compatibility

AETHER_TEST("coordinator: a manifest whose version differs from the tag is rejected") {
    CoordinatorHarness h;
    h.Create();
    h.Configure();
    // Release v1.3.0 carries the (validly signed) manifest of 1.2.0 under 1.3.0 names: a replay.
    TestRelease release;
    release.tag = "v1.3.0";
    release.assets = {{"AetherShell-1.3.0-win-x64-setup.exe", 300000, {}},
                      {ManifestName("1.3.0"), fixtures::kStable_1_2_0.manifest.size(), {}},
                      {SignatureName("1.3.0"), fixtures::kStable_1_2_0.signature.size(), {}}};
    h.http.Set(std::string(kLatestUrl), Ok200(ReleaseBody(release)));
    h.http.Set(DownloadUrl("v1.3.0", ManifestName("1.3.0")), Ok200(std::string(fixtures::kStable_1_2_0.manifest)));
    h.http.Set(DownloadUrl("v1.3.0", SignatureName("1.3.0")), Ok200(std::string(fixtures::kStable_1_2_0.signature)));
    h.CheckNow();
    const auto snapshot = h.Snap();
    CHECK(snapshot.state == UpdateState::Error);
    CHECK_EQ(FailureCode(snapshot), ErrorCode::Validation);
    CHECK_FALSE(snapshot.failure->retryable);
    CHECK(snapshot.release == nullptr);
    CHECK_EQ(CodeOf(h.coordinator->DownloadAndPrepare()), ErrorCode::InvalidState);  // nothing to download
}

AETHER_TEST("coordinator: the wrong architecture or an old Windows build is unsupported") {
    {
        CoordinatorHarness h;
        h.Create(std::nullopt, [](UpdateCoordinatorOptions& options) { options.architecture = Architecture::Arm64; });
        h.Configure();
        h.ServeStable(fixtures::kStable_1_2_0);  // x64 only
        h.CheckNow();
        CHECK(h.Snap().state == UpdateState::Error);
        CHECK_EQ(FailureCode(h.Snap()), ErrorCode::Unsupported);
        CHECK_FALSE(h.Snap().failure->retryable);
    }
    {
        CoordinatorHarness h;
        h.Create(std::nullopt, [](UpdateCoordinatorOptions& options) { options.osBuild = 19045; });
        h.Configure();
        h.ServeStable(fixtures::kStable_1_3_0_MultiArch);  // requires build 22000
        h.CheckNow();
        CHECK(h.Snap().state == UpdateState::Error);
        CHECK_EQ(FailureCode(h.Snap()), ErrorCode::Unsupported);
    }
    {
        CoordinatorHarness h;
        h.Create(std::nullopt, [](UpdateCoordinatorOptions& options) { options.architecture = Architecture::Arm64; });
        h.Configure();
        h.ServeStable(fixtures::kStable_1_3_0_MultiArch);
        h.CheckNow();
        REQUIRE(h.Snap().state == UpdateState::UpdateAvailable);
        CHECK_EQ(*h.Snap().downloadSize, 5000u);  // the arm64 installer
        REQUIRE(h.coordinator->DownloadAndPrepare().ok());
        h.Settle();
        REQUIRE(h.Snap().state == UpdateState::ReadyToInstall);
        CHECK(h.Snap().prepared->packagePath.filename() == L"AetherShell-1.3.0-win-arm64-setup.exe");
        CHECK(ReadAll(h.Snap().prepared->packagePath) == TestAssetBytes(5000, 4));
    }
}

AETHER_TEST("coordinator: invalid signatures are rejected") {
    const auto tampered = [] {
        std::string manifest(fixtures::kStable_1_2_0.manifest);
        manifest[manifest.find("19041")] = '2';  // "29041": a single changed byte
        return manifest;
    }();
    {
        CoordinatorHarness h;
        h.Create();
        h.Configure();
        h.ServeStable(fixtures::kStable_1_2_0);
        h.http.Set(DownloadUrl("v1.2.0", ManifestName("1.2.0")), Ok200(tampered));
        h.CheckNow();
        CHECK(h.Snap().state == UpdateState::Error);
        CHECK_EQ(FailureCode(h.Snap()), ErrorCode::Signature);
        CHECK_FALSE(h.Snap().failure->retryable);
    }
    {
        CoordinatorHarness h;
        h.Create(UnrelatedVerifier());  // signed, but not by a trusted key
        h.Configure();
        h.ServeStable(fixtures::kStable_1_2_0);
        h.CheckNow();
        CHECK_EQ(FailureCode(h.Snap()), ErrorCode::Signature);
    }
    {
        CoordinatorHarness h;
        h.Create();
        h.Configure();
        h.ServeStable(fixtures::kStable_1_2_0);
        h.http.Set(DownloadUrl("v1.2.0", SignatureName("1.2.0")), Ok200("garbage that is not base64\n"));
        h.CheckNow();
        CHECK_EQ(FailureCode(h.Snap()), ErrorCode::Signature);
    }
    {
        // No signature asset at all: the release is not installable.
        CoordinatorHarness h;
        h.Create();
        h.Configure();
        auto release = FixtureRelease(fixtures::kStable_1_2_0);
        std::erase_if(release.assets, [](const TestAsset& asset) { return asset.name.ends_with(".sig"); });
        h.http.Set(std::string(kLatestUrl), Ok200(ReleaseBody(release)));
        ServeFixtureAssets(h.http, fixtures::kStable_1_2_0, "v1.2.0");
        h.CheckNow();
        CHECK_EQ(FailureCode(h.Snap()), ErrorCode::Validation);
        CHECK_EQ(h.http.CountFor(DownloadUrl("v1.2.0", ManifestName("1.2.0"))), 0u);
    }
    {
        // The GitHub asset size disagrees with the signed manifest.
        CoordinatorHarness h;
        h.Create();
        h.Configure();
        auto release = FixtureRelease(fixtures::kStable_1_2_0);
        release.assets[0].size = 299999;
        h.http.Set(std::string(kLatestUrl), Ok200(ReleaseBody(release)));
        ServeFixtureAssets(h.http, fixtures::kStable_1_2_0, "v1.2.0");
        h.CheckNow();
        CHECK_EQ(FailureCode(h.Snap()), ErrorCode::Integrity);
    }
}

AETHER_TEST("coordinator: untrusted redirects are refused") {
    CoordinatorHarness h;
    h.Create();
    h.Configure();
    h.http.Set(std::string(kLatestUrl),
               TransportError(ErrorCode::Validation, "redirect to untrusted host 'evil.example' rejected"));
    h.CheckNow();
    CHECK(h.Snap().state == UpdateState::Error);
    CHECK_EQ(FailureCode(h.Snap()), ErrorCode::Validation);
    CHECK_FALSE(h.Snap().failure->retryable);
    auto api = h.http.LastRequestFor(kLatestUrl);
    REQUIRE(api.has_value());
    CHECK(api->allowedRedirectHosts == std::vector<std::string>{"api.github.com"});

    h.ServeStable(fixtures::kStable_1_2_0);
    const auto manifestUrl = DownloadUrl("v1.2.0", ManifestName("1.2.0"));
    h.http.Set(manifestUrl, TransportError(ErrorCode::Validation, "redirect to untrusted host 'evil.example' rejected"));
    h.CheckNow();
    CHECK_EQ(FailureCode(h.Snap()), ErrorCode::Validation);
    auto manifestRequest = h.http.LastRequestFor(manifestUrl);
    REQUIRE(manifestRequest.has_value());
    CHECK(manifestRequest->allowedRedirectHosts ==
          (std::vector<std::string>{"github.com", "objects.githubusercontent.com", "release-assets.githubusercontent.com"}));
}

// ------------------------------------------------------------------------------------ downloads

AETHER_TEST("coordinator: insufficient disk space is retryable and keeps the offer") {
    CoordinatorHarness h;
    h.Create();
    h.Configure();
    h.ServeStable(fixtures::kStable_1_2_0);
    h.CheckNow();
    REQUIRE(h.Snap().state == UpdateState::UpdateAvailable);

    h.freeSpace = 1000;
    REQUIRE(h.coordinator->DownloadAndPrepare().ok());
    h.Settle();
    auto snapshot = h.Snap();
    CHECK(snapshot.state == UpdateState::Error);
    CHECK_EQ(FailureCode(snapshot), ErrorCode::InsufficientSpace);
    CHECK(snapshot.failure->retryable);
    CHECK(snapshot.release != nullptr);  // the offer is still shown
    CHECK_EQ(h.http.CountFor(DownloadUrl("v1.2.0", fixtures::kStable_1_2_0.assets[0].fileName)), 0u);

    h.freeSpace = 1ull << 40;
    REQUIRE(h.coordinator->DownloadAndPrepare().ok());
    h.Settle();
    CHECK(h.Snap().state == UpdateState::ReadyToInstall);
}

AETHER_TEST("coordinator: a corrupted or interrupted download never becomes ReadyToInstall") {
    CoordinatorHarness h;
    h.Create();
    h.Configure();
    h.ServeStable(fixtures::kStable_1_2_0);
    const auto installerUrl = DownloadUrl("v1.2.0", fixtures::kStable_1_2_0.assets[0].fileName);
    auto corrupted = TestAssetBytes(300000, 1);
    corrupted[12345] = static_cast<char>(corrupted[12345] ^ 0x10);
    h.http.Set(installerUrl, Ok200(corrupted));
    h.CheckNow();
    REQUIRE(h.coordinator->DownloadAndPrepare().ok());
    h.Settle();
    auto snapshot = h.Snap();
    CHECK(snapshot.state == UpdateState::Error);
    CHECK_EQ(FailureCode(snapshot), ErrorCode::Integrity);
    CHECK_FALSE(snapshot.failure->retryable);
    CHECK_FALSE(snapshot.prepared.has_value());
    CHECK_FALSE(std::filesystem::exists(h.DownloadsDir() / L"1.2.0" / L"AetherShell-1.2.0-win-x64-setup.exe"));
    CHECK_FALSE(std::filesystem::exists(h.DownloadsDir() / L"1.2.0" / L"AetherShell-1.2.0-win-x64-setup.exe.partial"));

    // Interrupted every time: bounded retries, then a retryable error.
    FakeResponse interrupted = Ok200(TestAssetBytes(300000, 1));
    interrupted.failAfter = 1000;
    h.http.Set(installerUrl, interrupted);
    REQUIRE(h.coordinator->DownloadAndPrepare().ok());
    h.Settle();
    snapshot = h.Snap();
    CHECK_EQ(FailureCode(snapshot), ErrorCode::Network);
    CHECK(snapshot.failure->retryable);
    CHECK_EQ(h.http.CountFor(installerUrl), 1u + 3u);

    // Postponing the error goes back to offering the update quietly.
    REQUIRE(h.coordinator->Postpone().ok());
    h.Settle();
    CHECK(h.Snap().state == UpdateState::Disabled);
    CHECK(h.Snap().release != nullptr);
}

AETHER_TEST("coordinator: cancellation returns to a resting state") {
    {
        CoordinatorHarness h;
        h.Create();
        h.Configure();
        h.ServeStable(fixtures::kStable_1_2_0);
        h.CheckNow();
        h.http.onChunk = [&](std::uint64_t delivered) {
            if (delivered >= 32 * 1024) h.coordinator->Cancel();
        };
        REQUIRE(h.coordinator->DownloadAndPrepare().ok());
        h.Settle();
        CHECK(h.Snap().state == UpdateState::UpdateAvailable);
        CHECK_FALSE(h.Snap().failure.has_value());
        CHECK_FALSE(std::filesystem::exists(h.DownloadsDir() / L"1.2.0" / L"AetherShell-1.2.0-win-x64-setup.exe"));
        h.http.onChunk = nullptr;
        REQUIRE(h.coordinator->DownloadAndPrepare().ok());
        h.Settle();
        CHECK(h.Snap().state == UpdateState::ReadyToInstall);
    }
    {
        CoordinatorHarness h;
        h.Create();
        h.Configure();
        h.ServeStable(fixtures::kStable_1_2_0);
        h.http.onRequest = [&](const net::HttpRequest& request) {
            if (request.url == kLatestUrl) h.coordinator->Cancel();
        };
        h.CheckNow();
        CHECK(h.Snap().state == UpdateState::Disabled);
        CHECK_FALSE(h.Snap().failure.has_value());
        CHECK_FALSE(h.Snap().lastCheck.has_value());
        CHECK_EQ(h.http.CountFor(DownloadUrl("v1.2.0", ManifestName("1.2.0"))), 0u);
    }
}

AETHER_TEST("coordinator: operations that conflict are refused") {
    CoordinatorHarness h;
    h.Create();
    h.Configure();
    CHECK_EQ(CodeOf(h.coordinator->DownloadAndPrepare()), ErrorCode::InvalidState);  // nothing offered

    h.ServeStable(fixtures::kStable_1_2_0);
    std::optional<ErrorCode> downloadDuringCheck;
    h.http.onRequest = [&](const net::HttpRequest& request) {
        if (request.url == kLatestUrl && !downloadDuringCheck) {
            auto result = h.coordinator->DownloadAndPrepare();
            downloadDuringCheck = result ? ErrorCode::Unknown : result.error().code;
            CHECK(h.coordinator->CheckNow().ok());  // coalesced with the running check
        }
    };
    h.CheckNow();
    h.http.onRequest = nullptr;
    CHECK(downloadDuringCheck == ErrorCode::InvalidState);
    CHECK_EQ(h.http.CountFor(kLatestUrl), 1u);

    std::optional<ErrorCode> checkDuringDownload;
    std::optional<ErrorCode> skipDuringDownload;
    h.http.onChunk = [&](std::uint64_t) {
        if (!checkDuringDownload) {
            auto check = h.coordinator->CheckNow();
            checkDuringDownload = check ? ErrorCode::Unknown : check.error().code;
            auto skip = h.coordinator->SkipVersion(Version("1.2.0"));
            skipDuringDownload = skip ? ErrorCode::Unknown : skip.error().code;
            CHECK(h.coordinator->DownloadAndPrepare().ok());  // already running
        }
    };
    REQUIRE(h.coordinator->DownloadAndPrepare().ok());
    h.Settle();
    CHECK(checkDuringDownload == ErrorCode::InvalidState);
    CHECK(skipDuringDownload == ErrorCode::InvalidState);
    CHECK(h.Snap().state == UpdateState::ReadyToInstall);
}

// ------------------------------------------------------------------------------ user decisions

AETHER_TEST("coordinator: a skipped version is not announced by automatic checks") {
    CoordinatorHarness h;
    h.Create();
    h.Configure(UpdateChannel::Stable, true);
    h.ServeStable(fixtures::kStable_1_2_0);
    REQUIRE(h.coordinator->SkipVersion(Version("1.2.0")).ok());
    h.Settle();
    h.RunScheduledCheck();
    auto snapshot = h.Snap();
    CHECK(snapshot.state == UpdateState::UpToDate);
    REQUIRE(snapshot.latestVersion.has_value());
    CHECK_EQ(snapshot.latestVersion->ToString(), std::string("1.2.0"));
    REQUIRE(snapshot.skippedVersion.has_value());
    CHECK_EQ(snapshot.skippedVersion->ToString(), std::string("1.2.0"));
    CHECK_EQ(h.http.CountFor(DownloadUrl("v1.2.0", ManifestName("1.2.0"))), 0u);

    auto persisted = LoadUpdateState(h.StateFile(), kTestRepository);
    REQUIRE(persisted.ok());
    REQUIRE(persisted->skippedVersion.has_value());
    CHECK_EQ(persisted->skippedVersion->ToString(), std::string("1.2.0"));

    // The skip survives a restart; an explicit check still shows the version.
    h.Restart();
    h.Configure(UpdateChannel::Stable, true);
    REQUIRE(h.Snap().skippedVersion.has_value());
    h.RunScheduledCheck();
    CHECK(h.Snap().state == UpdateState::UpToDate);
    h.CheckNow();
    CHECK(h.Snap().state == UpdateState::UpdateAvailable);

    // Skipping the offered version withdraws the offer.
    REQUIRE(h.coordinator->SkipVersion(Version("1.2.0")).ok());
    h.Settle();
    CHECK(h.Snap().state == UpdateState::Idle);
    CHECK(h.Snap().release == nullptr);
    CHECK_EQ(CodeOf(h.coordinator->DownloadAndPrepare()), ErrorCode::InvalidState);

    // A newer version than the skipped one is announced again.
    CoordinatorHarness newer;
    newer.Create();
    newer.Configure(UpdateChannel::Stable, true, kDefaultCheckInterval, Version("1.2.0"));
    newer.ServeStable(fixtures::kStable_1_3_0_MultiArch);
    newer.RunScheduledCheck();
    CHECK(newer.Snap().state == UpdateState::UpdateAvailable);
}

AETHER_TEST("coordinator: postponing dismisses the announcement but keeps the offer") {
    CoordinatorHarness h;
    h.Create();
    h.Configure(UpdateChannel::Stable, true);
    h.ServeStable(fixtures::kStable_1_2_0);
    h.CheckNow();
    REQUIRE(h.Snap().state == UpdateState::UpdateAvailable);
    REQUIRE(h.coordinator->Postpone().ok());
    h.Settle();
    CHECK(h.Snap().state == UpdateState::Idle);
    CHECK(h.States().back() == UpdateState::Idle);

    // The next automatic check announces it again; the offer can be downloaded at any time.
    h.RunScheduledCheck();
    CHECK(h.Snap().state == UpdateState::UpdateAvailable);
    REQUIRE(h.coordinator->Postpone().ok());
    REQUIRE(h.coordinator->DownloadAndPrepare().ok());
    h.Settle();
    CHECK(h.Snap().state == UpdateState::ReadyToInstall);
    REQUIRE(h.coordinator->Postpone().ok());
    CHECK(h.Snap().state == UpdateState::ReadyToInstall);
}

AETHER_TEST("coordinator: the prerelease channel offers prereleases and ignores drafts") {
    CoordinatorHarness h;
    h.Create();
    h.Configure(UpdateChannel::Prerelease);
    CHECK(h.Snap().channel == UpdateChannel::Prerelease);
    auto draft = FixtureRelease(fixtures::kStable_1_3_0_MultiArch, "v3.0.0");
    draft.draft = true;
    const auto beta = FixtureRelease(fixtures::kPrerelease_2_0_0_beta_2);
    TestRelease stable;
    stable.tag = "v1.2.0";
    h.http.Set(std::string(kListUrl), Ok200(ReleaseListBody({draft, beta, stable})));
    ServeFixtureAssets(h.http, fixtures::kPrerelease_2_0_0_beta_2, beta.tag);
    h.CheckNow();
    const auto snapshot = h.Snap();
    REQUIRE(snapshot.state == UpdateState::UpdateAvailable);
    CHECK_EQ(snapshot.manifest->version.ToString(), std::string("2.0.0-beta.2"));
    CHECK(snapshot.manifest->channel == UpdateChannel::Prerelease);
    CHECK(snapshot.release->prerelease);
    CHECK_EQ(h.http.CountFor(kLatestUrl), 0u);
    CHECK_EQ(h.http.CountFor(kListUrl), 1u);

    // Switching back to stable drops the prerelease offer.
    h.Configure(UpdateChannel::Stable);
    CHECK(h.Snap().release == nullptr);
    CHECK(h.Snap().channel == UpdateChannel::Stable);
}

AETHER_TEST("coordinator: a channel change during a check never offers the old channel's release") {
    const auto beta = FixtureRelease(fixtures::kPrerelease_2_0_0_beta_2);
    const auto switchToStable = [](CoordinatorHarness& h) {
        UpdatePolicy policy;
        policy.enabled = false;
        policy.channel = UpdateChannel::Stable;
        CHECK(h.coordinator->StartAutomaticChecks(policy).ok());
    };
    {
        // The switch arrives after the metadata was read, while the manifest is being fetched.
        CoordinatorHarness h;
        h.Create();
        h.Configure(UpdateChannel::Prerelease);
        h.http.Set(std::string(kListUrl), Ok200(ReleaseListBody({beta})));
        ServeFixtureAssets(h.http, fixtures::kPrerelease_2_0_0_beta_2, beta.tag);
        bool switched = false;
        h.http.onRequest = [&](const net::HttpRequest& request) {
            if (!switched && request.url == DownloadUrl(beta.tag, ManifestName("2.0.0-beta.2"))) {
                switched = true;
                switchToStable(h);
            }
        };
        h.CheckNow();
        h.Settle();
        h.http.onRequest = nullptr;
        REQUIRE(switched);
        const auto snapshot = h.Snap();
        CHECK(snapshot.channel == UpdateChannel::Stable);
        CHECK(snapshot.state == UpdateState::Disabled);
        CHECK(snapshot.release == nullptr);
        CHECK(snapshot.manifest == nullptr);
        CHECK_FALSE(snapshot.failure.has_value());
        CHECK_EQ(CodeOf(h.coordinator->DownloadAndPrepare()), ErrorCode::InvalidState);

        // The next check asks the stable channel.
        h.http.Set(std::string(kLatestUrl), Status(404));
        h.CheckNow();
        CHECK(h.Snap().state == UpdateState::UpToDate);
        CHECK_EQ(h.http.CountFor(kLatestUrl), 1u);
    }
    {
        // A prerelease offer already shown is withdrawn even though a re-check is running.
        CoordinatorHarness h;
        h.Create();
        h.Configure(UpdateChannel::Prerelease);
        h.http.Set(std::string(kListUrl), Ok200(ReleaseListBody({beta})));
        ServeFixtureAssets(h.http, fixtures::kPrerelease_2_0_0_beta_2, beta.tag);
        h.CheckNow();
        REQUIRE(h.Snap().state == UpdateState::UpdateAvailable);
        h.http.onRequest = [&](const net::HttpRequest& request) {
            if (request.url == kListUrl) switchToStable(h);
        };
        h.CheckNow();
        h.Settle();
        h.http.onRequest = nullptr;
        CHECK(h.Snap().state == UpdateState::Disabled);
        CHECK(h.Snap().release == nullptr);
        CHECK_EQ(CodeOf(h.coordinator->DownloadAndPrepare()), ErrorCode::InvalidState);
    }
}

AETHER_TEST("coordinator: an offer withdrawn during a failing check is not left in the snapshot") {
    CoordinatorHarness h;
    h.Create();
    h.Configure();
    h.ServeStable(fixtures::kStable_1_2_0);
    h.CheckNow();
    REQUIRE(h.Snap().state == UpdateState::UpdateAvailable);

    h.http.Set(std::string(kLatestUrl), Status(503));
    h.http.onRequest = [&](const net::HttpRequest& request) {
        if (request.url == kLatestUrl) CHECK(h.coordinator->SkipVersion(Version("1.2.0")).ok());
    };
    h.CheckNow();
    h.Settle();
    h.http.onRequest = nullptr;
    const auto snapshot = h.Snap();
    CHECK(snapshot.state == UpdateState::Error);
    CHECK_EQ(FailureCode(snapshot), ErrorCode::Unavailable);
    CHECK(snapshot.release == nullptr);
    CHECK(snapshot.manifest == nullptr);
    CHECK_FALSE(snapshot.downloadSize.has_value());
    CHECK_EQ(CodeOf(h.coordinator->DownloadAndPrepare()), ErrorCode::InvalidState);
}

// ----------------------------------------------------------------------------------- scheduling

AETHER_TEST("coordinator: the first automatic check runs 2 minutes after start, then every interval") {
    CoordinatorHarness h;
    h.Create();
    const auto start = h.time.SystemNow();
    h.Configure(UpdateChannel::Stable, true);
    auto snapshot = h.Snap();
    CHECK(snapshot.state == UpdateState::Idle);
    CHECK(snapshot.automaticChecks);
    REQUIRE(snapshot.nextCheck.has_value());
    CHECK(*snapshot.nextCheck == start + 2min);
    CHECK(h.timer->IsArmed());
    CHECK(h.timer->Delay() == 2min);

    // Firing early (for example a coalesced wake-up) does not check, it waits for the rest.
    h.time.Advance(1min);
    REQUIRE(h.timer->Fire());
    h.Settle();
    CHECK_EQ(h.http.CountFor(kLatestUrl), 0u);
    CHECK(h.timer->IsArmed());
    CHECK(h.timer->Delay() == 1min);

    h.time.Advance(1min);
    REQUIRE(h.timer->Fire());
    h.Settle();
    CHECK_EQ(h.http.CountFor(kLatestUrl), 1u);
    snapshot = h.Snap();
    CHECK(snapshot.state == UpdateState::UpToDate);
    CHECK(*snapshot.nextCheck == h.time.SystemNow() + 24h);
    CHECK(h.timer->Delay() == 60min);  // long waits are split so sleep and clock changes are caught up

    h.time.Advance(60min);
    REQUIRE(h.timer->Fire());
    h.Settle();
    CHECK_EQ(h.http.CountFor(kLatestUrl), 1u);
    h.RunScheduledCheck();
    CHECK_EQ(h.http.CountFor(kLatestUrl), 2u);

    // A clock change is re-evaluated at once.
    h.time.Advance(25h);
    h.coordinator->NotifyClockChanged();
    h.Settle();
    CHECK_EQ(h.http.CountFor(kLatestUrl), 3u);
}

AETHER_TEST("coordinator: the persisted last check drives the schedule across restarts") {
    const auto expectNext = [](std::optional<std::chrono::hours> lastCheckAgo, std::chrono::hours offset,
                               bool expectFirstCheckDelay) {
        CoordinatorHarness h;
        const auto now = h.time.SystemNow();
        if (lastCheckAgo) {
            PersistedUpdateState state;
            state.lastCheckUtc = now - *lastCheckAgo + offset;
            REQUIRE(SaveUpdateState(h.StateFile(), state).ok());
        }
        h.Create();
        h.Configure(UpdateChannel::Stable, true);
        const auto next = h.Snap().nextCheck;
        REQUIRE(next.has_value());
        if (expectFirstCheckDelay) {
            CHECK(*next == now + 2min);
        } else {
            CHECK(*next == now - *lastCheckAgo + offset + 24h);
        }
    };
    expectNext(std::nullopt, 0h, true);           // never checked
    expectNext(1h, 0h, false);                    // checked an hour ago: 23 h to go
    expectNext(30h, 0h, true);                    // overdue: still not before the first-check delay
    expectNext(0h, 24h, true);                    // "last check" in the future (clock moved back): ignored
}

AETHER_TEST("coordinator: a clock moved backwards does not stall automatic checks") {
    CoordinatorHarness h;
    h.Create();
    h.Configure(UpdateChannel::Stable, true);
    h.http.Set(std::string(kLatestUrl), Status(404));
    h.RunScheduledCheck();
    REQUIRE(h.Snap().state == UpdateState::UpToDate);
    CHECK(*h.Snap().nextCheck == h.time.SystemNow() + 24h);

    // The clock is corrected 30 days backwards (it had been set wrongly ahead when the check ran).
    h.time.Advance(-std::chrono::hours(24 * 30));
    h.coordinator->NotifyClockChanged();
    h.Settle();
    REQUIRE(h.Snap().nextCheck.has_value());
    CHECK(*h.Snap().nextCheck == h.time.SystemNow() + 24h);  // one interval, not 31 days
    h.RunScheduledCheck();
    CHECK_EQ(h.http.CountFor(kLatestUrl), 2u);

    // The same bound applies to a back-off computed before the clock went back.
    h.http.Set(std::string(kLatestUrl), Status(502));
    h.RunScheduledCheck();
    REQUIRE(h.Snap().state == UpdateState::Error);
    h.time.Advance(-std::chrono::hours(24 * 30));
    h.coordinator->NotifyClockChanged();
    h.Settle();
    CHECK(*h.Snap().nextCheck <= h.time.SystemNow() + 24h);
}

AETHER_TEST("coordinator: the interval is at least 6 hours") {
    CoordinatorHarness h;
    h.Create();
    h.Configure(UpdateChannel::Stable, true, 1h);
    h.RunScheduledCheck();
    CHECK(*h.Snap().nextCheck == h.time.SystemNow() + 6h);
}

AETHER_TEST("coordinator: failures back off 15 min, 1 h, 4 h, then the interval") {
    CoordinatorHarness h;
    h.Create();
    h.Configure(UpdateChannel::Stable, true);
    h.http.Set(std::string(kLatestUrl), Status(502));
    for (const auto backoff : {std::chrono::minutes{15}, std::chrono::minutes{60}, std::chrono::minutes{240},
                               std::chrono::minutes{24 * 60}, std::chrono::minutes{24 * 60}}) {
        h.RunScheduledCheck();
        CHECK(h.Snap().state == UpdateState::Error);
        CHECK(*h.Snap().nextCheck == h.time.SystemNow() + backoff);
    }
    CHECK_EQ(h.http.CountFor(kLatestUrl), 5u);

    // Success resets the back-off.
    h.http.Set(std::string(kLatestUrl), Status(404));
    h.RunScheduledCheck();
    CHECK(h.Snap().state == UpdateState::UpToDate);
    CHECK(*h.Snap().nextCheck == h.time.SystemNow() + 24h);
    h.http.Set(std::string(kLatestUrl), Status(502));
    h.RunScheduledCheck();
    CHECK(*h.Snap().nextCheck == h.time.SystemNow() + 15min);
}

AETHER_TEST("coordinator: a failed automatic re-check keeps a valid offer visible") {
    CoordinatorHarness h;
    h.Create();
    h.Configure(UpdateChannel::Stable, true);
    h.ServeStable(fixtures::kStable_1_2_0);
    h.RunScheduledCheck();
    REQUIRE(h.Snap().state == UpdateState::UpdateAvailable);

    h.http.Set(std::string(kLatestUrl), TransportError(ErrorCode::Network, "offline"));
    h.RunScheduledCheck();
    CHECK(h.Snap().state == UpdateState::UpdateAvailable);
    CHECK_FALSE(h.Snap().failure.has_value());
    CHECK(*h.Snap().nextCheck == h.time.SystemNow() + 15min);

    // A manual check reports the failure.
    h.CheckNow();
    CHECK(h.Snap().state == UpdateState::Error);
    CHECK_EQ(FailureCode(h.Snap()), ErrorCode::Network);
    REQUIRE(h.coordinator->DownloadAndPrepare().ok());  // the verified offer is still usable
    h.Settle();
    CHECK(h.Snap().state == UpdateState::ReadyToInstall);
}

AETHER_TEST("coordinator: automatic checks pause while an update is staged") {
    CoordinatorHarness h;
    h.Create();
    h.Configure(UpdateChannel::Stable, true);
    h.ServeStable(fixtures::kStable_1_2_0);
    h.RunScheduledCheck();
    REQUIRE(h.coordinator->DownloadAndPrepare().ok());
    h.Settle();
    REQUIRE(h.Snap().state == UpdateState::ReadyToInstall);
    const auto checks = h.http.CountFor(kLatestUrl);
    h.RunScheduledCheck();
    CHECK_EQ(h.http.CountFor(kLatestUrl), checks);
    CHECK(h.Snap().state == UpdateState::ReadyToInstall);
    CHECK(*h.Snap().nextCheck == h.time.SystemNow() + 24h);
}

AETHER_TEST("coordinator: disabled automatic checks never schedule, manual checks still work") {
    CoordinatorHarness h;
    h.Create();
    h.Configure(UpdateChannel::Stable, false);
    CHECK(h.Snap().state == UpdateState::Disabled);
    CHECK_FALSE(h.Snap().automaticChecks);
    CHECK_FALSE(h.Snap().nextCheck.has_value());
    CHECK_FALSE(h.timer->IsArmed());
    h.http.Set(std::string(kLatestUrl), Status(404));
    h.CheckNow();
    CHECK(h.Snap().state == UpdateState::UpToDate);
    CHECK_FALSE(h.timer->IsArmed());

    // Turning them on schedules; turning them off again disarms.
    h.Configure(UpdateChannel::Stable, true);
    CHECK(h.timer->IsArmed());
    h.Configure(UpdateChannel::Stable, false);
    CHECK_FALSE(h.timer->IsArmed());
    CHECK_FALSE(h.Snap().nextCheck.has_value());
}

// --------------------------------------------------------------------------------- lifecycle

AETHER_TEST("coordinator: creation validates its inputs") {
    CoordinatorHarness h;
    auto make = [&](const std::function<void(UpdateCoordinatorOptions&)>& tweak,
                    std::optional<EcdsaP256Verifier> verifier = std::nullopt) {
        auto options = h.Options();
        tweak(options);
        return UpdateCoordinator::Create(h.http, h.time, h.dispatcher,
                                         verifier ? std::move(*verifier) : TestVerifier(), std::move(options));
    };
    CHECK_EQ(make([](auto& o) { o.configDir = L"relative"; }).error().code, ErrorCode::InvalidArgument);
    CHECK_EQ(make([](auto& o) { o.updatesDir.clear(); }).error().code, ErrorCode::InvalidArgument);
    CHECK_EQ(make([](auto& o) { o.currentVersion = "1.0"; }).error().code, ErrorCode::InvalidArgument);
    CHECK_EQ(make([](auto& o) { o.repository = GitHubRepository{"bad owner", "x"}; }).error().code,
             ErrorCode::InvalidArgument);
    const std::array<trust::TrustedKey, 0> none{};
    CHECK_EQ(make([](auto&) {}, EcdsaP256Verifier(none)).error().code, ErrorCode::InvalidArgument);
    CHECK(make([](auto&) {}).ok());
}

AETHER_TEST("coordinator: Stop cancels work and refuses later requests") {
    CoordinatorHarness h;
    h.Create();
    h.Configure(UpdateChannel::Stable, true);
    CHECK(h.timer->IsArmed());
    h.coordinator->Stop();
    CHECK_FALSE(h.timer->IsArmed());
    CHECK_EQ(h.coordinator->CheckNow().error().code, ErrorCode::InvalidState);
    CHECK_EQ(CodeOf(h.coordinator->DownloadAndPrepare()), ErrorCode::InvalidState);
    CHECK_EQ(h.coordinator->SkipVersion(Version("9.9.9")).error().code, ErrorCode::InvalidState);
    CHECK_EQ(h.coordinator->Postpone().error().code, ErrorCode::InvalidState);
    UpdatePolicy policy;
    CHECK_EQ(h.coordinator->StartAutomaticChecks(policy).error().code, ErrorCode::InvalidState);
    h.coordinator->Cancel();
    h.coordinator->NotifyClockChanged();
    h.coordinator->Stop();  // idempotent
    CHECK_FALSE(h.Snap().nextCheck.has_value());
    CHECK(h.coordinator->WaitUntilIdle(1s));
}

AETHER_TEST("coordinator: Stop during a download returns promptly") {
    CoordinatorHarness h;
    h.Create();
    h.Configure();
    h.ServeStable(fixtures::kStable_1_2_0);
    h.CheckNow();
    // The transfer stalls after the first chunk until it is cancelled.
    FakeResponse stalled = Ok200(TestAssetBytes(300000, 1));
    stalled.hangAfter = 16 * 1024;
    h.http.Set(DownloadUrl("v1.2.0", fixtures::kStable_1_2_0.assets[0].fileName), stalled);
    std::atomic<bool> transferring{false};
    h.http.onChunk = [&](std::uint64_t) { transferring = true; };
    REQUIRE(h.coordinator->DownloadAndPrepare().ok());
    const auto waitStart = std::chrono::steady_clock::now();
    while (!transferring && std::chrono::steady_clock::now() - waitStart < 10s) std::this_thread::sleep_for(1ms);
    REQUIRE(transferring.load());

    const auto stopStart = std::chrono::steady_clock::now();
    h.coordinator->Stop();
    CHECK(std::chrono::steady_clock::now() - stopStart < 5s);
    h.dispatcher.RunPending();
    const auto afterStop = h.published.size();
    CHECK(h.Snap().state == UpdateState::Downloading);  // results after Stop are discarded
    CHECK_FALSE(std::filesystem::exists(h.DownloadsDir() / L"1.2.0" / L"AetherShell-1.2.0-win-x64-setup.exe"));
    CHECK_FALSE(std::filesystem::exists(h.DownloadsDir() / L"1.2.0" / L"AetherShell-1.2.0-win-x64-setup.exe.partial"));
    h.dispatcher.RunPending();
    CHECK_EQ(h.published.size(), afterStop);
}

AETHER_TEST("coordinator: state and retryability helpers") {
    CHECK_EQ(ToString(UpdateState::UpdateAvailable), std::string_view("update-available"));
    CHECK_EQ(ToString(UpdateState::ReadyToInstall), std::string_view("ready-to-install"));
    for (const auto code : {ErrorCode::Network, ErrorCode::Timeout, ErrorCode::Unavailable, ErrorCode::RateLimited,
                            ErrorCode::InsufficientSpace, ErrorCode::Io}) {
        CHECK(IsRetryableUpdateError(code));
    }
    for (const auto code : {ErrorCode::Signature, ErrorCode::Integrity, ErrorCode::Validation, ErrorCode::Unsupported,
                            ErrorCode::Parse, ErrorCode::NotFound, ErrorCode::Cancelled}) {
        CHECK_FALSE(IsRetryableUpdateError(code));
    }
}

AETHER_TEST("coordinator: explicit empty settings override clears a persisted skipped version") {
    CoordinatorHarness h;h.Create();h.Configure();
    REQUIRE(h.coordinator->SkipVersion(Version("1.2.0")).ok());h.Settle();
    h.coordinator->Stop();h.Create();
    UpdatePolicy policy;policy.enabled=false;policy.channel=UpdateChannel::Stable;policy.replaceSkippedVersion=true;
    REQUIRE(h.coordinator->StartAutomaticChecks(policy).ok());h.Settle();
    CHECK_FALSE(h.Snap().skippedVersion.has_value());
    auto persisted=LoadUpdateState(h.StateFile(),kTestRepository);REQUIRE(persisted.ok());CHECK_FALSE(persisted->skippedVersion.has_value());
}

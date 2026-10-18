// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
//
// Orchestrates the update flow for the running application:
//
//   check (GitHubReleaseClient) -> download manifest + .sig -> verify signature (EcdsaP256Verifier)
//   -> parse manifest (tag, channel, digests) -> compatibility (architecture, Windows build)
//   -> UpdateAvailable -> [user: DownloadAndPrepare] -> verified download (UpdateDownloader)
//   -> ReadyToInstall {package, manifest, signature}
//
// Installing is NOT done here: the separate updater executable re-verifies everything and applies
// the package (ADR 0005). Nothing is executed by this module.
//
// States: Disabled (automatic checks not running - not started or turned off - and nothing else to
// report), Idle, Checking, UpToDate, UpdateAvailable, Downloading, ReadyToInstall, Error (with
// retryable flag and optional retry time). A failed automatic re-check does not replace a verified
// offer that is still valid (UpdateAvailable / ReadyToInstall stay); it only affects the back-off.
//
// Threading:
//  * Public methods are non-blocking and may be called from any thread (normally the UI thread).
//  * Network, disk and crypto work runs on one internal WorkerPool thread, one operation at a time.
//  * Subscribers receive snapshots through IUiDispatcher::Post, i.e. on the UI thread, in order.
//  * The timer callback only posts work to the worker.
//
// Scheduling (automatic checks, StartAutomaticChecks):
//  * never earlier than kFirstCheckDelay (2 min) after StartAutomaticChecks;
//  * then lastCheckUtc + interval (interval >= 6 h, default 24 h; lastCheckUtc persisted in
//    <configDir>/update-state.json, so restarts do not cause extra checks);
//  * after failures: 15 min, 1 h, 4 h, then the interval (never before a server retry-after time);
//  * timer waits are capped at kMaxTimerWait so time spent asleep is caught up after resume;
//    NotifyClockChanged() re-evaluates at once (call it on resume and on WM_TIMECHANGE);
//  * the next check is never more than one interval away, so a system clock moved backwards cannot
//    postpone checks until it catches up with a "last check" that now lies in the future.
//
// Changing the channel (StartAutomaticChecks) withdraws an offer from the other channel unless it is
// staged or downloading, and cancels a check still running on the old channel (its result is
// discarded).
//
// Skipped versions are not announced by automatic checks; CheckNow() still shows them (an explicit
// request). Postpone() dismisses the current announcement until the next automatic check.
#pragma once

#include "aether/BuildInfo.h"
#include "core/Dispatcher.h"
#include "core/Http.h"
#include "core/Result.h"
#include "core/Signal.h"
#include "core/Time.h"
#include "updates/Authenticode.h"
#include "updates/GitHubReleaseClient.h"
#include "updates/SemanticVersion.h"
#include "updates/SignatureVerifier.h"
#include "updates/UpdateDownloader.h"
#include "updates/UpdateManifest.h"
#include "updates/UpdateTimer.h"
#include "updates/UpdateTypes.h"

#include <array>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

namespace aether::updates {

inline constexpr std::chrono::minutes kFirstCheckDelay{2};
inline constexpr std::chrono::hours kMinimumCheckInterval{6};
inline constexpr std::chrono::hours kDefaultCheckInterval{24};
inline constexpr std::array<std::chrono::minutes, 3> kFailureBackoff{std::chrono::minutes{15}, std::chrono::minutes{60},
                                                                    std::chrono::minutes{240}};
inline constexpr std::chrono::minutes kMaxTimerWait{60};
// A persisted lastCheckUtc further in the future than this (clock moved back) is ignored.
inline constexpr std::chrono::minutes kMaxClockSkew{5};
// Downloading snapshots are published at most this often (plus the final 100 %).
inline constexpr std::chrono::milliseconds kProgressPublishInterval{250};

enum class UpdateState { Disabled, Idle, Checking, UpToDate, UpdateAvailable, Downloading, ReadyToInstall, Error };

[[nodiscard]] std::string_view ToString(UpdateState state) noexcept;

struct UpdatePolicy {
    bool enabled = true;  // automatic checks; CheckNow() works either way
    std::chrono::hours interval = kDefaultCheckInterval;  // clamped to >= kMinimumCheckInterval
    UpdateChannel channel = DefaultChannel();
    std::optional<SemanticVersion> skippedVersion;  // when set, replaces the persisted skipped version
    bool replaceSkippedVersion = false;  // also replaces with empty (settings reset/import)
};

struct DownloadProgress {
    std::uint64_t received = 0;
    std::uint64_t total = 0;
};

// Everything the updater needs; all three files sit in <updatesDir>/downloads/<version>/.
struct PreparedUpdate {
    SemanticVersion version;
    std::filesystem::path packagePath;
    std::filesystem::path manifestPath;
    std::filesystem::path signaturePath;
};

struct UpdateFailure {
    Error error;
    bool retryable = false;  // trying again later may succeed (offline, rate limit, server error, disk full)
    std::optional<std::chrono::system_clock::time_point> retryAfter;  // server-provided earliest retry
};

struct UpdateSnapshot {
    UpdateState state = UpdateState::Disabled;
    SemanticVersion currentVersion;
    UpdateChannel channel = UpdateChannel::Stable;
    bool automaticChecks = false;

    // UpdateAvailable / Downloading / ReadyToInstall (and Error after a failed download):
    std::shared_ptr<const ReleaseInfo> release;      // untrusted GitHub metadata of the offered release
    std::shared_ptr<const UpdateManifest> manifest;  // verified (signed) manifest
    std::string releaseNotes;                        // sanitized plain text (SanitizeReleaseNotes)
    std::optional<std::uint64_t> downloadSize;       // size of the selected installer
    DownloadProgress progress;                       // Downloading
    std::optional<PreparedUpdate> prepared;          // ReadyToInstall

    // UpToDate: newest version the server offered (nullopt when no release is published yet).
    std::optional<SemanticVersion> latestVersion;

    std::optional<UpdateFailure> failure;  // Error

    std::optional<std::chrono::system_clock::time_point> lastCheck;  // last successful check
    std::optional<std::chrono::system_clock::time_point> nextCheck;  // next automatic check
    std::optional<SemanticVersion> skippedVersion;
};

using UpdateListener = std::function<void(const UpdateSnapshot&)>;

struct UpdateCoordinatorOptions {
    std::filesystem::path configDir;   // absolute; holds update-state.json
    std::filesystem::path updatesDir;  // absolute; downloads go to <updatesDir>/downloads/<version>/
    GitHubRepository repository = GitHubRepository::Official();
    std::string currentVersion = std::string(build::kVersion);  // injectable for tests
    Architecture architecture = CurrentArchitecture();
    std::optional<std::uint32_t> osBuild;   // nullopt: platform::GetOsVersion().build
    std::string userAgent;                  // empty: DefaultUserAgent()
    std::shared_ptr<IUpdateTimer> timer;    // empty: a ThreadpoolUpdateTimer
    DiskSpaceQuery diskSpace;               // empty: fs::AvailableDiskSpace
    RetryWait downloadRetryWait;            // empty: WaitCancellable
    AuthenticodePolicy authenticode = AuthenticodePolicy::NotRequired;
};

class UpdateCoordinator {
public:
    // `http`, `time` and `dispatcher` must outlive the coordinator. `verifier` is the manifest trust
    // anchor: production passes EcdsaP256Verifier::ForReleaseManifests(). Fails with InvalidArgument
    // for relative directories, an invalid repository or current version, or a verifier without keys.
    [[nodiscard]] static Result<std::unique_ptr<UpdateCoordinator>> Create(net::IHttpClient& http,
                                                                           const ITimeSource& time,
                                                                           IUiDispatcher& dispatcher,
                                                                           EcdsaP256Verifier verifier,
                                                                           UpdateCoordinatorOptions options);
    ~UpdateCoordinator();  // Calls Stop().

    UpdateCoordinator(const UpdateCoordinator&) = delete;
    UpdateCoordinator& operator=(const UpdateCoordinator&) = delete;

    // Applies the policy and (re)schedules automatic checks. May be called again when settings change.
    Result<void> StartAutomaticChecks(UpdatePolicy policy);
    // Starts a check now. Ok when a check is already running (coalesced); InvalidState while
    // downloading or after Stop().
    Result<void> CheckNow();
    // Downloads and verifies the offered installer. Requires an offered update (UpdateAvailable, or
    // Idle/Error with one still pending); Ok without work when it is already staged. Directories of
    // other versions under <updatesDir>/downloads are removed first (UpdateDownloader::PruneOtherVersions).
    Result<void> DownloadAndPrepare();
    // Never announce `version` again from automatic checks (persisted). InvalidState while downloading.
    Result<void> SkipVersion(SemanticVersion version);
    // Dismisses an UpdateAvailable or Error announcement (state Idle/Disabled, or ReadyToInstall when an
    // update is already staged); the offer stays available to DownloadAndPrepare and the next
    // automatic check announces it again. ReadyToInstall itself is kept.
    Result<void> Postpone();
    // Cancels the running check or download (a cancelled download returns to UpdateAvailable).
    void Cancel();
    // Re-evaluates the schedule now (after resume from sleep or a system clock change).
    void NotifyClockChanged();
    // Cancels work, disarms the timer and joins the worker; afterwards every request fails with
    // InvalidState and nothing more is published. Idempotent. Must not be called from the timer
    // callback or the coordinator's worker thread (it joins that thread).
    void Stop() noexcept;

    [[nodiscard]] UpdateSnapshot Snapshot() const;
    // Listeners run on the UI thread (via the dispatcher) for every published state change. There is
    // no replay: read Snapshot() for the current state.
    [[nodiscard]] Connection Subscribe(UpdateListener listener);

    // Blocks until all work queued so far on the worker has finished. Tests and diagnostics only.
    bool WaitUntilIdle(std::chrono::milliseconds timeout) const;

private:
    struct Impl;
    explicit UpdateCoordinator(std::unique_ptr<Impl> impl) noexcept;
    std::unique_ptr<Impl> impl_;
};

// Retryable error categories: Network, Timeout, Unavailable, RateLimited, InsufficientSpace, Io.
[[nodiscard]] bool IsRetryableUpdateError(ErrorCode code) noexcept;

}  // namespace aether::updates

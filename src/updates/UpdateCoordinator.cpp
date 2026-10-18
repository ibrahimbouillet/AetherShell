// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
#include "updates/UpdateCoordinator.h"

#include "core/FileSystem.h"
#include "core/Strings.h"
#include "core/WorkerPool.h"
#include "diagnostics/Logger.h"
#include "platform/windows/OsVersion.h"
#include "updates/AssetNaming.h"
#include "updates/ReleaseNotes.h"
#include "updates/UpdateStateStore.h"

#include <algorithm>
#include <format>
#include <future>
#include <mutex>

namespace aether::updates {
namespace {

using SystemTime = std::chrono::system_clock::time_point;

// A verified offer: everything needed to download and stage the installer later.
struct PendingUpdate {
    std::shared_ptr<const ReleaseInfo> release;
    std::shared_ptr<const UpdateManifest> manifest;
    ManifestAsset asset;      // selected installer (from the signed manifest)
    std::string downloadUrl;  // its browser_download_url
    std::string manifestBytes;
    std::string signatureText;
    std::string notes;  // sanitized
};

struct CheckOutcome {
    enum class Kind { UpToDate, Available, Failed, Cancelled };
    Kind kind = Kind::Failed;
    std::optional<SemanticVersion> latest;
    std::optional<PendingUpdate> update;
    std::optional<UpdateFailure> failure;
};

CheckOutcome Failed(Error error, std::optional<SystemTime> retryAfter = std::nullopt) {
    CheckOutcome outcome;
    if (error.code == ErrorCode::Cancelled) {
        outcome.kind = CheckOutcome::Kind::Cancelled;
        return outcome;
    }
    outcome.kind = CheckOutcome::Kind::Failed;
    const bool retryable = IsRetryableUpdateError(error.code);
    outcome.failure = UpdateFailure{std::move(error), retryable, retryAfter};
    return outcome;
}

}  // namespace

std::string_view ToString(UpdateState state) noexcept {
    switch (state) {
        case UpdateState::Disabled: return "disabled";
        case UpdateState::Idle: return "idle";
        case UpdateState::Checking: return "checking";
        case UpdateState::UpToDate: return "up-to-date";
        case UpdateState::UpdateAvailable: return "update-available";
        case UpdateState::Downloading: return "downloading";
        case UpdateState::ReadyToInstall: return "ready-to-install";
        case UpdateState::Error: return "error";
    }
    return "unknown";
}

bool IsRetryableUpdateError(ErrorCode code) noexcept {
    switch (code) {
        case ErrorCode::Network:
        case ErrorCode::Timeout:
        case ErrorCode::Unavailable:
        case ErrorCode::RateLimited:
        case ErrorCode::InsufficientSpace:
        case ErrorCode::Io: return true;
        default: return false;
    }
}

struct UpdateCoordinator::Impl {
    enum class Operation { None, Check, Download };

    struct Listeners {
        Signal<const UpdateSnapshot&> signal;
    };

    Impl(net::IHttpClient& httpClient, const ITimeSource& timeSource, IUiDispatcher& uiDispatcher,
         EcdsaP256Verifier manifestVerifier, UpdateCoordinatorOptions coordinatorOptions, SemanticVersion current,
         std::uint32_t build, std::shared_ptr<IUpdateTimer> updateTimer)
        : time(timeSource),
          dispatcher(uiDispatcher),
          verifier(std::move(manifestVerifier)),
          options(std::move(coordinatorOptions)),
          currentVersion(std::move(current)),
          osBuild(build),
          timer(std::move(updateTimer)),
          client(httpClient, timeSource, options.repository, options.userAgent),
          downloader(httpClient, UpdateDownloaderOptions{options.updatesDir, options.userAgent, options.repository,
                                                         options.diskSpace, options.downloadRetryWait,
                                                         options.authenticode}),
          stateFile(options.configDir / kUpdateStateFileName) {
        snapshot.currentVersion = currentVersion;
        snapshot.channel = policy.channel;
    }

    // ---- immutable after construction ----
    const ITimeSource& time;
    IUiDispatcher& dispatcher;
    const EcdsaP256Verifier verifier;
    const UpdateCoordinatorOptions options;
    const SemanticVersion currentVersion;
    const std::uint32_t osBuild;
    const std::shared_ptr<IUpdateTimer> timer;
    GitHubReleaseClient client;
    UpdateDownloader downloader;
    const std::filesystem::path stateFile;
    const std::shared_ptr<Listeners> listeners = std::make_shared<Listeners>();

    // ---- guarded by mutex ----
    mutable std::mutex mutex;
    UpdateSnapshot snapshot;
    UpdatePolicy policy;
    bool started = false;
    bool stopped = false;
    bool stateLoaded = false;
    Operation operation = Operation::None;
    std::shared_ptr<CancellationSource> cancel;
    std::optional<PendingUpdate> pending;
    std::optional<PreparedUpdate> prepared;
    SystemTime startedAt{};
    std::optional<SystemTime> lastCheck;
    std::optional<SystemTime> lastFailure;
    int consecutiveFailures = 0;
    std::optional<SystemTime> retryAfter;
    std::optional<SystemTime> deferredUntil;  // automatic check postponed while ReadyToInstall
    std::optional<SemanticVersion> skipped;
    bool skippedExplicit = false;
    std::chrono::steady_clock::time_point lastProgressPublish{};

    // ---- worker thread only ----
    PersistedUpdateState persisted;

    // Declared last: destroyed first. Stop() has already joined it by then.
    WorkerPool workers{WorkerPool::Options{1, L"Aether updates", ComApartment::MultiThreaded}};

    // ------------------------------------------------------------------ helpers (mutex held)

    void PublishLocked() {
        std::weak_ptr<Listeners> weak = listeners;
        dispatcher.Post([weak, copy = snapshot] {
            if (auto alive = weak.lock()) alive->signal.Emit(copy);
        });
    }

    void SetStateLocked(UpdateState state) {
        if (snapshot.state != state) {
            AETHER_LOG_INFO(kLogComponent, "state {} -> {}", ToString(snapshot.state), ToString(state));
        }
        snapshot.state = state;
        if (state != UpdateState::Error) snapshot.failure.reset();
    }

    void ClearOfferLocked() {
        snapshot.release.reset();
        snapshot.manifest.reset();
        snapshot.releaseNotes.clear();
        snapshot.downloadSize.reset();
        snapshot.progress = {};
        snapshot.prepared.reset();
    }

    void ShowOfferLocked(const PendingUpdate& offer) {
        snapshot.release = offer.release;
        snapshot.manifest = offer.manifest;
        snapshot.releaseNotes = offer.notes;
        snapshot.downloadSize = offer.asset.size;
        snapshot.latestVersion = offer.manifest->version;
    }

    // Idle while automatic checks run, Disabled otherwise (not started yet, or turned off).
    UpdateState IdleStateLocked() const {
        return started && policy.enabled ? UpdateState::Idle : UpdateState::Disabled;
    }

    // State shown when no operation runs and nothing else was decided.
    UpdateState RestingStateLocked() const {
        if (prepared) return UpdateState::ReadyToInstall;
        if (pending) return UpdateState::UpdateAvailable;
        return IdleStateLocked();
    }

    std::shared_ptr<CancellationSource> BeginCheckLocked() {
        operation = Operation::Check;
        cancel = std::make_shared<CancellationSource>();
        SetStateLocked(UpdateState::Checking);
        PublishLocked();
        return cancel;
    }

    // Computes the next automatic check and arms the timer.
    void ScheduleLocked() {
        if (stopped) return;
        if (!started || !policy.enabled) {
            snapshot.nextCheck.reset();
            timer->Disarm();
            return;
        }
        if (!stateLoaded) return;  // the worker schedules once lastCheckUtc is known
        const auto now = time.SystemNow();
        SystemTime due = now;
        if (consecutiveFailures > 0 && lastFailure) {
            const auto index = static_cast<std::size_t>(consecutiveFailures - 1);
            const std::chrono::system_clock::duration backoff =
                index < kFailureBackoff.size() ? std::chrono::duration_cast<std::chrono::system_clock::duration>(
                                                     kFailureBackoff[index])
                                               : std::chrono::duration_cast<std::chrono::system_clock::duration>(
                                                     policy.interval);
            due = *lastFailure + backoff;
            if (retryAfter && *retryAfter > due) due = *retryAfter;
        } else if (lastCheck) {
            due = *lastCheck + policy.interval;
        }
        if (deferredUntil && *deferredUntil > due) due = *deferredUntil;
        due = std::max(due, startedAt + kFirstCheckDelay);
        // Every base above (lastCheck, lastFailure, retryAfter, deferredUntil, startedAt) was taken from
        // the clock at an earlier time, so in normal operation due <= now + interval (back-off steps and
        // the clamped retry hint are all shorter than the 6 h minimum interval). Only a system clock
        // moved backwards in this session pushes it further; without this bound automatic checks would
        // stop until the clock caught up again (days or years after a correction).
        due = std::min(due, now + std::chrono::duration_cast<std::chrono::system_clock::duration>(policy.interval));
        snapshot.nextCheck = due;

        auto delay = std::chrono::duration_cast<std::chrono::milliseconds>(due - now);
        delay = std::clamp(delay, std::chrono::milliseconds{0},
                           std::chrono::duration_cast<std::chrono::milliseconds>(kMaxTimerWait));
        // The callback runs on a timer thread and only queues work; it never takes `mutex`, so
        // Disarm() may safely be called while `mutex` is held.
        timer->Arm(delay, [this] { workers.Post([this] { Guarded([this] { OnTimer(); }); }); });
    }

    // ------------------------------------------------------------------ worker thread

    void EnsureStateLoaded() {
        {
            std::lock_guard lock(mutex);
            if (stateLoaded) return;
        }
        auto loaded = LoadUpdateState(stateFile, options.repository);
        if (!loaded) {
            AETHER_LOG_WARN(kLogComponent, "ignoring unreadable update state: {}", loaded.error().Describe());
            persisted = {};
        } else {
            persisted = std::move(loaded).value();
        }
        std::lock_guard lock(mutex);
        stateLoaded = true;
        const auto now = time.SystemNow();
        if (persisted.lastCheckUtc && *persisted.lastCheckUtc <= now + kMaxClockSkew) {
            lastCheck = persisted.lastCheckUtc;
        } else if (persisted.lastCheckUtc) {
            AETHER_LOG_WARN(kLogComponent, "persisted last check lies in the future; ignored");
        }
        if (!skippedExplicit && !skipped) skipped = persisted.skippedVersion;  // an explicit policy value wins
        snapshot.lastCheck = lastCheck;
        snapshot.skippedVersion = skipped;
    }

    void SavePersisted() {
        {
            std::lock_guard lock(mutex);
            persisted.lastCheckUtc = lastCheck;
            persisted.skippedVersion = skipped;
        }
        if (auto saved = SaveUpdateState(stateFile, persisted); !saved) {
            AETHER_LOG_WARN(kLogComponent, "cannot save the update state: {}", saved.error().Describe());
        }
    }

    void OnTimer() {
        EnsureStateLoaded();
        std::shared_ptr<CancellationSource> source;
        {
            std::lock_guard lock(mutex);
            if (stopped || !started || !policy.enabled) return;
            if (operation != Operation::None) return;  // its completion reschedules
            const auto now = time.SystemNow();
            if (!snapshot.nextCheck || now < *snapshot.nextCheck) {
                ScheduleLocked();  // woke up early (capped wait, clock change): wait for the rest
                return;
            }
            if (snapshot.state == UpdateState::ReadyToInstall) {
                deferredUntil = now + policy.interval;  // an update is staged; nothing to announce
                ScheduleLocked();
                return;
            }
            source = BeginCheckLocked();
        }
        AETHER_LOG_INFO(kLogComponent, "automatic update check");
        RunCheck(false, source->Token());
    }

    Result<PendingUpdate> PrepareOffer(const ReleaseInfo& release, const CancellationToken& token) {
        const auto manifestName = ManifestAssetName(release.version);
        const auto signatureName = SignatureAssetName(release.version);
        const auto* manifestAsset = release.FindAsset(manifestName);
        const auto* signatureAsset = release.FindAsset(signatureName);
        if (manifestAsset == nullptr || signatureAsset == nullptr) {
            return MakeError(ErrorCode::Validation, std::format("release {} has no signed manifest ({} and {})",
                                                                release.version.ToString(), manifestName,
                                                                signatureName));
        }
        if (manifestAsset->size > kMaxManifestBytes || signatureAsset->size > kMaxSignatureFileBytes) {
            return MakeError(ErrorCode::Validation, "the manifest or its signature is too large");
        }
        auto manifestBytes = downloader.FetchSmallAsset(manifestAsset->browserDownloadUrl, kMaxManifestBytes, token);
        if (!manifestBytes) return manifestBytes.error();
        auto signatureText = downloader.FetchSmallAsset(signatureAsset->browserDownloadUrl, kMaxSignatureFileBytes, token);
        if (!signatureText) return signatureText.error();

        // Authenticity first: nothing in the manifest is used before its signature verifies.
        auto keyId = verifier.VerifySignatureFile(*manifestBytes, *signatureText);
        if (!keyId) return keyId.error();
        AETHER_LOG_INFO(kLogComponent, "manifest of {} verified with release key '{}'", release.version.ToString(),
                        text::SanitizeForLog(*keyId));

        auto manifest = UpdateManifest::Parse(*manifestBytes,
                                              ManifestExpectations{release.version, release.prerelease,
                                                                   options.repository});
        if (!manifest) return manifest.error();
        auto selected = manifest->SelectAsset(options.architecture, osBuild);
        if (!selected) return selected.error();
        const auto* installer = release.FindAsset(selected->fileName);
        if (installer == nullptr) {
            return MakeError(ErrorCode::Validation, "the release does not contain the installer named in the manifest");
        }
        if (installer->size != selected->size) {
            return MakeError(ErrorCode::Integrity, "the installer size on GitHub differs from the signed manifest");
        }

        PendingUpdate offer;
        offer.release = std::make_shared<const ReleaseInfo>(release);
        offer.manifest = std::make_shared<const UpdateManifest>(std::move(manifest).value());
        offer.asset = std::move(selected).value();
        offer.downloadUrl = installer->browserDownloadUrl;
        offer.manifestBytes = std::move(manifestBytes).value();
        offer.signatureText = std::move(signatureText).value();
        offer.notes = SanitizeReleaseNotes(release.body);
        return offer;
    }

    CheckOutcome PerformCheck(bool manual, UpdateChannel channel, const CancellationToken& token) {
        std::optional<SemanticVersion> skippedNow;
        {
            std::lock_guard lock(mutex);
            skippedNow = skipped;
        }
        const ReleaseMetadataCache* cache =
            persisted.cache && persisted.cache->channel == channel ? &*persisted.cache : nullptr;
        RetryHint hint;
        auto query = client.FetchLatest(channel, cache, token, &hint);
        if (!query) {
            if (query.error().code == ErrorCode::NotFound) {
                persisted.cache.reset();
                CheckOutcome outcome;
                outcome.kind = CheckOutcome::Kind::UpToDate;
                return outcome;
            }
            return Failed(query.error(), hint.retryAt);
        }
        if (!query->fromCache) persisted.cache = ReleaseMetadataCache{channel, query->etag, query->release};
        const auto& release = query->release;

        CheckOutcome outcome;
        outcome.latest = release.version;
        if (release.version <= currentVersion) {
            outcome.kind = CheckOutcome::Kind::UpToDate;
            return outcome;
        }
        if (!manual && skippedNow && *skippedNow == release.version) {
            AETHER_LOG_INFO(kLogComponent, "version {} is skipped; not announced", release.version.ToString());
            outcome.kind = CheckOutcome::Kind::UpToDate;
            return outcome;
        }
        auto offer = PrepareOffer(release, token);
        if (!offer) return Failed(offer.error());
        outcome.kind = CheckOutcome::Kind::Available;
        outcome.update = std::move(offer).value();
        return outcome;
    }

    void RunCheck(bool manual, CancellationToken token) {
        UpdateChannel channel = UpdateChannel::Stable;
        {
            std::lock_guard lock(mutex);
            channel = policy.channel;
        }
        auto outcome = PerformCheck(manual, channel, token);
        const auto now = time.SystemNow();
        {
            std::lock_guard lock(mutex);
            operation = Operation::None;
            cancel.reset();
            if (stopped) return;
            deferredUntil.reset();
            // SkipVersion() may have been called while the check was running.
            if (outcome.kind == CheckOutcome::Kind::Available && !manual && skipped &&
                *skipped == outcome.update->manifest->version) {
                outcome.kind = CheckOutcome::Kind::UpToDate;
                outcome.update.reset();
            }
            // The channel changed while the check ran (StartAutomaticChecks also cancels it): whatever
            // it found belongs to the channel the user just left, so it is neither offered nor counted.
            if (channel != policy.channel) outcome.kind = CheckOutcome::Kind::Cancelled;
            switch (outcome.kind) {
                case CheckOutcome::Kind::Cancelled:
                    // SkipVersion() or a channel change may have withdrawn the offer meanwhile.
                    if (!pending && !prepared) ClearOfferLocked();
                    SetStateLocked(RestingStateLocked());
                    break;
                case CheckOutcome::Kind::UpToDate:
                    lastCheck = now;
                    consecutiveFailures = 0;
                    lastFailure.reset();
                    retryAfter.reset();
                    pending.reset();
                    prepared.reset();
                    ClearOfferLocked();
                    snapshot.latestVersion = outcome.latest;
                    SetStateLocked(UpdateState::UpToDate);
                    break;
                case CheckOutcome::Kind::Available: {
                    lastCheck = now;
                    consecutiveFailures = 0;
                    lastFailure.reset();
                    retryAfter.reset();
                    pending = std::move(outcome.update);
                    const bool alreadyStaged = prepared && prepared->version.IsIdenticalTo(pending->manifest->version);
                    if (!alreadyStaged) prepared.reset();
                    ClearOfferLocked();
                    ShowOfferLocked(*pending);
                    if (alreadyStaged) {
                        snapshot.prepared = prepared;
                        snapshot.progress = {pending->asset.size, pending->asset.size};
                    }
                    SetStateLocked(alreadyStaged ? UpdateState::ReadyToInstall : UpdateState::UpdateAvailable);
                    AETHER_LOG_INFO(kLogComponent, "update {} available ({} bytes)",
                                    pending->manifest->version.ToString(), pending->asset.size);
                    break;
                }
                case CheckOutcome::Kind::Failed:
                    ++consecutiveFailures;
                    lastFailure = now;
                    retryAfter = outcome.failure->retryAfter;
                    AETHER_LOG_WARN(kLogComponent, "update check failed ({}): {}",
                                    outcome.failure->retryable ? "retryable" : "not retryable",
                                    outcome.failure->error.Describe());
                    if (!manual && (pending || prepared)) {
                        // An automatic re-check failed while a verified offer exists. The offer is still
                        // valid, so keep showing it; the failure only drives the back-off schedule.
                        SetStateLocked(RestingStateLocked());
                    } else {
                        if (!pending && !prepared) ClearOfferLocked();  // withdrawn while checking
                        SetStateLocked(UpdateState::Error);
                        snapshot.failure = outcome.failure;
                    }
                    break;
            }
            snapshot.lastCheck = lastCheck;
            PublishLocked();
            ScheduleLocked();
        }
        if (outcome.kind != CheckOutcome::Kind::Cancelled) SavePersisted();
    }

    Result<PreparedUpdate> WriteCompanionFiles(const std::filesystem::path& package, const PendingUpdate& offer) {
        const auto& version = offer.manifest->version;
        const auto directory = package.parent_path();
        PreparedUpdate result{version, package, directory / text::Widen(ManifestAssetName(version)),
                              directory / text::Widen(SignatureAssetName(version))};
        const auto root = downloader.DownloadsRoot();
        if (!fs::IsPathWithin(root, result.manifestPath) || !fs::IsPathWithin(root, result.signaturePath)) {
            return MakeError(ErrorCode::InvalidArgument, "companion files would leave the downloads directory");
        }
        if (auto written = fs::WriteFileAtomic(result.manifestPath, offer.manifestBytes); !written) return written.error();
        if (auto written = fs::WriteFileAtomic(result.signaturePath, offer.signatureText); !written) {
            return written.error();
        }
        return result;
    }

    void RunDownload(PendingUpdate offer, CancellationToken token) {
        const AssetDownloadRequest request{offer.downloadUrl, offer.asset.fileName, offer.manifest->version,
                                           offer.asset.size, offer.asset.sha256};
        const DownloadProgressCallback progress = [this](std::uint64_t received, std::uint64_t total) {
            std::lock_guard lock(mutex);
            if (operation != Operation::Download || stopped) return;
            snapshot.progress = {received, total};
            const auto now = time.SteadyNow();
            if (received == total || received == 0 || now - lastProgressPublish >= kProgressPublishInterval) {
                lastProgressPublish = now;
                PublishLocked();
            }
        };
        // Downloads of superseded or abandoned versions are never needed again.
        downloader.PruneOtherVersions(offer.manifest->version);
        auto package = downloader.Download(request, progress, token);
        Result<PreparedUpdate> result =
            package ? WriteCompanionFiles(*package, offer) : Result<PreparedUpdate>(package.error());

        std::lock_guard lock(mutex);
        operation = Operation::None;
        cancel.reset();
        if (stopped) return;
        if (result) {
            prepared = *result;
            snapshot.prepared = prepared;
            snapshot.progress = {offer.asset.size, offer.asset.size};
            SetStateLocked(UpdateState::ReadyToInstall);
            AETHER_LOG_INFO(kLogComponent, "update {} is ready to install", prepared->version.ToString());
        } else if (result.error().code == ErrorCode::Cancelled) {
            snapshot.progress = {};
            SetStateLocked(UpdateState::UpdateAvailable);
        } else {
            const auto& error = result.error();
            AETHER_LOG_WARN(kLogComponent, "update download failed: {}", error.Describe());
            snapshot.progress = {};
            SetStateLocked(UpdateState::Error);
            snapshot.failure = UpdateFailure{error, IsRetryableUpdateError(error.code), std::nullopt};
        }
        PublishLocked();
        ScheduleLocked();
    }

    // Runs a worker task. An exception (in practice only allocation failure) must neither escape to the
    // pool nor leave `operation` set, which would refuse every later request: it becomes an Error.
    template <class Function>
    void Guarded(Function&& function) noexcept {
        try {
            function();
        } catch (...) {
            try {
                std::lock_guard lock(mutex);
                operation = Operation::None;
                cancel.reset();
                if (stopped) return;
                AETHER_LOG_ERROR(kLogComponent, "unexpected exception in the update worker");
                SetStateLocked(UpdateState::Error);
                snapshot.failure =
                    UpdateFailure{MakeError(ErrorCode::Internal, "unexpected failure in the update worker"), false,
                                  std::nullopt};
                PublishLocked();
            } catch (...) {
                // Nothing more can be done safely; the state stays as it was.
            }
        }
    }

    // ------------------------------------------------------------------ shutdown

    void Stop() noexcept {
        std::shared_ptr<CancellationSource> running;
        {
            std::lock_guard lock(mutex);
            // From here on ScheduleLocked() never arms the timer again and results are discarded.
            stopped = true;
            running = cancel;
            snapshot.nextCheck.reset();
        }
        if (running) running->Cancel();
        // Disarm waits for a timer callback in flight; that callback only posts to the worker, which
        // refuses work once shut down. The worker is joined after its current task observes the
        // cancellation.
        timer->Disarm();
        workers.Shutdown();
    }
};

UpdateCoordinator::UpdateCoordinator(std::unique_ptr<Impl> impl) noexcept : impl_(std::move(impl)) {}

UpdateCoordinator::~UpdateCoordinator() {
    if (impl_) impl_->Stop();
}

Result<std::unique_ptr<UpdateCoordinator>> UpdateCoordinator::Create(net::IHttpClient& http, const ITimeSource& time,
                                                                     IUiDispatcher& dispatcher,
                                                                     EcdsaP256Verifier verifier,
                                                                     UpdateCoordinatorOptions options) {
    if (options.configDir.empty() || !options.configDir.is_absolute() || options.updatesDir.empty() ||
        !options.updatesDir.is_absolute()) {
        return MakeError(ErrorCode::InvalidArgument, "update directories must be absolute paths");
    }
    if (!options.repository.IsValid()) return MakeError(ErrorCode::InvalidArgument, "invalid update repository");
    auto current = SemanticVersion::Parse(options.currentVersion);
    if (!current) return MakeError(ErrorCode::InvalidArgument, "the current version is not a semantic version");
    if (verifier.TrustedKeyCount() == 0) {
        return MakeError(ErrorCode::InvalidArgument, "the manifest verifier has no usable trusted key");
    }
    if (options.userAgent.empty()) options.userAgent = DefaultUserAgent();
    const auto build = options.osBuild.value_or(platform::GetOsVersion().build);
    try {
        auto timer = options.timer;
        if (!timer) {
            auto created = ThreadpoolUpdateTimer::Create();
            if (!created) return created.error();
            timer = std::shared_ptr<IUpdateTimer>(std::move(created).value());
        }
        auto impl = std::make_unique<Impl>(http, time, dispatcher, std::move(verifier), std::move(options),
                                           std::move(current).value(), build, std::move(timer));
        return std::unique_ptr<UpdateCoordinator>(new UpdateCoordinator(std::move(impl)));
    } catch (const std::exception& e) {
        // Allocation or thread start failures (WorkerPool) must not cross the module boundary.
        return MakeError(ErrorCode::Internal, std::string("cannot create the update coordinator: ") + e.what());
    } catch (...) {
        return MakeError(ErrorCode::Internal, "cannot create the update coordinator");
    }
}

Result<void> UpdateCoordinator::StartAutomaticChecks(UpdatePolicy policy) {
    auto& d = *impl_;
    if (policy.interval < kMinimumCheckInterval) {
        AETHER_LOG_INFO(kLogComponent, "update interval raised to the minimum of {} h", kMinimumCheckInterval.count());
        policy.interval = kMinimumCheckInterval;
    }
    const auto skippedOverride = policy.skippedVersion;
    const bool replaceSkipped = policy.replaceSkippedVersion || skippedOverride.has_value();
    bool persistOverride = false;
    std::shared_ptr<CancellationSource> staleCheck;
    {
        std::lock_guard lock(d.mutex);
        if (d.stopped) return MakeError(ErrorCode::InvalidState, "the update coordinator was stopped");
        if (!d.started) d.startedAt = d.time.SystemNow();
        const bool channelChanged = d.started && policy.channel != d.policy.channel;
        d.started = true;
        d.policy = std::move(policy);
        if (replaceSkipped) {
            persistOverride = !d.stateLoaded || d.skipped != skippedOverride;
            d.skipped = skippedOverride; d.skippedExplicit = true;
        }
        d.snapshot.channel = d.policy.channel;
        d.snapshot.automaticChecks = d.policy.enabled;
        d.snapshot.skippedVersion = d.skipped;
        // An offer from the other channel no longer applies. A staged update (or one being downloaded)
        // was an explicit user decision and is kept. A check still querying the old channel is
        // cancelled, and RunCheck discards its result in any case.
        if (channelChanged && d.operation != Impl::Operation::Download && !d.prepared) {
            d.pending.reset();
            if (d.operation == Impl::Operation::Check) {
                staleCheck = d.cancel;
            } else {
                d.ClearOfferLocked();
                d.SetStateLocked(UpdateState::Idle);
            }
        }
        if (d.operation == Impl::Operation::None &&
            (d.snapshot.state == UpdateState::Disabled || d.snapshot.state == UpdateState::Idle)) {
            d.SetStateLocked(d.IdleStateLocked());
        }
        d.ScheduleLocked();
        d.PublishLocked();
    }
    // Outside the lock, like Cancel(): cancellation callbacks run synchronously.
    if (staleCheck) staleCheck->Cancel();
    const bool posted = d.workers.Post([&d, persistOverride] {
        d.Guarded([&] {
            d.EnsureStateLoaded();
            if (persistOverride) d.SavePersisted();
            std::lock_guard lock(d.mutex);
            d.ScheduleLocked();
            d.PublishLocked();
        });
    });
    if (!posted) return MakeError(ErrorCode::InvalidState, "the update worker is shut down");
    return Ok();
}

Result<void> UpdateCoordinator::CheckNow() {
    auto& d = *impl_;
    std::shared_ptr<CancellationSource> source;
    {
        std::lock_guard lock(d.mutex);
        if (d.stopped) return MakeError(ErrorCode::InvalidState, "the update coordinator was stopped");
        if (d.operation == Impl::Operation::Check) return Ok();
        if (d.operation == Impl::Operation::Download) {
            return MakeError(ErrorCode::InvalidState, "an update download is in progress");
        }
        source = d.BeginCheckLocked();
    }
    const bool posted = d.workers.Post([&d, source] {
        d.Guarded([&] {
            d.EnsureStateLoaded();
            d.RunCheck(true, source->Token());
        });
    });
    if (!posted) {
        std::lock_guard lock(d.mutex);
        d.operation = Impl::Operation::None;
        d.cancel.reset();
        return MakeError(ErrorCode::InvalidState, "the update worker is shut down");
    }
    return Ok();
}

Result<void> UpdateCoordinator::DownloadAndPrepare() {
    auto& d = *impl_;
    std::shared_ptr<CancellationSource> source;
    PendingUpdate offer;
    {
        std::lock_guard lock(d.mutex);
        if (d.stopped) return MakeError(ErrorCode::InvalidState, "the update coordinator was stopped");
        if (d.operation == Impl::Operation::Download) return Ok();
        if (d.prepared) {
            // Already staged (perhaps hidden behind a later check error): nothing to download.
            if (d.snapshot.state != UpdateState::ReadyToInstall && d.operation == Impl::Operation::None) {
                d.SetStateLocked(UpdateState::ReadyToInstall);
                d.PublishLocked();
            }
            return Ok();
        }
        if (d.operation == Impl::Operation::Check) {
            return MakeError(ErrorCode::InvalidState, "an update check is in progress");
        }
        if (!d.pending) return MakeError(ErrorCode::InvalidState, "no update is available to download");
        offer = *d.pending;
        d.operation = Impl::Operation::Download;
        d.cancel = std::make_shared<CancellationSource>();
        source = d.cancel;
        d.ClearOfferLocked();
        d.ShowOfferLocked(offer);
        d.snapshot.progress = {0, offer.asset.size};
        d.lastProgressPublish = d.time.SteadyNow();
        d.SetStateLocked(UpdateState::Downloading);
        d.PublishLocked();
    }
    const bool posted = d.workers.Post([&d, offer = std::move(offer), source]() mutable {
        d.Guarded([&] { d.RunDownload(std::move(offer), source->Token()); });
    });
    if (!posted) {
        std::lock_guard lock(d.mutex);
        d.operation = Impl::Operation::None;
        d.cancel.reset();
        return MakeError(ErrorCode::InvalidState, "the update worker is shut down");
    }
    return Ok();
}

Result<void> UpdateCoordinator::SkipVersion(SemanticVersion version) {
    auto& d = *impl_;
    {
        std::lock_guard lock(d.mutex);
        if (d.stopped) return MakeError(ErrorCode::InvalidState, "the update coordinator was stopped");
        if (d.operation == Impl::Operation::Download) {
            return MakeError(ErrorCode::InvalidState, "an update download is in progress");
        }
        d.skipped = version;
        d.snapshot.skippedVersion = version;
        const bool offered = d.pending && d.pending->manifest->version == version;
        const bool staged = d.prepared && d.prepared->version == version;
        if (offered) d.pending.reset();
        if (staged) d.prepared.reset();
        if ((offered || staged) && d.operation == Impl::Operation::None) {
            d.ClearOfferLocked();
            d.SetStateLocked(d.RestingStateLocked());
        }
        AETHER_LOG_INFO(kLogComponent, "version {} skipped", version.ToString());
        d.PublishLocked();
    }
    if (!d.workers.Post([&d] {
            d.Guarded([&] {
                d.EnsureStateLoaded();
                d.SavePersisted();
            });
        })) {
        return MakeError(ErrorCode::InvalidState, "the update worker is shut down");
    }
    return Ok();
}

Result<void> UpdateCoordinator::Postpone() {
    auto& d = *impl_;
    std::lock_guard lock(d.mutex);
    if (d.stopped) return MakeError(ErrorCode::InvalidState, "the update coordinator was stopped");
    if (d.operation != Impl::Operation::None) return Ok();
    if (d.snapshot.state == UpdateState::UpdateAvailable) {
        d.SetStateLocked(d.IdleStateLocked());
        d.PublishLocked();
    } else if (d.snapshot.state == UpdateState::Error) {
        // Dismissing an error must not hide an update that is already staged.
        d.SetStateLocked(d.prepared ? UpdateState::ReadyToInstall : d.IdleStateLocked());
        d.PublishLocked();
    }
    return Ok();
}

void UpdateCoordinator::Cancel() {
    auto& d = *impl_;
    std::shared_ptr<CancellationSource> running;
    {
        std::lock_guard lock(d.mutex);
        running = d.cancel;
    }
    // Outside the lock: cancellation callbacks (for example the transport closing its request handle)
    // run synchronously and must not wait on a worker that is itself waiting for `mutex`.
    if (running) running->Cancel();
}

void UpdateCoordinator::NotifyClockChanged() {
    auto& d = *impl_;
    d.workers.Post([&d] { d.Guarded([&] { d.OnTimer(); }); });
}

void UpdateCoordinator::Stop() noexcept { impl_->Stop(); }

UpdateSnapshot UpdateCoordinator::Snapshot() const {
    std::lock_guard lock(impl_->mutex);
    return impl_->snapshot;
}

Connection UpdateCoordinator::Subscribe(UpdateListener listener) {
    return impl_->listeners->signal.Connect(std::move(listener));
}

bool UpdateCoordinator::WaitUntilIdle(std::chrono::milliseconds timeout) const {
    auto done = std::make_shared<std::promise<void>>();
    auto future = done->get_future();
    if (!impl_->workers.Post([done] { done->set_value(); })) return true;
    return future.wait_for(timeout) == std::future_status::ready;
}

}  // namespace aether::updates

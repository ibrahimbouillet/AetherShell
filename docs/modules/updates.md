<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Ibrahim Bouillet -->
# Module `updates` (`src/updates`, target `aether_updates`)

Finds official releases on GitHub, authenticates the signed release manifest, downloads and
verifies the installer and stages it for the separate updater executable. **Nothing is installed or
executed here.** The trust model is ADR 0004 (`docs/decisions/0004-update-trust-model.md`); the
install transaction is ADR 0005.

Dependencies: `aether_core`, `aether_diagnostics`, `aether_platform`, `bcrypt`, `crypt32`,
`wintrust`. The HTTP transport is the `aether::net::IHttpClient` interface (`core/Http.h`); the
application injects the WinHTTP client, tests inject fakes. The module does not link `aether_net`.

## Flow

```
GitHubReleaseClient ──► release metadata (untrusted)
        │                 newer than current? skipped? (automatic checks only)
        ▼
manifest + .sig ──► EcdsaP256Verifier (TrustedKeys.h) ──► UpdateManifest::Parse (strict)
        │                                                    │
        │                       SelectAsset(arch, osBuild) ◄─┘   asset size == signed size
        ▼
UpdateAvailable ──[DownloadAndPrepare]──► UpdateDownloader (stream + SHA-256 + size + Authenticode policy)
        ▼
ReadyToInstall {package, manifest, signature} in <updatesDir>\downloads\<version>\
```

## Public API (namespace `aether::updates`)

| Header | Purpose |
|---|---|
| `SemanticVersion.h` | SemVer 2.0.0 `Parse`, `ParseTag` (exactly one optional leading `v`/`V`), `ToString`, precedence (`<=>`, build metadata ignored), `IsIdenticalTo` (build metadata included). 64-bit numeric parts; leading zeros, empty identifiers, bad characters, missing parts and overflow are `ErrorCode::Parse`. |
| `UpdateTypes.h` | `UpdateChannel`, `Architecture`, `GitHubRepository::Official()`, `ReleaseInfo` / `ReleaseAsset`, URL predicates (`IsRepositoryWebUrl`, `IsReleaseDownloadUrl`, `IsHttpsUrlOnHost`). |
| `GitHubReleaseClient.h` | `FetchLatest(channel, cache, cancel, retryHint)`: `/releases/latest` (stable) or `/releases?per_page=30` (prerelease: highest valid non-draft SemVer, prereleases included). ETag / `If-None-Match`, 304 → cached release. `ParseRelease`, `ReleaseToJson`, `SelectNewestRelease`. |
| `AssetNaming.h` | `AetherShell-<v>-win-<arch>-setup.exe`, `-manifest.json`, `-manifest.json.sig`, `-checksums.txt`; `IsSafeAssetFileName`. |
| `UpdateManifest.h` | Schema v1 `UpdateManifest::Parse(bytes, ManifestExpectations)`, `SelectAsset(arch, osBuild)`, `IsPlaceholderDigest`. |
| `Sha256.h` | CNG SHA-256: incremental `Sha256` (`Create`/`Update`/`Finish`), `Hash`, `HashFile`, `ParseSha256Hex`. |
| `SignatureVerifier.h` | `EcdsaP256Verifier(keys)` / `ForReleaseManifests()`, `Verify`, `VerifySignatureFile`; strict `ParseSignatureFile`, `ParseDerEcdsaSignature`, `ParseEcdsaSignature`. Returns the id of the key that verified (rotation). |
| `TrustedKeys.h` | `trust::kReleaseSigningKeys` — the production P-256 public key(s), X‖Y hex. |
| `Encoding.h` | Strict hex, strict base64 (own decoder: padding required, canonical trailing bits, no whitespace), UTC timestamps `YYYY-MM-DDTHH:MM:SSZ`. |
| `Authenticode.h` | `CheckAuthenticode(file)`, `EnforceAuthenticodePolicy(file, policy)`; policy `NotRequired` (default) / `RequiredIfPresent` / `Required`. |
| `UpdateDownloader.h` | `Download(request, progress, cancel)`, `FetchSmallAsset`, `PruneOtherVersions`, `RequiredDiskSpace`, `WaitCancellable`. |
| `ReleaseNotes.h` | `SanitizeReleaseNotes(body, maxBytes)` → safe plain text. |
| `UpdateStateStore.h` | `<configDir>\update-state.json` (schema 1): `lastCheckUtc`, `etag`, `cachedChannel`, `cachedRelease`, `skippedVersion`; atomic writes. |
| `UpdateTimer.h` | `IUpdateTimer` (injectable) and `ThreadpoolUpdateTimer` (Win32 thread-pool timer with a coalescing window). |
| `UpdateCoordinator.h` | The orchestrator used by the application (below). |

Every public operation returns `aether::Result<T>`; no exception leaves the module (the coordinator's
factory catches allocation and thread-creation failures, every worker task runs inside a guard that
turns an unexpected exception into `Error`/`Internal` instead of wedging the state machine, and the
timer callback swallows exceptions).

### `UpdateCoordinator`

```cpp
auto coordinator = UpdateCoordinator::Create(http, time, uiDispatcher,
                                             EcdsaP256Verifier::ForReleaseManifests(),
                                             UpdateCoordinatorOptions{paths.configDir, paths.updatesDir});
coordinator->Subscribe([](const UpdateSnapshot& s) { /* UI thread */ });
coordinator->StartAutomaticChecks(UpdatePolicy{.enabled = true, .interval = 24h, .channel = channel});
coordinator->CheckNow();  coordinator->DownloadAndPrepare();  coordinator->SkipVersion(v);
coordinator->Postpone();  coordinator->Cancel();  coordinator->Snapshot();  coordinator->Stop();
```

States: `Disabled` (automatic checks not running and nothing else to report), `Idle`, `Checking`,
`UpToDate` (with `latestVersion`, or none when nothing is published), `UpdateAvailable` (release,
verified manifest, sanitized notes, download size), `Downloading` (progress), `ReadyToInstall`
(`PreparedUpdate{version, packagePath, manifestPath, signaturePath}`), `Error`
(`UpdateFailure{error, retryable, retryAfter}`).

Rules worth knowing:

- Automatic checks never announce the skipped version; `CheckNow()` (an explicit request) still does.
  `SkipVersion` is persisted; `UpdatePolicy::skippedVersion`, when set, replaces the persisted value.
- `Postpone()` dismisses `UpdateAvailable`/`Error`; the offer stays downloadable and the next
  automatic check announces it again.
- A failed **automatic** re-check while a verified offer exists keeps `UpdateAvailable` /
  `ReadyToInstall` visible (the failure only drives back-off). A failed manual check shows `Error`.
- Changing the channel drops an offer from the other channel (unless one is already staged or being
  downloaded); a check still running on the old channel is cancelled and its result discarded.
- `DownloadAndPrepare()` removes download directories of other versions first.
- `CheckNow()` during a check is coalesced; conflicting requests return `InvalidState`.
- Current version: `aether::build::kVersion` (injectable through `UpdateCoordinatorOptions`).

## Threading model

- Public coordinator methods are non-blocking and callable from any thread (normally the UI thread).
- Network, disk and crypto work runs on one internal `WorkerPool` thread, one operation at a time.
- Snapshots are delivered through `IUiDispatcher::Post`, so listeners run on the UI thread, in order.
  Download progress is published at most every 250 ms (plus 0 % and 100 %).
- The timer callback only posts to the worker. `Stop()` cancels the running operation, disarms the
  timer and joins the worker; it must not be called from the worker or the timer callback.
- `GitHubReleaseClient`, `UpdateDownloader`, `Sha256::HashFile`, `CheckAuthenticode` block: worker
  threads only. `EcdsaP256Verifier::Verify` is thread-safe (no shared CNG handles).

## Scheduling

- First automatic check: 2 minutes after `StartAutomaticChecks`, never earlier.
- Then `lastCheckUtc + interval` (interval ≥ 6 h, default 24 h); `lastCheckUtc` is persisted, so
  restarts do not add checks. A persisted time more than 5 minutes in the future is ignored.
- After failures: 15 min, 1 h, 4 h, then the interval; never before a server `retry-after` /
  `x-ratelimit-reset` time (clamped to 6 h).
- Timer waits are capped at 60 minutes so time spent asleep is caught up; call
  `NotifyClockChanged()` on resume and `WM_TIMECHANGE` for an immediate re-evaluation.
- The next check is never more than one interval away: if the system clock is moved backwards
  during a session, a last check (or back-off) that now lies in the future delays checks by at most
  one interval instead of until the clock catches up.
- While an update is staged (`ReadyToInstall`), automatic checks are deferred by one interval.

## Failure behaviour

| Situation | Result | `retryable` |
|---|---|---|
| No release published (404, or none valid) | `UpToDate`, `latestVersion` empty | — |
| Offline, DNS, timeout | `Error` Network / Timeout | yes |
| HTTP 5xx | `Error` Unavailable | yes |
| 403/429 with `x-ratelimit-remaining: 0` or `retry-after` | `Error` RateLimited + `retryAfter` | yes |
| Other 403 | `Error` AccessDenied | no |
| Malformed metadata or manifest | `Error` Parse / Validation | no |
| Redirect to a host outside the allow-list | `Error` Validation | no |
| Manifest version ≠ tag, channel mismatch, placeholder digest | `Error` Validation | no |
| Missing/invalid/untrusted signature | `Error` Signature (Validation when the `.sig` asset is missing) | no |
| Wrong architecture, Windows build too old | `Error` Unsupported | no |
| GitHub asset size ≠ signed size; digest/size mismatch after download | `Error` Integrity (partial file deleted) | no |
| Not enough disk space (size × 3 + 50 MiB) | `Error` InsufficientSpace (offer kept) | yes |
| Interrupted transfer | up to 3 attempts from byte zero (2 s, 8 s back-off), then `Error` Network | yes |
| Cancelled | back to the resting state (`UpdateAvailable` after a cancelled download) | — |

Logs (`AETHER_LOG_*`, component `updates`) record state transitions, HTTP statuses, which trusted
key verified a manifest and failures. Untrusted text is passed through `SanitizeForLog`; no secret
exists on this path (no token is ever sent).

## Release notes

The GitHub release body is untrusted and unsigned. `SanitizeReleaseNotes` decodes UTF-8 (malformed
sequences → U+FFFD), normalizes CRLF/CR/U+2028/U+2029 to LF, removes C0/C1 controls except LF and
TAB, DEL and invisible formatting characters (bidi embeddings/overrides/isolates, LRM/RLM, BOM,
interlinear annotations), trims trailing blanks, collapses runs of blank lines and caps the result
at 64 KiB on a code-point boundary (ending with "…"). Markup is not interpreted: the UI renders
plain text only. The signed `releaseNotesUrl` from the manifest is the link to prefer.

## Publishing a release

`scripts/release/` (Python 3.11+, standard library only, plus the OpenSSL CLI):
`make_manifest.py` (real SHA-256 and sizes, refuses placeholders, misnamed files and release-notes
URLs the application would reject) →
`sign_manifest.py` (DER from `openssl dgst -sha256 -sign`, converted to base64 P1363, verified again,
key from `--key` or `AETHER_RELEASE_SIGNING_KEY_FILE`, refuses keys not in `TrustedKeys.h`, never
prints key material) → `make_checksums.py` (sha256sum format). See `scripts/release/README.md`.

## Tests

- `tests/unit/updates` (`aether_updates_tests`): SemVer (semver.org corpus, precedence chain),
  encodings, SHA-256 vectors, ECDSA (P1363/DER, tampering, wrong key, rotation, malformed input,
  production key import), manifest validation, release notes, state file, GitHub client (headers,
  ETag/304, 404, rate limits, 5xx, transport errors, redirects, strict parsing, prerelease
  selection, drafts), downloader (retries, interruption, corruption, size limits, disk space,
  redirects, cancellation, reuse, pruning, Authenticode policy) and coordinator (end-to-end staging,
  every failure category, skip/postpone, channels, scheduling and back-off, Stop) with fakes for
  `IHttpClient`, `ITimeSource`, `IUpdateTimer` and `IUiDispatcher`.
- Signed fixtures (`UpdateTestFixtures.h`) are produced by running the real release scripts with a
  throw-away test key: `python tests/unit/updates/generate_test_fixtures.py`. Only public data is
  written; the test key is deleted with its temporary directory.
- Optional: set `AETHER_TEST_SIGNED_PE` to a file with an embedded Authenticode signature (for
  example `python.exe` from python.org) to exercise the `Valid` and tampered paths of WinVerifyTrust.
- Script tests: `python -m unittest discover -s scripts/release -p "test_*.py" -v`.

## Limitations

- Installers are **not Authenticode-signed** yet; the default policy is `NotRequired` and SmartScreen
  may warn on manual downloads. In-app authenticity rests on the manifest signature (ADR 0004).
- No protection against freeze attacks (withheld releases): the manifest carries no expiry.
- The prerelease channel considers only the 30 most recent releases (one API page).
- Unauthenticated GitHub API access is limited to 60 requests per hour per IP; the 24 h schedule and
  ETag revalidation stay far below it, but shared NAT addresses can still hit it (reported as
  `RateLimited`, retried later).
- Downloads restart from byte zero (no HTTP range resume).
- Asset names embed the full version. GitHub documents that it renames uploaded assets whose names
  contain special characters, so a version with build metadata (`+...`) may become unpublishable
  (the manifest is not found under its expected name and the check fails with `Validation`). Not
  verified against a live upload; the release README advises against build metadata.
- An error page streamed with a status other than 200 and larger than the expected installer size is
  reported as `Integrity` rather than by its status (installers are megabytes, error pages are not).
- `WinVerifyTrust` with the generic policy does not pin the publisher; revocation checks may need the
  network. Catalog-signed files report `NotSigned` (only embedded signatures are examined). It cannot
  be cancelled, so with a non-default policy `Stop()` may wait for a revocation lookup in progress.
- Pruning old download directories could, in theory, race with an updater that is still reading an
  older staged version; the updater re-verifies everything and fails safely in that case.

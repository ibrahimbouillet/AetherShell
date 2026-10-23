# Installation, launcher and updater

Requirements: [FR-018–FR-021](../../PROJECT_SPEC.md), the
[install work order](../specs/install.md), [ADR 0004](../decisions/0004-update-trust-model.md)
and [ADR 0005](../decisions/0005-installation-layout-and-rollback.md).
Headers in `src/install/Install.h` define the implemented API. `aether_install` depends on core,
diagnostics, platform, settings and updates; all filesystem, registry and process operations block
and belong on a worker. Targets `AetherShell` and `AetherShellUpdater` are native executables.
The root installer wrapper is documented and verified separately by its owner.

## Implemented behavior

- `InstallLayout::Create` accepts an absolute, non-root directory; versions must be SemVer.
  Paths stay within the root and reparse points are refused, including before recursive deletion.
  A new installation refuses a nonempty directory without its own journal. The cross-process mutex
  normalizes the existing directory's final Windows path and invariant case, including a root that
  does not yet exist.
- Schema-v1 journal parsing rejects unknown keys, wrong field types, invalid versions/tokens and
  illegal transitions. Saves are atomic. Staging, repair backups and versions share the root volume.
  Each transaction has a cryptographically random identifier.
- Setup payloads are read with `LOAD_LIBRARY_AS_DATAFILE_EXCLUSIVE | LOAD_LIBRARY_AS_IMAGE_RESOURCE`;
  reading resources never runs their executable entry point. The bounded manifest accepts at most
  64 files and 512 MiB in total, rejects traversal/ADS/device aliases and case-insensitive duplicates,
  and verifies each extracted size/SHA-256. Complete install packages require the app, launcher,
  updater, license and notices. Extraction checks free disk space first.
- Install and repair stage before requesting app exit. Cancellation is observed before/between
  extraction and immediately before activation; after activation the transaction completes safely.
  Repair keeps the old version directory until replacement succeeds. Commit preserves active and
  previous versions. Recovery handles each journal state, including interrupted first installation
  and cleanup interrupted after commit/rollback. Working application files are never removed before
  the replacement is committed.
- Registration is per-user: an Installed apps entry, requested native shortcuts and consent-only
  startup through `settings::StartupRegistration`. Removal checks that shortcut/registry targets
  belong to this installation. Uninstall stops the app gracefully, removes owned installation files,
  and preserves data unless removal was explicitly selected; overlapping data/install roots fail
  before deletion. Registry roots and process operations are injectable for isolated tests.
- Health checks use 32 random hex characters and an existing manual-reset event named
  `Local\AetherShell.Health.<token>`. The app calls `SignalHealthy`. Token collisions fail closed.
  Timeouts are bounded. Only an unhealthy process launched by this updater can be terminated, after
  its graceful exit request has failed; no healthy application is forcibly stopped.

## Launch and update trust

The stable launcher reads/reconciles the journal, then starts the selected version with a correctly
quoted argument vector and the version directory as working directory. Normal launch/Run/setup
sessions retain the exact child handle and observe the app's surface-readiness signal. At most
three abnormal Windows exception exits are retried, after 1, 2 and 4 seconds; the budget never resets
during that launcher session. Ordinary exit codes, Quit, recovery commands and the usual Task
Manager exit code 1 do not restart. A live hung app is never killed or repeatedly relaunched.

Each retry checks the journal SHA-256 under the nonblocking install lock; changed/missing state,
maintenance lock contention or launch failure stops recovery. Quit, recovery and maintenance cancel
backoff through installation-scoped session kernel objects and wait at most three seconds for the
supervisor lease. If an initial child is not yet addressable through IPC, its supervisor keeps the
lease until that child is addressable or exits; maintenance fails safely on timeout instead of
leaving an unaddressable child able to start afterward. Stable-image sharing failures get at most
one second of bounded retry after normal shutdown. Corrupt/missing state on a manual launch shows
a native Repair message; maintenance-busy and autostart errors exit without a modal dialog.
Launcher/updater executables embed the project icon and VERSIONINFO.

The updater independently verifies the exact manifest bytes with the compiled-in release verifier,
then validates official repository metadata, architecture/minimum OS, package size/SHA-256 and embedded
payload version/architecture. A read handle denies package replacement through verification/staging.
No downloaded package is executed for extraction. The public updater has no unsigned/test bypass.

After activation the updater starts `AetherShellApp.exe` directly so it owns the actual health-checked
process rather than the supervising launcher. It forwards both health token and the selected data
directory; rollback forwards the same data directory. A healthy version is committed before refreshing
maintenance files. A maintenance refresh error preserves the healthy application and records
`MaintenanceRefreshFailed`. Failed health rolls back and restarts the previous version.
The result journal is `<dataRoot>/updates/last-update-result.json`; logs use generic error categories.

```text
AetherShellUpdater.exe --apply --package <setup.exe> --manifest <manifest.json>
  --signature <manifest.json.sig> --install-root <directory>
  [--data-dir <directory>] [--wait-pid <pid>]
```

The native progress window uses standard controls, indeterminate progress, system reduced-motion
preference and Per-Monitor-V2 DPI scaling. Disk/verification/log initialization runs on its worker.
The installed maintenance setup copies itself to an isolated temporary directory and inherits only
a parent-process wait handle before the original exits. The worker verifies its copied bytes and
waits for that exit before removing/repairing the installation. As allowed by ADR 0005, its temporary
executable remains in `%TEMP%`. A successful relocation returns handoff exit code zero; its child/log
records the final operation result. Setup startup consent also persists the setting through the
settings store and restores the previous Run value if persistence fails.

The wizard discloses default-on sign-in startup for fresh interactive installs with opt-out (D34).
Settings/portable defaults remain off. Existing saved disables, Task Manager StartupApproved data
and unrelated Run targets are preserved; silent setup only changes startup with an explicit flag.
Windows schedules per-user Run entries and may delay them; registration cannot guarantee startup
ordering or hide Explorer's taskbar before Aether's guarded replacement becomes active. No service,
Winlogon change, elevation, priority boost or startup-delay override is installed.

The setup wizard clamps to the monitor work area and uses horizontal/vertical native scrollbars,
wheel scrolling and focus visibility for smaller viewports. DPI rebuilds preserve folder and checkbox
choices; focus notifications cannot advance pages or trigger installation. The test-mode policy is
reapplied at operation/finish time so UI controls cannot enable real startup, shortcuts or app launch.

## Verification and limits

D34 lifecycle changes compile in Release `/W4 /WX`, including the stable launcher and setup source.
The focused `build/wf-alpha2-lifecycle` suites pass 16 install and 54 settings cases. Their new
coverage uses pure policies, injected registry and installation-scoped own kernel objects: bounded
exception retries, preserved startup disables, owned opt-out, changed journal rejection, stop during
backoff and nonblocking control/launch lock ordering. No test starts, stops or crashes a real app,
changes the owner's startup registration or qualifies Windows sign-in scheduling. Actual installed
crash recovery, maintenance during a real crash and physical sign-in remain separate device gates.

The initial Debug `/W4 /WX` library, launcher and updater build passed 2/2 CTest executables in
`build/wf-install2`. A later case-alias regression exposed rejection of an otherwise valid trailing
separator after lexical normalization; `InstallLayout::Create` now normalizes that input safely.
The settled rebuild then passed **10 unit + 7 integration cases**, 2/2 CTest, 10.73 s. A fresh
final Debug `/W4 /WX` build in `build/wf-install2-final` passed the same **10 + 7** cases,
2/2 CTest, 8.95 s, and built both launcher/updater executables without warnings. The case-alias
fixture also proves recovery and launch refuse an installation lock held on another thread.

Tests use temporary roots and resource-only DLL fixtures whose payloads are synthetic text. Update
health success/timeout use an internal transaction seam after verification, with fake process
operations: no test launches/kills a real app, changes real Run/Uninstall values or touches real
shortcuts. A separate invalid-signature case exercises the public fail-closed path before shutdown.
Registration testing uses only random `HKCU\Software\AetherShellTests` subkeys with RAII cleanup.

```powershell
./scripts/build.ps1 -BuildDir build/wf-install2-final -Modules 'install;launcher;updater' `
  -Target @('aether_install_tests','aether_install_integration_tests','AetherShell','AetherShellUpdater') `
  -Jobs 2 -CMakeArgs '-DAETHER_WARNINGS_AS_ERRORS=ON' -Test -TestRegex '^aether_install'
```

No official signed release package has been supplied, so real production signature success and a
real app's 90-second health/rollback flow remain unverified. Tests do not prove power-loss durability,
real Installed apps/shortcut behavior, cross-monitor visual appearance, Windows 10 or ARM64 runtime,
or whole-product privacy/security. Native installer packaging and installed-copy maintenance are
separate integration gates owned by the root app task.
`SetupViewportTests.cpp` adds a native owned-window DPI/focus/scroll qualification without installing;
its execution is pending the root's installer build and is not included in the 17 library cases.

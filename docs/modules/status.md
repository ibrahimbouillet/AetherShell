# Status providers

Requirements: [FR-005–FR-008 and NFR-005](../../PROJECT_SPEC.md); task T5c in
[TASKS.md](../../TASKS.md). Library `aether_status`, direct sources in `src/status/` only;
`src/status/audio/` is the independent `status_audio` module. Shared `StatusTypes.h` was not changed.

## Implemented entry points

`CreateStatusProviders(StatusProviderOptions)` returns unstarted clock, battery, keyboard and
network providers. `Start(listener)` launches background work, `Current()` copies the last snapshot,
and `Stop()` closes delivery and joins. Consumers post snapshots through `IUiDispatcher`. The app
must forward time/locale/resume/power notifications as described in `StatusProviders.h`; that app
wiring is owned by the app module. Provider options are configuration/test seams, not persisted settings.

| Area | Implementation and behavior |
|---|---|
| Clock | `ClockProvider`, `ClockTime`, `ClockFormatter`: precise UTC reading, local fields/time-zone description; absolute waitable timer at second/minute boundaries; change wake and backward-time watchdog; Windows NLS localized system/12/24-hour/date formatting. |
| Battery | `BatteryProvider`, `BatteryMapping`: PowerManager change events on MTA thread plus GetSystemPowerStatus cross-check/fallback; missing percent/time remain optional; no battery is Unavailable; raw mapping tested independently. Fallback intervals must be positive and at most 4,294,967 whole seconds, rejected before any worker starts otherwise. |
| Keyboard | `KeyboardProvider`, `KeyboardLayouts`: foreground thread HKL, localized locale/layout labels; GetMessage loop with foreground WinEvent hook and default 1 s coalescable comparison. Expensive layout descriptions cached; Refresh clears them. |
| Network | `NetworkProvider`, `NetworkMapping`: NetworkInformation events and background profile query; quiet-period debounce with maximum burst deadline. Start requires 0 < debounce <= maxDebounce <= 60 s. No SSID/ProfileName getter; profileName remains empty. |
| Lifetime/helpers | `ProviderHost`, `ProviderThread`, `SnapshotPublisher`, `WinRtSupport`: wake/stop events, dedicated threads, deduplication and serialized listener calls; body/listener exceptions contained. |

## Supported behavior and lifecycle

- Unknown/absent battery and unknown network fields are distinct; values are never synthesized.
- Input layout refers to the foreground host thread. Console hosts may differ from console input;
  elevated foreground events can be delayed until comparison; IME conversion modes are not exposed.
  The per-app Windows input setting affects when another thread adopts a language. No foreground
  window falls back to the provider thread and marks `fromForegroundWindow=false`.
- Each Start owns a shared session. Lifecycle locks only select sessions; no lock is held while
  closing a listener or joining. Concurrent external Stops wait for the same retired worker. Start
  returns InvalidState promptly while stopping; it never waits for old background work on the UI.
  Once Stop has returned, a later successful Start establishes a new listener session.
- Stop and public-provider destruction are safe inside the listener. The worker owns the Impl and
  session until its body has unwound; late events own only their wake signal. Worker-side Stop cannot
  join itself, so final thread/handle cleanup happens on exit. Current keeps the last snapshot after
  session resources are released. Restart inside a running listener remains refused.
- The shared WinRT runtime now owns its MTA cookie explicitly. Every provider balances its own COM
  scope; the runtime spans provider restarts. FinalizeStatusRuntime() refuses active scopes, clears
  factories and calls CoDecrementMTAUsage exactly once, with static RAII cleanup as fallback. This is
  terminal application shutdown; never call it for an ordinary provider Stop/restart. Later WinRT
  scopes fail explicitly rather than resurrecting torn-down platform factories.
- A trial using only last-scope cache clearing and CoUninitialize reproduced an access violation
  when later providers activated factories. The final design retains the shared runtime until final
  shutdown; the isolated shutdown executable proves balanced cleanup without contaminating restart
  tests. The root app must call the finalizer after all status providers stop.
- A keyboard timer/hook failure falls back where possible and logs limitations; not all platform
  failure branches are fault-injected. Real network/power transitions, sleep/resume, Windows 10 and
  UI rendering/wiring remain unverified. Live callbacks may expose system error text in diagnostic
  details; a whole-product privacy/security audit has not been performed.

## This session's changes

The interrupted implementation was preserved. Added rejection of battery timeouts that would spin
or overflow a DWORD wait, and invalid network debounce/deadline options. Two unit cases exercise
zero, negative, out-of-range and maximum-duration inputs without starting real providers.

Independent review corrected unit/integration callback-state lifetimes during failed REQUIRE
unwinding, joined the test producer safely, distinguished battery flag 255 (unknown) from no battery,
and removed cross-sample assumptions about a foreground window that the user can change while a
test runs. Integration is read-only: no settings/registry/work-area/audio changes or app launches.

GAP-003 implementation adds controlled concurrent/reentrant shutdown, prompt overlapping Start
rejection, callback-side Host/Keyboard destruction with weak lifetime witnesses, all four real
provider owners destroyed from their listener, sequential/overlapping WinRT restarts, and isolated
terminal runtime cleanup. Independent source review found one arbitrary listener-exception log;
it now uses a generic message and a synthetic-marker regression verifies suppression.

## Executed verification

2026-10-18, branch main based on `a9e22cb` with uncommitted module/test/documentation changes:
the first private Debug /W4 /WX build passed 43 unit + 8 integration cases; after focused changes,
the incremental /WX rebuild passed **45 unit + 8 integration cases**, 2/2 CTest executables.
The fresh final /WX build also passed **45 + 8**, 2/2 CTest (13.43 s). Command and final outcome are in
[TEST_STATUS](../knowledge/TEST_STATUS.md) and the canonical handoff:

```powershell
./scripts/build.ps1 -BuildDir build/wf-codex-status-20261018-final -Modules status `
  -Target @('aether_status_tests','aether_status_integration_tests') -Jobs 2 `
  -CMakeArgs '-DAETHER_WARNINGS_AS_ERRORS=ON' -Test -TestRegex '^aether_status'
```

The subsequent lifecycle build in `build/wf-status-lifecycle2` passed **48 unit + 10 read-only
integration + 1 isolated runtime shutdown case**, 3/3 CTest executables, /W4 /WX, 14.72 s total CTest.
This directory began fresh; the final source was incrementally rebuilt after the failed scoped-MTA
experiment and one missing direct header include were corrected.

The independent review correction was then verified in fresh `build/wf-status-lifecycle2-final`:
**49 unit + 10 read-only integration + 1 isolated runtime shutdown case**, 3/3 CTest executables,
/W4 /WX, 18.55 s total CTest. The command below uses that final directory.

```powershell
./scripts/build.ps1 -BuildDir build/wf-status-lifecycle2-final -Modules status `
  -Target @('aether_status_tests','aether_status_integration_tests','aether_status_runtime_shutdown_tests') `
  -Jobs 2 -CMakeArgs '-DAETHER_WARNINGS_AS_ERRORS=ON' -Test -TestRegex '^aether_status'
```

Tests: `tests/unit/status/` (pure mapping/formatting, publisher/host, fake keyboard, invalid options)
and `tests/integration/status/StatusProviderIntegrationTests.cpp` (real read-only providers, first
snapshots, clock boundaries/refresh, fallback agreement, lifecycle/factory). Test evidence applies
to those cases; the canonical decision record owns GAP-003 closure.

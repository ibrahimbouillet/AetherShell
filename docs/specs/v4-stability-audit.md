<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Ibrahim Bouillet -->
# V4 preparation — Alpha 3 stability and input audit

Date: 2026-10-28. Reviewed source: `ce59649` (`ce59649` changes documentation only; release tag
`v0.3.0-alpha.3` remains on `4835641`). D51; T50–T52; FR-036/FR-039/FR-040/FR-041;
NFR-002–NFR-008/NFR-011. Codex and three independent read-only reviewers inspected the canonical
shared checkout. No product implementation or installed/runtime/settings change is part of this
audit. The compact [Claude prompt](../CLAUDE_V4_STABILITY_TOUCH_WELCOME_PROMPT.md) is the work order.

## Implementation status (Claude, 2026-11-01; evidence TEST_STATUS S-061)

States follow the work order: reported, reproduced, source-established, hypothesis, fixed, unverified.
"Fixed" means corrected in source with the named regression passing here. It never means accepted by
the owner, proven on real applications or physical touch, or that a reported incident is explained.
Nothing below is committed, installed or released; the installed Alpha 3 still runs the old behavior.

| ID | State now | What was done, and what is still open |
| --- | --- | --- |
| AS4-01 | **Reported; not reproduced; unverified** | No fixture ever went blank, so the cause is still unknown. Contributors in the suspected chain were corrected (AS4-03/06/07/08). Real browser/Electron qualification with the owner remains the deciding check. |
| AS4-02 | **Reported; not reproduced; unverified** | No reboot was performed. AS4-16/17/19/20/22 removed related defects; none is shown to be the cause. The requested startup stage diagnostics were not added. Read-only on 2026-11-01: this machine's installed settings have sign-in startup off with no Run entry, though S-059 recorded it on and a backup from 2026-10-29 still had it on; how it changed is unknown. That alone would produce the symptom here, but does not establish the cause of the earlier reports. |
| AS4-03 | **Source-established; fixed** | Responsiveness is separate from eligibility: a busy target keeps its clip, mutations wait with a growing delay, and region writes run on their own thread with a bounded wait. Fixtures: with one target hung or stalled, a healthy one was served within 95 ms over 40 runs. The old code's blocking was not separately executed: on this desktop it never clips at all (see AS4-06). |
| AS4-04 | **Source-established; fixed** | Wanted and effective state are separate, with bounded retry, back-off and `Health()`. Injected start failure passes. Helper death after readiness is implemented but was not injected. |
| AS4-05 | **Source-established; fixed in part** | D56: a new guard recognises the claim of a guard process that is gone, takes over its exact clip and removes it on release; any other shape stays the application's. Unit tested with a fake backend; a real helper was not killed. A material the dead guard changed is not restored by this; the application re-asserts its own. |
| AS4-06 | **Reversed by the owner (D56)** | Not a defect. The behavior this entry called wrong, leaving a maximized window that fills the monitor square, is what the owner wants: with both bars hidden it takes the whole screen. The S-061 correction was reverted. |
| AS4-07 | **Source risk; partly fixed** | Per-target dirty tracking, top-level and maximized filtering, a validated process-identity cache and sanitized counters. Leased windows are also rechecked every 2 s, because applications can re-assert a material with no window event (D46). Not measured with many real windows. |
| AS4-08 | **Source-established; fixed** | Work-area broadcasts no longer re-apply the configuration, appearance bursts coalesce, and unchanged hotkeys, update policy and own-window regions cause no writes. The classifier has unit tests; the host path has no separate native assertion. |
| AS4-09 | **Gap; fixed in source; physical touch unverified** | Shared gesture and physics model plus pointer plumbing on Settings/forms, menus, Search, Quick Settings, Dock, previews and Top Bar. Exercised with synthetic contacts through the real routing only. |
| AS4-10 | **Gap; fixed in source; physical touch unverified** | A swipe in from the screen edge is observed from raw digitizer reports, consuming nothing. The recognizer has unit tests; the HID parsing path has **never run against a device**. |
| AS4-11, AS4-12 | **Source-established; fixed** | Press admission with native regressions for mismatch, no press, capture loss and a valid click. |
| AS4-13, AS4-14, AS4-15 | **Source-established; fixed** | Signed fractional carry, with regressions for equal totals across packet sizes, signs and reversal. |
| AS4-16 | **Source-established; fixed** | The installed root is recognized, private roots stay refused. Unit tests cover the comparison; the Settings path has no native test. |
| AS4-17 | **Source-established; fixed** | The handoff wait is bounded (30 s) and never terminates the child, with a unit regression. The launcher itself was not run end to end. |
| AS4-18, AS4-21 | **Source-established; fixed** | D56: Settings reads and states what Windows will really do, and Apply repairs a lost registration while a Task Manager turn-off stays Windows'. A failed save puts the registration back. Regressions with injected states and an injected save failure; the real registry path has not run in a session allowed to change it. |
| AS4-19 | **Source-established; fixed** | The whole Run command is validated before any write, with a regression at the limit. |
| AS4-20 | **Source-established; fixed by refusal** | Setup leaves sign-in startup off for a custom data folder and says so. No dedicated test of that branch. |
| AS4-22 | **Source-established; fixed** | Bounded registration retry and an explicit Unavailable state, with regressions for late success, permanent failure and shutdown during retry. |
| AS4-23 | **Source-established; open** | Dormant update path; not changed. |
| AS4-24 | **Source-established; fixed** | Native regression for reattach at the same and a changed size. |
| AS4-25 | **Source-established; fixed in part** | One shared bounded recovery with unit-tested policy. The device-loss path itself was not injected. |
| AS4-26 | **Observed; unresolved** | Not worked on. The Search benchmark passed in this session's combined runs, which is not a fix. |

Observed during this work, unexplained: Windows can give a standard frame a shape of its own (one
rectangle while maximized, a few matching the window once restored) after a clip is written or removed.
The guard treats that as not its own and leaves it. Its effect on real applications is unqualified.

Not investigated at all from the list below: malformed IPC, named-object identity, callback exception
containment, reparse/TOCTOU races, worker shutdown and crash-handler allocation. No measurements of
first-usable startup, unlock, touch latency, idle CPU/memory or resource growth were taken.

## Evidence language and scope

**Reported** means the owner's real-use observation, not independently reproduced here.
**Source** means the stated control-flow/contract defect is established in the inspected code;
it does not identify the cause of the owner's incident. **Risk** needs reproduction or measurement.
**Gap** is missing requested functionality. **Observed** identifies an executed baseline check,
not reproduction of the owner's desktop incident. Priorities describe implementation order: P0 blocks a
new release candidate; P1 is high-impact reliability/input work; P2 is correctness/qualification.
This is a broad targeted audit, not proof that every defect or exploitable weakness has been found.
Line numbers below belong to the reviewed revision and will shift during implementation.

## Reported blockers and the first reproduction

| ID | Priority / status | Scenario and required evidence |
| --- | --- | --- |
| AS4-01 | P0 / Reported | Slow-loading maximized browser/Electron pages sometimes stop repainting; after enabling Auto-Hide for both bars, multiple windows expanded and became uniformly blank/grey. The owner rebooted. Reproduce with continuous changing owned content and delayed target message handling; distinguish maximized from borderless fullscreen and responsiveness from network progress. Then qualify actual rendering families with owner coordination. A reboot workaround does not diagnose a system/GPU deadlock. |
| AS4-02 | P1 / Reported | Installed Aether does not reliably appear after reboot; native Windows UI remains. Record whether Windows invoked the stable launcher, selected profile/version, bootstrap stage, first usable frame, supervisor/IPC health and replacement outcome. No reboot or installed-registration verification occurred in this audit. |

Hypothesis for AS4-01: toggling both choices releases Dock, Top Bar and both side reservations
separately (`src/app/ShellApplication.cpp:837–845`), prompting external maximized-window relayout
while the corner helper observes and changes regions/materials. AS4-03/06/07/08 make this chain
worth testing. It is **not** established as the blank-content root cause. Do not fix browser caches,
disable hardware acceleration globally, restart Explorer or resize owner-placed normal windows as
an unproven workaround.

## Foreign windows, recovery and event load

| ID | Priority / status | Evidence, consequence and correction |
| --- | --- | --- |
| AS4-03 | P1 / Source | `src/app/WindowCorners.cpp:264–269` returns `eligible=false` after `IsHungAppWindow` or a 50 ms `WM_NULL` probe fails. `Refresh:601` then calls `Restore:654–660`, which can call synchronous `SetWindowRgn:295` on that same unresponsive target. One target can block every other lease/restoration. Model transient unresponsiveness separately; defer unsafe mutation and isolate slow targets. The preflight is not a timeout around the later mutation. Test a leased target becoming unresponsive alongside a healthy changing target; require bounded healthy progress and eventual conditional recovery. |
| AS4-04 | P1 / Source | `WindowCorners::Request:776–789` sets `wanted=true` before starting the helper; failure only logs and leaves it true. Identical later requests return early; no child-exit supervision repairs a later death. Add desired/starting/ready/failed/restoring state, bounded health observation/retry and visible fallback. Inject startup failure, worker rejection and helper death; saved preference must not masquerade as effective readiness. |
| AS4-05 | P1 / Source; acknowledged architectural gap | Original regions/materials exist only in the helper's `WindowCornerLease` memory (`WindowCorners.h:108–116`; helper lifecycle `WindowCorners.cpp:675–741`). Its own abrupt death cannot run that restoration; stale lease properties also cause a new helper's `Claim:316–319` to refuse ownership. Parent death is handled; helper death is different. Design independently owned crash recovery with identity/session/generation checks, or restrict effects to a fail-open technique. Never remove arbitrary properties or overwrite an application's newer region. Kill only a disposable fixture helper and prove recovery/no stale claims. |
| AS4-06 | P1 / Source | `WindowCorners.cpp:251–277` requires framed/resizable maximize but later treats any bounds covering `rcMonitor` as fullscreen. With both bars auto-hidden, `rcWork==rcMonitor` is legitimate ordinary maximize; its corners/rim are released. `src/platform/windows/MonitorUtilities.cpp:61–70` already distinguishes framed maximize from borderless fullscreen. Unify classifiers and test both cases with zero reservations, negative monitor coordinates and DPI changes. This explains missing effects, not blank client rendering. |
| AS4-07 | P2 / Source risk requiring performance evidence | Global create/show/location hooks (`WindowCorners.cpp:469–477,707`) coalesce for 40 ms but still sweep all leases/windows (`354–418`). Each observation can query process identity/token and wait 50 ms for `WM_NULL`; 128 slow observations could consume 6.4 s before writes. Add relevance filtering, per-target dirty tracking, cached validated identity, bounded batches and sanitized latency counters. Measure events/observations/writes, healthy-target latency and idle work with many owned windows. |
| AS4-08 | P2 / Source | Every `WM_SETTINGCHANGE`, including work-area notifications, reaches a full `ApplySettings` (`SystemEvents.cpp:59–65`; `ShellApplication.cpp:1040`). That path relayouts bars/margins and re-registers hotkeys (`837–845,872–877`); bars also independently subscribe to settings changes (`TopBarWindow.cpp:323–334`; `DockWindow.cpp:810–812`). Scope/diff/coalesce changes and suppress no-op feedback. Test that unchanged work-area/theme/settings notifications cause no redundant reservations, region changes or hotkey churn. Multiple intermediate geometry changes are a contributor to investigate, not a reproduced repaint loop. |

Retain AS4-05's existing unsupported boundary until a recovery design is actually validated. ADR
0012 also documents that regions remove DWM-drawn foreign caption buttons and can conflict with
standard captions. Include standard-frame applications in qualification; their native controls are
not expendable. A safe per-family fallback is preferable when the effect cannot preserve behavior.
Do not silently change D46's chosen technique or expand eligibility without recording the evidence.

## Touch, pointer admission and precision input

| ID | Priority / status | Evidence, consequence and correction |
| --- | --- | --- |
| AS4-09 | P1 / Gap | No custom `WM_POINTER`/`WM_TOUCH`/`WM_GESTURE` or Direct Manipulation pipeline was found. `SurfaceWindow.cpp:323–358` handles mouse messages; `FormScrollView.cpp:827–831` handles wheel/vertical scroll. Some native controls can retain Windows legacy touch; this does not provide complete direct pan/fling/elastic behavior. Implement shared pointer identity, timestamped motion, capture/cancel, gesture arbitration and promoted-mouse deduplication. |
| AS4-10 | P1 / Gap | Auto-Hide reads `GetCursorPos` and a one-pixel primary-screen edge (`ShellApplication.cpp:263–293`); no explicit touch reveal exists. Define discoverable touch access and keyboard fallback without swallowing Windows edge gestures or clicks belonging to other windows. Test hidden bars on a physical touchscreen, including a neighboring monitor beyond the edge. |
| AS4-11 | P1 / Source | `TopBarWindow.cpp:607–612` activates the release hit without remembering a matching press. Press A/blank space, move to B and release can invoke B, including a system action. Require same target/pointer gesture and clear it on drag, capture loss, hide/deactivate/cancel. Fake-action tests must prove zero actions for each canceled/mismatched case and one for a valid tap. |
| AS4-12 | P2 / Source | `SearchWindow.cpp:527–542,588–591` tracks text selection but launches whichever result receives release, without matching result press. The same mismatch can launch another application. Add explicit tap admission and pan cancellation; test through a fake launcher. |
| AS4-13 | P2 / Source | `FormScrollView.cpp:645–664` rounds accumulated wheel motion to integer pixels and clears all remainder once any pixel is produced. At 96 DPI/three lines, 120 raw-delta-1 packets analytically produce 40 px versus 48 px for one raw-delta-120 packet. Preserve fractional pixel carry. Compare equal totals across packet sizes, both signs, DPI, reversals and bounds. These numbers are source arithmetic, not measured device results. |
| AS4-14 | P2 / Source | `ControlCenterWindow.cpp:1621–1638` discards every paging packet below 60 and changes sliders by +/-2 for each event regardless of magnitude; a zero slider packet takes the negative branch. Preserve signed remainder, ignore zero and define proportional value changes. Test bursts with equal total delta, reverse direction and clamped endpoints. The existing single-small-packet test does not establish accumulation. |
| AS4-15 | P2 / Source | `SearchWindow.cpp:583–586` moves exactly one selection per nonzero wheel event, regardless of magnitude. Equivalent motion split into many precision packets travels much farther. Define list-scroll/selection policy, preserve fractional input and test equivalent totals, bursts and reversal. |

Surface acceptance inventory:

| Surface | Required touch behavior |
| --- | --- |
| Settings/confirmation/setup/updater forms | Vertical direct pan, release-velocity fling, bounded elastic return; native text/IME/selection, nested list/dropdown and scrollbar ownership preserved. |
| Glass menus/submenus | Finger pan without command activation, usable submenus and cancellation. Mouse drag-to-select is intentional (`GlassMenu.cpp:1383–1429`); accidental touch command admission is a risk to test, not a proven ordinary-mouse bug. |
| Quick Settings | Direct value sliders, horizontal page gestures, unambiguous tap versus pan, reachable detail/Back actions. No inertial slider values or accidental device actions. |
| Search | Result viewport pan/fling, matching tap, native editor selection/IME arbitration. |
| Dock/previews | Explicit tap/hold/menu/reorder gestures with pointer cancellation; no mandatory hover. Retain accepted mouse behavior and exact footprint. |
| Hidden bars | A tested touch reveal and keyboard path that coexist with Windows gestures. |

Phone-like motion means velocity from timestamped positions, not pressure hardware. Model
PendingTap/Panning/Inertia/SpringBack explicitly; keep logical and UIA offsets bounded while any
elastic visual displacement is temporary. Regrab/reverse without jumps; clamp elapsed time across
sleep; end timers/frames at rest. Reduced motion retains direct tracking and removes decorative
fling/bounce. Handle `WM_POINTERCAPTURECHANGED`/`WM_CANCELMODE`; avoid double inertia on touchpad
sequences already carrying momentum. Every long menu/form must reach its last item by touch with
zero command/system actions after a pan. Verify slow pan, fast flick, both limits, second contact,
capture loss, canceled touch,
hide/lock/DPI/resize, nested controls and equivalent wheel packet totals at 100/150/200% DPI.

Illustrative physical model, not fixed acceptance constants: inertia `dv/dt = -k*v`; spring return
`x'' + 2*zeta*omega*x' + omega^2*(x-bound) = 0`. Tune against measured interaction in DIPs/seconds,
not event count or drag distance; use finite values, bounded steps and explicit settling thresholds.

Settings has deliberate editor-image holds (`FormScrollView.cpp:270–295,435–464`) and synchronous
own-control image capture/allocation (`FormSurface.cpp:715–745`). Profile actual touch-to-present
latency with native editors/IME; do not discard S-052's atomic presentation correction to hide lag.

## Startup, persistence, session events and updates

| ID | Priority / status | Evidence, consequence and correction |
| --- | --- | --- |
| AS4-16 | P1 / Source | Production setup passes `--data-dir` even for the ordinary installed root (`installer/main.cpp:319,742`), but `ShellApplication.cpp:926–927` rejects startup changes whenever that argument exists. Production updater paths do so too. Distinguish installed/default roots from private profiles with canonical identity/provenance. Test installed-default startup controls allowed, isolated profiles refused. This is not sufficient to diagnose AS4-02. |
| AS4-17 | P1 / Source | `launcher/main.cpp:34–38` waits indefinitely while a child remains alive without IPC; Quit/maintenance paths call it (`111,115`) and retain the supervisor lease. Maintenance times out but the supervisor stays stuck. Implement bounded cancellation and a safe ownership handoff; test a child that never creates its channel, simultaneous stop, and exit during handoff. Preserve installation and do not indiscriminately kill applications. |
| AS4-18 | P2 / Source | Settings displays only stored `general.startWithWindows` (`SettingsWindow.cpp:856–857`); the app does not call `StartupRegistration::IsEnabled`, and unchanged Apply compares only stored preference (`1550`). Missing registration, a different target or Task Manager disable can appear enabled. Read effective state asynchronously, distinguish desired from actual state and preserve Windows disable. Test external changes and read failure without touching real startup keys. |
| AS4-19 | P2 / Source | `StartupRegistration.cpp:82–99` accepts launcher paths up to 30,000 characters but writes a Run command that must fit the documented 260-character limit. Validate the entire serialized command, including quoting/arguments, before writing; fail visibly or separately approve a supported alternative. Pure fake-registry boundary tests. |
| AS4-20 | P2 / Source | Setup supports a custom data root (`installer/main.cpp:92–94,319`), but Run registration stores only launcher plus `--autostart` (`StartupRegistration.cpp:99`; `install/Registration.cpp:72–74`). Next sign-in defaults to another profile (`AppPaths.cpp:41–47`). Persist a supported installed-profile identity or explicitly refuse unsupported custom-root startup; never silently switch settings. Test temporary install/launcher round-trip and quoting within startup limits. |
| AS4-21 | P2 / Source | Settings changes startup registration before saving preferences (`SettingsWindow.cpp:1551–1580`), with no reconciliation after save failure. Setup already has rollback handling (`installer/main.cpp:300`). Make partial outcomes explicit and converge persisted/effective state on failure/retry, without undoing a later Task Manager choice. Inject save failure after enable and disable. |
| AS4-22 | P2 / Source / prerequisite | `SystemEvents.cpp:36` silently records failed `WTSRegisterSessionNotification` and never retries; `SessionChanged:72` currently has no consumer. Early-service failure is documented. Add bounded cancellable registration retry and explicit unavailable state; pair successful registration/unregistration. Test initial failure then success, shutdown during retry and permanent failure. This must precede reliable unlock greetings. |
| AS4-23 | P2 / Source, dormant production path | `install/UpdateFlow.cpp:72,80–89` launches the app directly for rollback/health without transferring a successful session to the stable launcher supervisor. That session lacks D34 abnormal-crash supervision. Restore supervision through an explicit health/ownership handoff and temporary-root tests. Signed automatic updates remain unavailable; do not describe this as an observed Alpha 3 incident. |

D34 intentionally retries abnormal Windows exceptions, not ordinary error exit 1 or intentional
Quit (`install/LifecyclePolicy.cpp:16–23`). Preserve that contract; broad retries can mask the
startup failure and create loops. Windows chooses Run scheduling and offers no prompt-start
guarantee. Diagnose absent invocation, delay, bootstrap failure, graphics failure and taskbar-only
failure separately, with sanitized stage/timing/error codes.

## Shared rendering recovery

| ID | Priority / status | Evidence, consequence and correction |
| --- | --- | --- |
| AS4-24 | P2 / Source, reusable-object path | `CompositionSurface::Attach:186` creates a new 1x1 drawing surface. `Detach:206–227` does not reset cached dimensions; `Resize:234` can return early for the previous size, leaving the new surface 1x1/content visual unsized after reattachment. Reset resource-dependent dimensions or use a new resource generation. Test Attach/Resize/Detach/Attach/same Resize and changed-size variants. This is a shared API lifecycle defect, not an established explanation of the owner's external windows. |
| AS4-25 | P2 / Source recovery gap | `SurfaceWindow::Render:251` clears pending rendering before drawing; if device recreation fails (`265–266`), it only logs and schedules no independent recovery. An otherwise idle surface can remain stale until unrelated input; optional frequent updates do not guarantee recovery. Add bounded cancellable shared-device recovery with backoff/fallback, avoiding one retry loop per surface. Inject initial recreate failure then success while surfaces are idle, and persistent failure with usable recovery controls/no busy loop. |

## Observed baseline performance failure

| ID | Priority / status | Executed evidence and next investigation |
| --- | --- | --- |
| AS4-26 | P2 / Observed; unresolved qualification | The fresh Release `/W4 /WX` non-network unit run passed 23/24 suites; `aether_search_tests` failed `tests/unit/search/SearchTests.cpp:129`: the 2,000-application benchmark p95 was 2.547 ms against the strict <2 ms target (p50 1.095 ms; 5,000-app p95 4.554 ms). One unchanged-binary scoped rerun passed all seven cases, 2,000-app p95 0.540 ms and 5,000-app p95 1.229 ms. Preserve both outcomes; the combined gate remains a failed run. Profile cold/warm behavior and representative load, characterize UI impact and target robustness, and correct a cause if established. Do not weaken the benchmark or claim a product fix from the rerun. These measurements do not diagnose blank external pages. See TEST_STATUS S-060 and ignored build evidence. |

## Welcome acceptance and architectural boundaries

FR-041 is a transient supporting surface, not a Windows lock screen replacement or a third
persistent desktop bar. Show only on the authenticated user's normal desktop, once for sign-in
and once per unlock; coalesce duplicate notifications and cancel on lock/disconnect. Do not add
Winlogon changes, a credential provider, elevated service, global input blocking or hidden startup.

Setup/onboarding offers an optional local display name with an explanation, skip, later edit/delete
and no account requirement or account-database scraping. French `Salut {name}` / English
`Hello {name}` follows the chosen language (optional explicit override). Do not log, transmit or
publish this name. Preserve unknown settings on migration/rollback and document preference defaults.

With motion enabled, use original continuous handwriting strokes with a moving tip, speed variation and pen lifts;
measure actual path length. A fade/typewriter/font-outline trace alone fails the visual request.
Shape and lay out the variable name, including accents, long names and a clear readable fallback
for unsupported scripts. Prepare essential shell surfaces concurrently. Existing `ready=true`
health (`ShellApplication.cpp:1056–1057`) is not proof of a successful first present; add that latch.
Optional network/catalog/weather tasks must not gate the animation or essential readiness.

Skip by touch/mouse/Escape immediately; dismiss after writing and essential readiness, with an
independent hard deadline on every outcome. Never keep the desktop covered because a provider
hangs. No extra delay beyond the writing; fast readiness must not truncate the handwriting.
No stacked greetings, service restart on unlock or restart after intentional Quit. Test reduced
motion, high contrast, GPU loss, slow bootstrap,
re-lock during a stroke, duplicate/other-session events, suspend/resume, display/DPI and skip.
Actual handwriting quality and reboot/unlock timing remain owner/device checks. Future macOS
adaptation is a direction to keep in mind, not approved implementation or a compatibility claim.

## Security review boundaries and remaining coverage

No remote/cross-user exploitable vulnerability was demonstrated in this scoped audit. Reliability
defects above are not automatically privilege escalation or proof of attackability. Preserve the
existing exact-byte manifest signature before trusting fields, signed size/SHA-256, locked package
verification/extraction (`install/UpdateFlow.cpp:19–52`), bounded/hashed resource extraction
(`install/Payload.cpp`), checked reparse-point ancestry (`install/InstallState.cpp:120–135`), saved
taskbar baseline/limited handle inheritance (`TaskbarGuardSession.cpp:34–75`) and bounded/filtering
IPC (`SingleInstance.cpp`). This is a source review of those controls, not security certification.

Claude must still investigate malformed IPC, named-object collision/identity, callback exception
containment, stale HWND/PID reuse, reparse/TOCTOU cleanup races, cancellation/worker shutdown,
crash-handler allocation/locks and resource exhaustion. Test trust boundaries without weakening
signature, hash, rollback or path protections to make startup faster.

Additional unclosed qualification: Windows 10, physical multi-monitor/removal/DPI, Explorer restart,
DRM/protected/elevated apps, standard/custom/layered captions, true fullscreen, high contrast,
reduced motion, native IME/screen readers, idle CPU/memory, long-running resource growth, complete
panel/device actions and normal-speed owner review. AppBar result-handling deserves inspection,
but ignored return values alone do not establish a failure: validate each message's documented
return semantics before calling it a bug.

## Validation protocol

1. Reproduce AS4-01/02 with controlled stage diagnostics; add regressions for source findings.
2. Use a matrix of Auto-Hide off/off, on/off, off/on, on/on; maximize/restore/borderless fullscreen;
   healthy/slow/hung/recreated owned targets; theme/transparency changes; helper crash and Quit.
   Require continued marker/content updates, bounded healthy-target progress and exact conditional
   restoration. Compare effects on/off; count geometry/material/region writes per actual change.
3. Run focused Release `/W4 /WX`, combined non-network units and affected owned native suites.
   Keep every failure/skip visible; scoped reruns do not erase a failing aggregate.
4. Profile startup/first usable present, unlock, touch latency, p50/p95/worst frame and UI work,
   idle CPU/memory, helper scans and handle/GDI/resource growth. State hardware/DPI/refresh/load.
   NFR-002 aims at 60 FPS where capable; Q-001 still needs agreed whole-product budgets. Do not
   invent benchmark passes or a permanent zero-bug promise.
5. Inspect continuous changing-frame/normal-speed owned-fixture evidence. Coordinate real
   application, physical touch, reboot/lock and owner acceptance separately. D36 requires a fresh
   ready reply before each real-desktop capture; never perform unrequested reboot/lock or close
   the owner's applications. Preserve the usable installed Alpha 3.

Executed checks for this audit belong in TEST_STATUS S-060. The entries above record the audit as it
was written; the status table at the top records what has since been done (S-061).

## Primary platform references checked for the handoff

- [SetWindowRgn](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-setwindowrgn): region ownership and window-position messages; relevant to AS4-03 and region/event feedback.
- [Run and RunOnce](https://learn.microsoft.com/en-us/windows/win32/setupapi/run-and-runonce-registry-keys): command length and Windows-controlled launch timing; AS4-02/19.
- [Session notification registration](https://learn.microsoft.com/en-us/windows/win32/api/wtsapi32/nf-wtsapi32-wtsregistersessionnotification): early-service failure/readiness, scope and unregister pairing; AS4-22.
- [Session change messages](https://learn.microsoft.com/en-us/windows/win32/termserv/wm-wtssession-change): lock/unlock/logon/desktop-ready events; FR-041 lifecycle.
- [Win32 touch and inertia](https://learn.microsoft.com/en-us/windows/win32/wintouch/architectural-overview): touch/gesture ownership and release-velocity inertia; FR-040.
- [Touch interaction guidance](https://learn.microsoft.com/en-us/windows/apps/develop/input/touch-developer-guide): direct manipulation, discoverability and input parity; FR-040.

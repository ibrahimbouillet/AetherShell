<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Ibrahim Bouillet -->
# ADR 0009 — Reversible visual taskbar replacement

- Status: owner-approved scope change (2026-10-18); platform qualification remains open.
- Decision: D19; requirements FR-001, FR-002, FR-017, FR-024, NFR-003 and NFR-008.
- Supersedes: ADR 0003's blanket no-hide policy and non-AppBar Dock rule only in this mode.

## Context and approval

After trying the first alpha, the owner explicitly requested a V2 iteration that removes the
duplicate native taskbar while Aether Shell runs, places the Dock below applications, reveals the
backdrop through glass, uses the correct Wi-Fi glyph, and includes pinned applications. This is
approval for the narrowly defined visual replacement below. V2 is the product iteration name;
the candidate version is `0.2.0-alpha.1`, not a claim that a stable major release exists.

The request also envisaged Aether Shell becoming the taskbar configured by native Windows Settings.
That integration is not implemented or represented as supported. Windows Settings continues to
configure Windows. Aether Shell's Desktop page owns its own mode and pin import controls.

## Decision

1. Keep exactly two persistent surfaces and keep Explorer running. Never modify Winlogon's Shell,
   require elevation, disable security, or add a third-party library for this feature.
2. Add `general.replaceWindowsTaskbar`, default **false**, as an additive schema-v1 field. The
   Desktop checkbox requires **Apply** for either direction; live autosave/navigation/close does
   not consent. Startup remains a separate explicit choice. Persisted consent can resume the mode
   on a later normal launch.
3. Use the documented taskbar auto-hide state through
   [ABM_SETSTATE](https://learn.microsoft.com/en-us/windows/win32/shell/abm-setstate), preserving
   the captured baseline. Hide only Explorer-owned `Shell_TrayWnd` / `Shell_SecondaryTrayWnd`
   windows. [ShowWindow](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-showwindow)
   is documented; those shell classes and reliable third-party control of their visibility are a
   **compatibility seam**, not a documented taskbar replacement contract. Report refusal/failure.
4. Run this lease in a recovery helper using the existing versioned launcher. Start it with an
   explicit inherited handle list, validate the parent's image directory/session, acknowledge
   readiness, and restore on normal disable, parent exit or parent failure. The app observes helper
   exit and attempts restoration from its independent baseline. Blocking shell/process operations
   stay on workers; no UI waits for the recovery process.
5. Force a bottom Dock AppBar while replacement is requested, reserving its visible panel plus
   animated/focus/shadow footprint. Use the actual rectangle returned by the documented
   [AppBar positioning protocol](https://learn.microsoft.com/en-us/windows/win32/shell/application-desktop-toolbars).
   The visible bottom margin is applied once. This protects maximized application content; it does
   not automatically relocate every manually positioned window.
6. Read available `.lnk` taskbar pins on a worker without changing native pins. Merge supported
   entries without duplicates or overwriting existing Aether pins. An empty Dock is eligible for an
   initial automatic import after replacement consent; successful completion is recorded, while a
   failed read can remain retryable. A manual import remains available. Windows 11 can
   keep additional pins/order in stores this feature does not parse, so completeness is not claimed.
7. Keep `--restore-taskbar` and `--quit` as existing-instance recovery entry points. Neither starts
   a new desktop instance when none is running. They are not an independent system repair utility.

## Boundaries and recovery limitations

Native [Windows Settings launch links](https://learn.microsoft.com/en-us/windows/apps/develop/launch/launch-settings)
open Microsoft's settings pages. Aether Shell does not register as their taskbar implementation,
redirect their controls or claim official replacement status. It also does not reproduce all native
taskbar facilities, including its notification area and system menus.

Restoration is best effort across process/shell failures. If both app and helper fail, automatic
restoration is not assured. A killed helper cannot transfer later native Settings changes to the
parent's initial fallback snapshot. Explorer restart, monitor changes, native auto-hide interactions
and Windows updates require real desktop qualification. The user can disable the Desktop mode or
quit; if recovery is unavailable, restart Windows Explorer through Task Manager. No unattended
registry-based recovery or hidden startup mechanism is introduced.

Automated tests retain NFR-008: fake taskbar backend and synthetic windows only, with no native
taskbar, work-area, startup or audio mutation. Manual owner-authorized qualification is recorded
separately. Pure model tests and own-window captures do not prove real shell recovery.

## Alternatives

Keeping coexistence alone did not meet the owner's revised request. Replacing Explorer/Winlogon or
injecting into Windows was rejected as outside the approved scope. Unconditionally hiding native
windows without an independent recovery process was rejected. Claiming complete Windows pin or
Settings integration without a supported implementation was rejected.

## Amendment: D21 (owner decision, 2026-10-18)

The owner rejected the residual risk described under "Boundaries and recovery limitations" (no
automatic restoration when both the app and its helper fail). The mode must be recoverable without
Aether Shell running, as brief §18 requires:

1. The captured baseline (auto-hide state and which taskbar windows were visible) is written
   atomically to the per-user data directory before anything is hidden, and removed only after a
   successful restoration.
2. `--restore-taskbar` with no running instance restores the native taskbar itself from that baseline.
   It never starts the bars and is idempotent. Without a baseline it only makes the Explorer taskbar
   windows visible and leaves the auto-hide setting unchanged. With a running instance it keeps
   forwarding as before.
3. Setup repair and uninstall restore from a saved baseline when one exists. They never change
   settings, so repair keeps the Desktop choice.
4. `0.2.0-alpha.1` is not published until this is implemented, covered by fake-backend tests and
   checked on the real desktop with the owner's authorization (task T21).

Implementation (2026-10-18), with simulated-taskbar tests recorded in TEST_STATUS S-015:
- `PrepareTaskbarBaseline`, `RecoverTaskbars` and `TaskbarGuardSession` in `src/platform/windows`;
- `RecoverTaskbarWithoutInstance` in `src/app`;
- the repair and uninstall hook in `installer/main.cpp`.

Real-desktop findings (2026-10-18, TEST_STATUS S-016):
- **Dock reservation:** the Dock's first AppBar reservation used an empty rectangle as its work area
  and was always rejected, so replacement never activated. This is fixed in `DockWindow.cpp`.
- **Saved visibility:** a capture that coincided with an Explorer re-layout recorded the taskbar as
  not visible. Restoration therefore now shows every Explorer taskbar window and never hides one.
- **Explorer's own recovery:** on that machine, Explorer showed its taskbar again by itself once the
  killed process's AppBars disappeared. The saved auto-hide state still needed the recovery path.

<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Ibrahim Bouillet -->
# Dock

The native bottom Dock implements the application surface in FR-001 through FR-004: search,
pinned applications and optional unpinned running applications. It contains no status data.
The exact APIs are in `src/dock/DockWindow.h`, `DockModel.h` and `DockLayout.h`.

## Model and layout

`BuildDockItems` merges case-insensitive application identities, including duplicate running
groups, while preserving pinned order and accumulating windows for activation. Pin/unpin/move
helpers return new lists and reject transient window-only identities. `FitDockItems` first reduces
icon size to 32 DIPs, then places trailing running applications in an overflow menu; on exceptionally
narrow work areas pins can also overflow. The minimum fallback keeps search with an overflow menu.

Cosine hover targets use resting icon centers. Springs arrange icons from their actual animated
sizes, on a common bottom baseline with sufficient headroom and no overlap. Hit testing uses this
drawn geometry. The glass panel expands with the icons. Reduced motion snaps to targets and skips
the finite launch bounce. Pointer press feedback, keyboard focus rings and running dots are drawn
with Direct2D; icons use the application icon worker and a GPU bitmap cache. No repeating idle timer
or frame request is used.

## Window and input

`DockWindowContext` receives graphics, scheduler, UI dispatcher, launcher, icon provider and loaded
settings. `Create` creates a topmost tool window without activation, centered above the primary
work area's bottom edge. `PlaceDock` anchors the visible panel edge, so `bottomMargin` is the sole
bottom gap rather than adding the transparent window's trailing padding. The transparent window
bounds include hover headroom; its input region
is the panel and current icons. DPI, display and work-area changes relayout and refresh icon size.
`SetTheme` applies validated settings immediately, and settings notifications keep pin order live.

Search and settings emit signals to the application. Normal clicks activate a running group or
launch a non-running application; middle click requests a new instance. The native context menu
provides Open, New instance, Pin/Unpin, Move left/right, overflow applications and settings. The
Windows taskbar-pin import and Windows Start entries emit `ImportWindowsPinsRequested` and
`StartRequested`; the application owns discovery and shell activation outside the Dock module.
Application drag reordering starts at six DIPs and persists through the asynchronous settings service.
Pinned and unpinned running icons are both draggable. Moving a running icon pins it; earlier
running icons that must precede its destination become pins as well, preserving the exact requested
order under the pins-first model. Existing pin launch metadata is retained. Search and overflow
remain fixed, and a transient window-only identity or the 64-pin limit produces a visible error
without committing a partial order. A completed drag consumes its release rather than launching
the moved application.
Reordered slots spring from their currently drawn positions to their new slots; reduced motion
snaps directly. Hit testing follows these positions during the transition.
Launch errors appear in a temporary anchored tooltip. Tooltip delay is a one-shot timer.

`FocusDock` enters keyboard mode, with arrows, Home/End, Enter/Space, context-menu keys and Escape.
The normal no-activate style is restored when focus leaves; a refused focus request returns an
error. Escape attempts to return to the previous foreground window. UI Automation exposes a
ToolBar with named Button children, pinned/running status, Invoke and focus/structure events.
Invoke queues an HWND message so an overflow menu never holds an accessibility COM call open.

`SetSuppressed` handles full-screen hiding. Optional bottom-edge AppBar reservation uses documented
shell messages, caches unchanged reservation rectangles and removes an existing registration before
re-registering after Explorer restarts. The V2 `replacementTaskbarMode` option and
`SetTaskbarReplacement` force reservation while the application's reversible taskbar mode is active,
even when the ordinary floating-mode `dock.reserveSpace` preference is false. The application owns
native-taskbar visibility/restoration. Dock placement respects the AppBar allocation returned by
Explorer, including other AppBars and an auto-hide reveal strip. Reservation covers the maximum
magnification, launch bounce, focus outline and panel shadow; it is stable while hovering rather
than resizing other applications every animation frame. Reduced motion omits bounce headroom.
Before the first `ABM_SETPOS`, the work area comes from the monitor, not from the still-empty AppBar
rectangle (a real-desktop defect fixed on 2026-10-18, TEST_STATUS S-016). Tests can explicitly
prohibit all reservation and supply a synthetic work area. The production application's tracker, catalog,
shutdown and full-screen policy remain in the app module.

## Executed checks

On 2026-10-18 the private `build/wf-dock2` build passed `/WX`, **14 unit cases and 2 native integration
cases**. The final command was:

```powershell
./scripts/build.ps1 -BuildDir build/wf-dock2 -Modules dock -Jobs 2 -Target @('aether_dock_tests','aether_dock_integration_tests') -Test -TestRegex '^aether_dock'
```

The build cache retains `AETHER_WARNINGS_AS_ERRORS:BOOL=ON`. Unit cases cover identity/order/window
merging, pins, capacity/overflow, validation, magnification symmetry, many pointer positions,
no overlap and actual-geometry hit testing. Integration cases use a temporary settings file, fake
launcher/icons and a private opaque synthetic backdrop. They check enlarged icons, blue synthetic
pixels, search/click/middle/keyboard routing, suppression, unchanged work area and idle frames.
The dark/light rest and hover PNGs in `%TEMP%\AetherShellTests` were visually inspected.
Capture refuses a foreign visible window above the backdrop intersecting the crop, both before
and after capture. No real cursor movement, app launch/activation, work-area or registry mutation
is performed. An initial synthetic hover failure was corrected by cancelling only the test
window's real-cursor leave tracking. A final queued native paint is drained before idle checks.

Real launch/activation, AppBar reservation, Explorer restart, physical mixed-DPI monitors,
Windows 10 blur fallback, high contrast and screen-reader interaction remain unverified manually.

The final rebuild after review fixes also passed all 14+2 cases with the release privacy compile
definition enabled. Added coverage includes queued UIA Invoke, animated drag positions, visual
stack hit testing during icon crossings, and separate-MTA UIA root/navigation/Invoke/disconnection.
Windows denied background UIA focus in both fixtures; the tests require the explicit denial and
restored no-activate style. Successful foreground focus still needs a direct-user-input check.
Queued real cursor motion is filtered only for the test HWND while synthetic moves are sent
directly, keeping rest captures deterministic without moving the physical cursor.

## V2 placement checks — 2026-10-18

The Debug `/W4 /WX` build in `build/wf-v2-dock` passed **19 unit cases and 2 native integration
cases**. The unit gate took 4.37 seconds and the separate integration gate 10.66 seconds. The four
new pure-placement cases cover the sole visible bottom gap, animated/focus/shadow reservation,
reduced-motion headroom, negative monitor coordinates, fractional DPI rounding and invalid native
coordinates. A further model case preserves pinned executable arguments in both conversion
directions. Existing identity, overflow, spring, drag and input cases remain passing.

The own-window fixtures run with replacement mode enabled, a synthetic work area and all real
reservation prohibited; they toggle the mode off/on, check the visible panel edge and assert the
actual work area is unchanged. Dark/light rest/hover PNGs were inspected: the synthetic pattern
remains visible through the tint, icons remain distinct and hover geometry stays separated. No
native taskbar, startup, audio, registry, cursor position or external application was changed.
Background UIA focus was explicitly denied by Windows in both fixtures; successful foreground
focus remains a separate manual check. These results do not qualify real taskbar replacement,
AppBar allocation, other-AppBar coexistence or Explorer restart.

```powershell
./scripts/build.ps1 -BuildDir build/wf-v2-dock -Modules dock -Jobs 3 `
    -Target @('aether_dock_tests','aether_dock_integration_tests') `
    -CMakeArgs '-DAETHER_WARNINGS_AS_ERRORS=ON' -Test -TestRegex '^aether_dock_tests$'
ctest --test-dir build/wf-v2-dock -R '^aether_dock_integration_tests$' --output-on-failure
```

The subsequent running-icon drag correction adds three model cases and expands both native
fixtures with a running-to-pinned drag, unchanged fake launch/activation counters and asynchronous
save completion. That correction's new checks are pending until its focused rebuild finishes;
the 19+2 receipt above predates it. The installed alpha still has the original pin-only drag policy.

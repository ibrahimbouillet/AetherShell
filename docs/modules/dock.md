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
work area's bottom edge. The transparent window bounds include hover headroom; its input region
is the panel and current icons. DPI, display and work-area changes relayout and refresh icon size.
`SetTheme` applies validated settings immediately, and settings notifications keep pin order live.

Search and settings emit signals to the application. Normal clicks activate a running group or
launch a non-running application; middle click requests a new instance. The native context menu
provides Open, New instance, Pin/Unpin, Move left/right, overflow applications and settings.
Pinned drag reordering starts at six DIPs and persists through the asynchronous settings service.
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
re-registering after Explorer restarts. Tests can explicitly prohibit all
reservation and supply a synthetic work area. The production application's tracker, catalog,
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

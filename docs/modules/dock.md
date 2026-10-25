<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Ibrahim Bouillet -->
# Dock

V3 source follow-up (D38/T39, S-035): the native HWND region now uses only the panel and current
animated-icon input rectangles. The shadow-expanded visual override is removed because
HTTRANSPARENT cannot guarantee cross-process input delivery. Outer shadow/entrance decoration is
clipped; physical visual approval, rounded-corner input and separate-process verification remain.
AppBar policy is unchanged. T40 subsequently reduces panel top padding from 12 to 9 DIPs (default
height 70→67), preserving icons, baseline and bottom gap. Checks cover 96/144/192 DPI and custom sizes
(S-036). These patches are not installed over Alpha 2.

T42 adds per-case-normalized application identity marker springs (S-037). Only an observed running
application has a marker: its 5-DIP background dot expands to a 12-DIP pill while hovered or actually
foreground. Hover leaves foreground identity and active color unchanged. Retargeting keeps current
width and velocity; the marker center uses the existing animated icon/reorder geometry. Reduced
motion/high contrast snap, observed closure removes the marker immediately, and settled markers
request no further frames. Removal/overflow discards marker state; a later reappearance starts a
new dot. Pure state tests and existing native Dock checks pass; physical visual acceptance and
complete window-origin/layout motion remain unqualified.

### Hover emphasis and window previews — T42/T43 (Claude, 2026-10-25, S-041)

`DockHoverSession` (`DockHover.h`) is the pure hover/preview state. A running icon under the pointer
is *emphasized* at once: its marker retargets to `kDockMarkerPillWidth`, exactly the foreground
marker's width, while only the observed foreground keeps the active colour. Emphasis never
activates anything. After a 0.6 s dwell (deliberately longer than the 0.45 s name tooltip) the
name tooltip is replaced by `DockPreviewWindow`, a landscape card above the icon. The icon and the
card are one interaction: leaving the icon while a card is shown starts a 0.35 s crossing grace;
entering the card or returning to the icon keeps both the card and the emphasis; leaving both
dismisses the card and the marker contracts. A shown card switches to another running icon after
0.12 s; a non-previewable entry closes it. A click on the icon or a tile closes the card and keeps the
emphasis for 0.8 s, so an observed foreground transfer continues the already expanded width instead
of contracting and regrowing; a refused activation drops that hold. Closed pins, Search, Start and
running groups without a live window keep the name tooltip and never get a card.

Marker motion uses the dedicated `graphics::Motion().marker` spring (stiffness 100, damping ratio
0.92): about 0.35 s to 90 % of a change and 0.52 s to within 0.1 DIP at normal speed (the previous
magnify spring took 0.17 s); overshoot is below 0.01 DIP. Width and velocity survive reversals,
rapid foreground alternation and reorders; the icon magnification is unchanged. Reduced motion and
high contrast snap the marker and show/hide the card at once.

The card has one tile per live window of the group (tracker order, most recent first; as many as fit
the monitor, then an honest `+N` column). Each tile shows a documented DWM thumbnail of that exact
window (`DwmRegisterThumbnail`), fitted inside a 240×150-DIP box without stretching or cropping and
without enlarging a small window beyond one source pixel per device pixel; DWM's invisible resize
borders are cropped via `DWMWA_EXTENDED_FRAME_BOUNDS` when both measurements agree. A window whose
thumbnail cannot be registered, or whose minimized source is only a caption, shows an explicit
placeholder. The header holds the app icon, the window title and the close button at the tile's
upper right. DWM composes thumbnails above the window's own visuals (verified with own windows), so
nothing is drawn over a thumbnail and its opacity follows the compositor fade through DWM. The card
uses the Dock's neutral glass with the host-backdrop blur and the Dock's opacity preference; its HWND
is exactly the card (no shadow margin, no invisible strip over other applications).

Actions revalidate identity first (`DockPreview.h`): the tile's captured HWND must still belong to the
same process and thread and still be listed by the tracker for the group. A tile click activates
exactly that window through the existing `IApplicationLauncher::Activate` path (a window already in
front is left alone rather than minimized). The close button posts `WM_SYSCOMMAND/SC_CLOSE` to that
window only (`AllowSetForegroundWindow` lets its own save prompt come forward); nothing is killed and
no sibling is touched. The button shows a pending state until the tracker removes the window; after
2.5 s a still-open window counts as refused or waiting on its prompt and the button recovers.
Keyboard: with the Dock focused, Up on a running item opens its card; Left/Right/Tab move between
tiles and close buttons, Enter/Space invoke, Delete closes, Escape or Down return to the Dock. The card
exposes a UIA list (`"<app> windows"`) with an invokable button per window and a `Close <title>`
button. Titles and pixels are transient: never logged or stored. Dismissal, refresh, suppression,
DPI/display changes, disabled input and shutdown release every thumbnail. Media play/pause remains a
later T43 step. Own-window tests cover the above; the owner's real-desktop acceptance is pending.

The native bottom Dock implements the application surface in FR-001 through FR-004: search,
pinned applications and optional unpinned running applications. It contains no status data.
The exact APIs are in `src/dock/DockWindow.h`, `DockModel.h` and `DockLayout.h`.

## Model and layout

`BuildDockItems` emits configurable Start, Search and Task View entries before application items.
It merges case-insensitive identities, including duplicate running groups, while preserving pinned
order and accumulating windows for activation. Session ordering can temporarily move unpinned
groups among those pins. Pin helpers require a trusted launch route and reject transient window-only
identities; activation of existing windows remains available. `FitDockItems` first reduces
icon size to 32 DIPs, then places trailing running applications in an overflow menu; on exceptionally
narrow work areas pins can also overflow. The minimum fallback keeps search with an overflow menu.

Cosine hover targets use resting icon centers. Springs arrange icons from their actual animated
sizes, on a common bottom baseline with sufficient headroom and no overlap. Hit testing uses this
drawn geometry. The glass panel expands with the icons. Reduced motion snaps to targets and skips
the finite launch bounce. Spring-based icon press feedback, keyboard focus rings and running dots are drawn
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

Start, Search, Task View and settings emit signals to the application. Normal clicks activate a running group or
launch a non-running application; middle click requests a new instance only with a trusted route.
The shared themed context-menu presenter (native fallback in high contrast)
provides Open, New instance, Pin/Unpin, Move left/right, overflow applications and settings. The
Windows taskbar-pin import and Windows Start entries emit `ImportWindowsPinsRequested` and
`StartRequested`; the application owns discovery and shell activation outside the Dock module.
Application drag reordering starts at six DIPs; holding for the shared long-press interval lifts an
icon. Pinned and unpinned running icons are both draggable. Running icons move only in the session
and never pin themselves or their neighbours; closing their last window removes that session slot.
Moving a pin persists only the order of existing pins through the asynchronous settings service.
Existing pin launch metadata is retained. Fixed entries and overflow never move. Explicit Unpin
records `dismissedPins` so automatic Windows pin sync cannot re-add the item; explicit Pin removes
that dismissal. A completed drag consumes its release rather than launching the moved application.
The captured original icon follows horizontal pointer motion with its original grab offset and is
drawn above its neighbours. Neighbours move only when its center crosses a stable resting slot
center, independent of magnification and animated hit rectangles. Reordering rebases each existing
slide spring instead of restarting it, preserving both current position and velocity. Press, lift
and release scale/offset also use the shared springs. Release settles the original icon into its
destination; reduced motion keeps direct manipulation but snaps decorative movement. There are no
hover/pressed tile backgrounds or pinned/running divider. Focus rings and magnification remain.
`DockLayout::raisedItem` makes overlap hit testing agree with that paint order. Running metadata
updates keep capture and the held identity; removal, disabled input, DPI/display changes and
fullscreen suppression safely cancel the gesture.
Launch errors appear in a temporary anchored tooltip. Tooltip delay is a one-shot timer.

`FocusDock` enters keyboard mode, with arrows, Home/End, Enter/Space, context-menu keys and Escape.
The normal no-activate style is restored when focus leaves; a refused focus request returns an
error. Escape attempts to return to the previous foreground window. UI Automation exposes a
ToolBar with named Button children, pinned/running status, Invoke and focus/structure events.
Invoke queues an HWND message so an overflow menu never holds an accessibility COM call open.

Real background Shell attention requests carry `attentionRequested` through the application model.
A rising request starts three soft red pulses with a small bounce, bounded to 2.4 seconds before
motion-policy scaling. An unchanged true flag never restarts that envelope. A static attention dot
and UIA "Needs attention" status remain until foreground/removal clears the request. Reduced motion
and high contrast use the static cue only. Hidden requests can retain one pending cue until the host
reveals the Dock; the Dock never reveals or activates itself, the application or another surface.
Shell attention availability is conditional on the tracker compatibility API. Normal title changes
do not imply attention, and there is no claimed complete notification/unread-count API.

`SetSuppressed` handles full-screen hiding. Optional bottom-edge AppBar reservation uses documented
shell messages, caches unchanged reservation rectangles and removes an existing registration before
re-registering after Explorer restarts. The V2 `replacementTaskbarMode` option and
`SetTaskbarReplacement` force reservation while the application's reversible taskbar mode is active,
even when the ordinary floating-mode `dock.reserveSpace` preference is false. The application owns
native-taskbar visibility/restoration. Dock placement respects the AppBar allocation returned by
Explorer, including other AppBars and an auto-hide reveal strip. The HWND always includes 32 DIPs
for the two-layer shadow (whose default extent is 31), entrance slide, magnification, lift, bounce and focus outline;
visual margins remain outside the input region. With `reserveAnimationSpace=false` (default),
reservation covers the resting panel and bottom gap. When true, it also covers the maximum
magnification, bounce, lift, focus and panel shadow. It stays stable while hovering; reduced
motion omits bounce/lift reservation.
Before the first `ABM_SETPOS`, the work area comes from the monitor, not from the still-empty AppBar
rectangle (a real-desktop defect fixed on 2026-10-18, TEST_STATUS S-016). Tests can explicitly
prohibit all reservation and supply a synthetic work area. The production application's tracker, catalog,
shutdown and full-screen policy remain in the app module. The host calls `SetSuppressed` with its
combined fullscreen/temporary-native-taskbar-reveal state. The Dock never acts on raw
`ABN_FULLSCREENAPP` hints independently: a shell notification must neither hide it over an ordinary
maximized app nor show it while the native taskbar is temporarily revealed.
While registered, `WM_ACTIVATE` and `WM_WINDOWPOSCHANGED` forward `ABM_ACTIVATE` and
`ABM_WINDOWPOSCHANGED` respectively, with the existing reservation reentrancy guard and default
window-message handling preserved. This follows Microsoft's
[AppBar lifecycle](https://learn.microsoft.com/en-us/windows/win32/shell/application-desktop-toolbars#appbar-size-and-position),
including same-edge auto-hide z-order coordination. The 2026-10-21 notification correction is
Release `/WX` built; Dock31 unit and app2 native pass. A bounded desktop check also confirmed
an owned standard framed window maximized before activation reflowed into the reserved area and
back on normal exit. This does not establish every external application's behavior. The native
Dock light fixture has a recurring UIA root-type failure under separate investigation (S-024).

## Fixed-action icon size — 2026-10-21

Start, Search and Task View draw into the same animated icon rectangle as application bitmaps.
Their shared 16-unit vector grid already leaves optical margins; an additional 18-percent Dock
inset on each side previously reduced visible artwork to about 55–56 percent of the slot. Removing
that duplicate inset brings it to about 86–88 percent without changing layout, hit targets,
magnification, shared stroke width or Top Bar glyphs. Release `/WX` and inspected synthetic dark/light
captures confirm the drawing change; this does not resolve the separate native UIA qualification.

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

## Combined V2 consumer checkpoint — 2026-10-20

The current integration worktree implements the fixed entries, session-only running drag, safe
profile presentation, shared glyph/menu/material consumers and compact reservation policy described
above. `SetApplicationCatalog` builds the pure `ApplicationIndex` and resolves running groups through
`ResolveRunningPresentation`; an unmatched explicit profile can activate its windows but cannot
launch a generic browser or become a persistent pin. Icon keys include `IconSourceFingerprint`, so
later catalog metadata requests a new icon instead of retaining a generic image. Old callbacks and
unused CPU/GPU entries cannot replace that refreshed source.

The app connects `StartRequested`, `SearchRequested` and `TaskViewRequested` to its shell-action
controller, and provides the catalog after discovery. The Dock does not synthesize global keys or
perform shell actions itself. `DockWindowContext::menuPresenter` can inject an in-memory presenter;
production uses `ThemedMenuPresenter` with anchored placement and foreground restoration.
The visual region follows the panel/icons plus their shadow and entrance extents, excluding empty
side/headroom regions. Pointer invocation waits for the finite compositor entrance to settle;
switching to reduced motion or high contrast cancels that entrance immediately.
Modal commands retain the active presenter, check the owner's lifetime after its nested message loop,
and resolve the selected item against the current model before acting. Launch, deferred UIA Invoke
and asynchronous error tooltips require a shown, enabled, unsuppressed surface; closing input
admission prevents a queued request from launching an application during shutdown.

The updated suite contains 31 unit and three own-window native cases, including configurable fixed
entries, session order without pinning, profile route refusal/catalog enrichment, source-sensitive
icon refresh, explicit Unpin dismissal and compact-versus-animated reservation. Existing captures
retain their private backdrop and pre/post guards. These edits have not yet been built or executed
against the combined candidate; previous results above are historical receipts, not validation of
this checkpoint. Real Chrome profile avatars, launch actions, multi-monitor geometry and AppBar
recovery remain separate manual qualification.

Combined candidate verification (2026-10-20, TEST_STATUS S-022): Release /W4 /WX passes **31/31 unit
and 3/3 native cases**. The native checks include fixed-entry requests, unknown-profile route
refusal, catalog/icon refresh, modal hiding, disabled launch admission and queued-tooltip rejection.
Fresh dark-rest and light-hover public images contain only the inspected synthetic fixtures. This
supersedes the pending checkpoint above for automated scope, not actual browser/AppBar qualification.

## Captured motion and attention continuation — 2026-10-21

The local D27 continuation implements continuous captured movement, interruption-preserving
neighbour/press/lift/drop springs, the clean icon-only hover treatment and finite real attention
presentation described above (FR-002/FR-004, NFR-002/NFR-005/NFR-007/NFR-008/NFR-011).
New pure cases exercise stable crossing/reversal, spring rebase continuity, overlap paint/input
priority and the finite pulse envelope. A new isolated native case samples intermediate geometry
before/after a crossed slot and a second crossing, running-state refresh during capture, release
settling, stationary long-hold, unchanged attention and reduced-motion cancellation. It uses fake
launchers, temporary settings and only its own windows; no global input, work-area reservation,
real application activation or capture is required. These new checks are pending execution; the
S-022/S-026 receipts above do not validate this continuation. Physical pointer/refresh-rate and real
Shell notification behavior remain separate owner qualification.

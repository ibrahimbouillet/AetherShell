# Maximized-window corner investigation

**Latest owner clarification, D45 (2026-10-26):** four rounded corners must be accompanied by a
thin continuous Aether-style perimeter around the complete window, including genuine maximize.
The supplied rejected target is an external application; Aether-owned frames need their own path.
S-054 implements the perimeter (ring window beneath foreign windows, own frame for Settings) and
establishes the skip reason privately: the pictured window was layered and therefore ineligible;
see the last section. Follow
[the next Claude brief](../CLAUDE_V3_NOTIFICATIONS_TRAY_FRAME_PROMPT.md), FR-036/T44 and FR-037/T47;
S-053 separates source findings, historical cutout evidence and unverified real-window acceptance.

D38 / FR-036 / T44, 2026-10-23. The owner repeated that maximized applications must have rounded
corners matching Aether. This outcome is still open; neither inspection nor a DWM hint is a fix.

## Source and documented constraint

`src/app/WorkAreaInsets.*` reserves 12-DIP left/right AppBars on the primary monitor. It does not
change another application's maximized state, window procedure, frame or corner radius. An inset
work area does not itself make an OS-maximized window a rounded normal window.

Microsoft excludes maximized/snapped windows from automatic rounding. Its corner preference is a
hint for applicable windows, not a universal override. See [Windows 11 rounding policy](https://learn.microsoft.com/en-us/windows/apps/desktop/modernize/ui/apply-rounded-corners).

`platform::SetCornerPreference` currently returns void and ignores the DWM HRESULT. `SurfaceWindow`
selects DoNotRound for its own composition-backed panels, whose rounded glass is drawn separately.
Changing that flag cannot round unrelated maximized applications. Do not record it as a fix.

## Owned-window prototype — 2026-10-25

`src/settings/ui/MaximizedCornerPrototype.*` now supplies an **unwired experimental helper**.
It has no production caller, persisted option, external-window hook or global policy. The default
product and installed Alpha 2 therefore retain their current corner behavior.

`ComputeMaximizedCornerLayout` intersects physical native bounds, caller-observed DWM visible-frame
bounds and the approved work area, then translates to native-window coordinates. The prototype
uses a provisional 12-DIP radius, scaled by DPI and clamped to the visible dimensions. Widened
arithmetic and explicit GDI coordinate limits reject invalid/overflow/empty geometry.

`Attach` requires a same-process, same-thread, top-level standard left-to-right resizable window.
`Apply` additionally requires true maximized, non-minimized state. A private property identifies
the exact window lifetime. The helper snapshots the original region, intersects it rather than
expanding it, retains only private copies and transfers the applied HRGN to USER32 on success.
Unchanged geometry is skipped. Synchronous position-message reentry is rejected; destruction is
best effort and cannot throw. Restore only replaces a region that still matches this instance's
last applied region. A later different region or lost identity is preserved/reported.

S-038 records seven passing cases, including hidden own-window `IsZoomed`/placement preservation,
rounded-region corner membership, prior nonrectangular region restoration, replacement protection,
reentry and normal/wrong-thread/destroyed-window rejection. All native fixtures remain hidden and
use synthetic frame/work observations: they do not establish actual DWM frame acquisition, visual
antialiasing, caption-button accessibility, shadows, snap/restore interaction or foreign compatibility.

## Next visible qualification

Continue with an opt-in observer on Aether's own standard Settings window
(`src/settings/ui/SettingsWindow.cpp`). Acquire fresh DWM visible-frame/native/work-area bounds,
retain true maximize semantics, and call Restore on restore/minimize/disable/shutdown. The helper
does not install event hooks or perform those lifecycle transitions itself. Before any owner-app wiring:

- distinguish window, DWM visible-frame and approved work-area bounds, including DPI;
- preserve caption buttons, client content, snap/restore behavior and minimum size;
- verify matching visible/input corners, no clipped controls or invisible corner targets, at
  96/144/192 DPI with small and negative-coordinate bounds;
- retain original attributes/region/placement and restore only if the state is still ours;
  retain/validate process and window identity, including handle reuse;
- exclude unsupported/protected/elevated/fullscreen/snapped/external windows until qualified;
- avoid UI-thread blocking and continuous geometry fights with applications.

A restored normal-state window fitted to the work area is a different candidate: it changes
IsZoomed, maximize/restore buttons, snap behavior and app layout/placement. Do not silently substitute
that state for genuine maximize or apply it globally.

No foreign window was moved, clipped, subclassed or injected into. Installed Alpha 2 is unchanged.
T44 has a tested geometry/hidden-owned-window prototype; visible qualification, external compatibility
and icon-anchored choreography remain open. Native region clipping is not an antialiased compositor
corner solution or a universal rounding guarantee.

Native ownership/message contract: [SetWindowRgn](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-setwindowrgn).

## Guarded maximized-window policy — D40 / S-045, 2026-10-25

The owner repeated the real application corner request. `src/app/WindowCorners.*` now provides a
separate implementation that reuses the prototype's pure layout calculation. The original
`MaximizedCornerPrototype` lease class itself remains unwired. `ShellApplication` asynchronously
requests the new policy only during active, unsuspended taskbar replacement; isolated app sessions
cannot enable it. `main.cpp` dispatches its internal inherited-handle mode before ordinary settings,
logging, instance lock or UI bootstrap. No additional startup registration is created.

The separate child observes WinEvents/display/work-area changes and coalesces geometry refreshes.
It captures a window's process/thread, original simple region and private lifetime marker; intersects
physical frame/work-area bounds at a provisional 12-DIP radius; and retains genuine maximize and
placement. Same user/session and non-elevated medium-or-lower integrity are required. Normal/snapped,
full-monitor, shell/security (including Explorer), parent Aether, owned/tool, layered/cloaked,
protected/elevated and nonstandard-region windows are skipped. Original rectangular regions are
accepted only if they contain the clip. App replacements or lost lifetimes win; the child does not
fight the application. No placement/style change, subclass, injection, process termination or disk
inventory is involved.

The child watches parent death and a stop event through an explicit three-handle inheritance list.
Snapshots survive recoverable native errors; owned failures retry at 250 ms. Shutdown removes hooks
before conditional restoration and the child acknowledges by exiting only after snapshots are no
longer needed. The parent waits at most five seconds for a slow recovery child, which may stay
alive until an unresponsive target answers. Hard-killing/crashing the recovery child itself is not
protected by a second guardian or durable journal. Native foreign HWND writes cannot be atomic.
Region edges are rasterized, not compositor-antialiased. These are real remaining limits.

S-045 qualifies fake error recovery and genuine own-window WinEvent/DWM geometry, maximize/placement,
region restoration/replacement and a real fixture-process death. Every native target is restricted
to the test's own PID and explicit HWND whitelist. The test-only lifetime helper has no windows and
is excluded from installation payloads. The kernel parent-death branch is tested; actual production
child dispatch/readiness and external appearance need the separate authorized runtime trial.

## Transparent cutout correction — S-046, 2026-10-25

The owner withdrew the apparent acceptance reply and rejected the dark rectangular remainder
behind the curve. The original tests checked geometry/cleanup, not actual composed corner pixels.
The new own-window fixture reproduces the defect with a custom non-client frame,
`WS_EX_NOREDIRECTIONBITMAP` and `DWMSBT_MAINWINDOW`: all four cutouts differ from the real private
backdrop in both colour phases. Standard and custom GDI frames do not show that defect.

DWM's explicit system material fills the whole rectangular window independently of the region.
The guard now snapshots the exact supported material and temporarily selects `DWMSBT_NONE`
for explicit full-window materials. Read-back verifies it before applying the clip. Region and
material restoration have independent ownership; an app replacing one does not strand the other.
An app's new material wins and releases our clip. Partial first application stays in the observer
with a bounded follow-up so recovery state survives a failed region write. No opaque corner
mask, wallpaper copy, style change or non-client-rendering disable is used.

Removing the system material also changes any transparent interior/title-bar areas that used it;
it does not remove an application's own compositor content. The fixture therefore includes a
genuine opaque `CompositionSurface` body and verifies that it remains visible while the outside
pixels follow the changing backdrop. Exact region/material recovery and app replacements are
separate assertions. Scope remains the eligible-window policy and this machine's native gate;
broader OS/DPI compatibility, raster antialiasing and owner visual acceptance remain open.

Documented API: [system backdrop values](https://learn.microsoft.com/en-us/windows/win32/api/dwmapi/ne-dwmapi-dwm_systembackdrop_type)
and [read/write attribute contract](https://learn.microsoft.com/en-us/windows/win32/api/dwmapi/ne-dwmapi-dwmwindowattribute).

## Rounded perimeter including maximize — D45/D46, S-054, 2026-10-27

Diagnosis (private, read-only; no title, content, path or process inventory recorded): the pictured
application's top-level window is captioned, resizable and genuinely maximized but `WS_EX_LAYERED`,
which the D40 guard excluded, so it was never clipped. Layered captioned windows are now eligible;
click-through overlays stay excluded through `WS_EX_TRANSPARENT`, tool-window and caption/frame
rules. Other maximized windows observed without a lease were not diagnosed further; a late
region/material replacement by the application is the leading hypothesis and is handled by the
bounded reacquisition below, not by overriding the application.

Technique (owner-chosen, [ADR 0012](../decisions/0012-maximized-window-perimeter.md)): the lease
clips the window inside its rounded outline by the rim width, and the guard keeps a click-through
ring window directly beneath it, shaped to exactly that band with the same GDI geometry. Native
tests over two private backdrop phases verify all four cutouts, a continuous ring at every edge
midpoint, the ring beneath the target in z-order, a layered composition window with a backdrop,
rim removal on restore and guard stop, and that an application replacement keeps its own choice until the
next maximize. Aether Settings has its own path: a rounded region over its visible client, a
painted rim and its own caption buttons (DWM draws none under a region); restored, DWM rounds it with
the `windowRim` border.

Measured constraints (owned windows, Windows 11 build 26300): any region on a maximized custom-frame
window removes DWM's caption buttons, also with forced DWM non-client rendering; without a region a
frame-extended window's transparent corners show DWM's frame fill; DWM draws system material
outside a window region (so maximized Settings is opaque); and a standard-frame window under a
region alternates between the basic caption and DWM's own caption composed over the top of the
region while active. The standard-frame native case therefore requires all corners and the bottom,
left and right rim edges, and records the top edge as observed (D46 limit). Not yet qualified: the pictured
application after the change, other rendering families, 192 DPI, negative monitor coordinates on a
physical second monitor and owner visual acceptance.

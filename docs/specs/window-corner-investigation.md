# Maximized-window corner investigation

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

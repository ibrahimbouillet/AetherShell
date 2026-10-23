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

## Next prototype

Start with Aether's own standard Settings window (`src/settings/ui/SettingsWindow.cpp`). Investigate
an owned rounded frame/clip while retaining true maximize semantics. Use pure geometry and a fakeable
native backend before wiring this to owner applications:

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
T44 is investigated, with native prototype, external compatibility and visual acceptance still open.

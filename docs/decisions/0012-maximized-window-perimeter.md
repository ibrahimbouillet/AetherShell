# ADR 0012: Rounded perimeter for genuinely maximized windows

Status: implemented decision under D45/D46 and FR-036/FR-037 (Claude, 2026-10-26/27). The owner chose
the external technique ("rim window underneath") when asked in the implementing session. Owned and
foreign paths are qualified separately; owner visual acceptance and broad compatibility are pending.
Executed evidence is in TEST_STATUS S-054.

## Context
DWM rounds and borders normal Windows 11 windows but draws neither on a maximized one. The owner
rejected square corners on a maximized external application and asked for a thin, continuous
neutral perimeter around every window, maximized included (D45), while keeping genuine maximize,
placement, native caption buttons, Snap, the system menu and UI Automation. D40's child guard
already clipped eligible foreign maximized windows with a rounded region and an exact material
lease, but drew no rim, excluded layered windows and abandoned a window for good after the
application replaced its region or material. Aether's own Settings window took DWM's frame in the
normal state only.

Measured on Windows 11 (build 26300) with owned test windows before deciding:
- a window region on a maximized custom-frame window removes DWM's caption buttons, also with
  `DWMWA_NCRENDERING_POLICY = DWMNCRP_ENABLED`; DWM then reports empty `DWMWA_CAPTION_BUTTON_BOUNDS`;
- without a region, transparent client corners of a frame-extended window show DWM's frame fill,
  not the scene behind the window;
- DWM draws a window's system material (Acrylic/Mica) over the whole window rectangle, outside a
  window region too: with Acrylic kept, the outermost corner pixel of a maximized Settings window
  differed from the bare backdrop in both phases, while the opaque body matched it exactly;
- a standard-frame window under a region alternates between a basic fallback caption (already noted
  under D40) and DWM's own caption, which DWM then composes across the top of the region while the
  window is active (square top corners, no top rim band at those moments).

## Decision
- **Foreign maximized windows: a rim window underneath.** The guard (still a separate child
  process with D40's identity, session, ownership and conditional-restore rules) clips the window to
  its rounded visible rectangle inset by the rim width (one DIP, at least one pixel). For each such
  lease it owns one click-through popup (`WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_NOACTIVATE |
  WS_EX_TOOLWINDOW`, class `AetherShell.WindowCornerGuard.Rim`) placed directly beneath the target in
  z-order, shaped to the ring between the outer rounded edge and the inset clip with the same GDI
  geometry, and filled with one neutral color for the system theme (`#4D4D4F` dark, `#C6C6C8` light,
  the window-frame color in high contrast). The ring never covers application pixels or takes
  input, activation or a taskbar entry; it follows moves and z-order changes and leaves with the clip.
- **Layered application windows are eligible.** A captioned, resizable, genuinely maximized window
  that is merely `WS_EX_LAYERED` (Chromium/Electron transparency) is an ordinary application window;
  click-through overlays stay excluded by `WS_EX_TRANSPARENT`, tool-window and caption/frame rules.
  This was the established cause of the rejected still: its window was layered and therefore skipped.
- **Bounded reacquisition.** After an application replaces the region or material, the guard keeps
  out of that maximized presentation. Once the window is restored, minimized or hidden, its next
  maximize is a new presentation and gets one fresh attempt; there is never immediate re-application.
- **Aether-owned Settings, maximized.** The form paints its whole client as a rounded opaque body
  with the same rim (`windowRim` token), Settings applies the matching rounded window region over its
  visible client, and draws minimize, restore and close itself because DWM shows none under a region.
  The buttons hit-test as `HTMINBUTTON`, `HTMAXBUTTON` (Snap layouts) and `HTCLOSE`, act through
  `WM_SYSCOMMAND` when released over the pressed button, follow Windows 11 hover/press colors and
  report their rectangles through `WM_GETTITLEBARINFOEX` for the system title-bar accessibility.
  Restored, the region is removed and DWM rounds the window with `DWMWA_BORDER_COLOR = windowRim`.
  High contrast keeps the system frame in both states.

## Alternatives rejected
Restore-and-resize to imitate maximize (owner rejected); a global overlay above windows (unapproved,
covers content and input); drawing into or subclassing foreign windows or Explorer (forbidden);
painted wallpaper in the corners (not the live scene); keeping DWM caption buttons by not using a
region (corners show DWM's frame fill); forcing DWM non-client rendering under a region (buttons
still vanish).

## Consequences and limits
- Foreign windows that draw caption buttons through DWM lose them under the region (pre-existing
  D40 behavior); standard-frame windows show the basic fallback caption, or, while active, DWM's own
  caption drawn over the top rim band and top corners. Preventing that would mean overriding the
  application's non-client rendering policy, which cannot be read back and restored exactly, so
  the guard does not. Applications that draw their own caption (Chromium/Electron, most modern
  apps) keep it; the custom-frame and layered fixtures show the full perimeter, while real
  applications of that family remain to be observed.
- The ring and the clip are aliased GDI regions; the corner arc is not antialiased.
- The ring color follows the system app theme, not Aether's own theme setting.
- Owned Settings is opaque (Solid material) while maximized, because DWM's material would cover its
  transparent corners; its Acrylic material returns on restore.
- Protected, elevated, full-screen, other-session and otherwise unsupported windows stay square.
- Physical multi-monitor, 192 DPI and broad application compatibility are not yet observed.

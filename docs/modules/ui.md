<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Ibrahim Bouillet -->
# Shared UI infrastructure

`src/ui`, target `aether_ui`, holds the custom-drawn window base (`SurfaceWindow`), the glass
tooltip (`TooltipWindow`) and the popup-menu model and presenters (`PopupMenu`, `ThemedMenu`). The
Dock, the Top Bar and Search build on it. Colors, geometry and motion come from
[graphics](graphics.md).

## SurfaceWindow

A borderless `WS_EX_NOREDIRECTIONBITMAP` popup with its own composition tree, per-monitor DPI,
pointer and keyboard dispatch and on-demand rendering through the `FrameScheduler`.
`OnPointerCaptureLost()` is raised from `WM_CAPTURECHANGED` (except for the window's own release after
the left button goes up). `SetBackdropPanel(panel, radius, blur, backdropOpacity = 1)` places the
frosted layer under the panel.

`SetInputRegionPx` retains its original behavior: the rectangles also become the HWND's drawing
region through [`SetWindowRgn`](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-setwindowrgn),
which clips drawing outside that region. Search
uses the additive `SetVisualRegionPx` to preserve its exterior shadow and entrance space while
keeping the unused tail of its fixed-height HWND excluded. Visual-only margins return
[`HTTRANSPARENT`](https://learn.microsoft.com/en-us/windows/win32/inputdev/wm-nchittest) from hit testing;
Windows guarantees forwarding that result only to windows on the
same thread, so cross-process click-through in those narrow margins is not promised. Callers that
do not opt into a visual region retain the original behavior; each consumer must qualify its new
geometry independently and exclude unused sides/headroom from the actual HWND region.

Transitions:

- `ShowAnimated(transition, activate)` applies the transition's start state before the window becomes
  visible, shows it and plays the transition on the compositor.
- `HideAnimated(transition)` plays the transition and hides the window when the compositor reports
  completion; `IsShown()` stays true and `IsHiding()` is true until then. An instant transition
  (reduced motion, high contrast, duration 0), a window that is not shown, or a compositor failure
  hides at once.
- `ShowAnimated`, `Show` or `Hide` during an exit cancel it; `Hide` also returns the root visual to its
  resting state, so the next `Show` never starts half faded. `IsTransitioning()` reports a running
  transition.

Use `graphics::MakeEnterTransition` / `MakeExitTransition` with the surface's tokens and motion
policy; for example the Dock slides by its panel height plus a fade, Search rises 12 DIPs from 97%.

## TooltipWindow

A glass bubble (tooltip radius, 8 x 6 DIP padding, caption type, Tooltip elevation) that never takes
focus, is transparent to hit-testing (`HTTRANSPARENT`) and stays inside the work area of the anchor's
monitor. It fades in while sliding 4 DIPs towards its anchor (fast duration, decelerate curve) and
fades out (accelerate curve); reduced motion and high contrast are instant.

Warm-up: `IsWarm(now)` is true while a tooltip is shown and for `tokens.tooltipWarmWindow` (0.6 s)
after it was hidden. Callers then call `ShowFor` at once instead of arming their delay, and the
tooltip switches with a micro fade and no slide.

Changing to reduced motion or high contrast immediately finishes an entrance at rest, or completes
an ongoing exit and hides the tooltip, without waiting for its old animation duration.

## Popup menus

`PopupMenu.h` (foundation) defines the shared `MenuEntry` model, `IMenuPresenter`, the injectable
`IMenuWindowOps` and `NativeMenuPresenter` with the foreground rule (remember the foreground, clear
`WS_EX_NOACTIVATE`, make the owner foreground, track with `rcExclude` above or below the anchor, post
`WM_NULL`, restore).

`ThemedMenu.h` adds owner-drawn menus behind the same interface:

- `ThemedMenu::Build(entries, tokens, dpi)` validates the entries and builds `MFT_OWNERDRAW` items.
  Each item's `dwItemData` starts with an `MSAAMENUINFO` (`MSAA_MENU_SIG` plus the label without `&`),
  so screen readers keep the names. The menu background is `surface.popupBackground`.
- Menus and their item data are owned by the calling UI thread. A non-owning registry validates item
  pointer identity before any dereference, including foreign integer `itemData`; closing a menu
  unregisters its items. `HandleMessage` additionally verifies ownership by the receiving menu.
- `HandleMessage` answers `WM_MEASUREITEM` (DIP metrics scaled to the owner's DPI), `WM_DRAWITEM`
  (Direct2D DC render target: background, hover pill, check mark or radio bullet, label with the
  mnemonic underlined when keyboard cues are on, accelerator text after `\t`, submenu chevron,
  disabled text) and `WM_MENUCHAR` (`&` mnemonics, enabled items only, per menu level). Items of other
  menus pass through.
- `ParseMenuLabel` splits a label into text, accelerator and mnemonic (`&&` is a literal `&`).
- `ThemedMenuPresenter` shows the menu under the same foreground rule. While it is tracked, the owner
  is subclassed (`SetWindowSubclass`) to answer the owner-draw messages, so owners need no forwarding;
  the subclass is removed before the menu is destroyed, however `Show` ends. It falls back to the
  native presenter in high contrast (system-drawn menus with system colors), when the owner is not a
  window of the calling thread, and when the themed menu cannot be built. Command ids are identical
  either way. `LastShowWasThemed()` reports which path ran. `MakeThemedMenuPresenter(tokens)` is the
  `Result`-returning factory; `SetTheme` updates the tokens.

The menu frame, its drop shadow and the submenu arrow area stay system-drawn (Win32 has no documented
dark theme for them); the item area covers the arrow.

## Tests

- Unit (`tests/unit/ui`): popup-menu model and foreground rule with fake window operations;
  themed-menu label parsing, owner-draw data and MSAA names, high-contrast native fallback, DPI
  measuring, mnemonics, drawing into an offscreen DC, and presenter fallbacks.
- Integration (`tests/integration/ui`, own windows only): capture-loss reporting on a never-shown
  window; the presenter's owner subclass during tracking on a never-shown owner with faked window
  operations; animated show/hide, cancellation and instant transitions on a tiny transparent,
  hit-transparent window in a corner of the work area; tooltip fade, warm window and reduced motion;
  the glass probe captures over a private test backdrop.

2026-10-20 focused verification: Release with warnings as errors, 19 UI unit cases and 12 UI native
cases pass. Foreign integer/stale/other-menu item data is rejected; the default HWND region contract
and opt-in visual margins are checked on a never-shown window. The native batch registration crash
found in the initial run is fixed by explicit source/token ownership; cancellation, detach,
destruction and throwing callbacks are covered. Reduced-motion/high-contrast changes finish ongoing
tooltip motion synchronously. Own-backdrop mean RGB deltas are 17.872 (dark host blur), 18.613 (light
no-blur fallback) and 0.000 (high contrast). The three newly generated PNGs were visually inspected:
only the synthetic probe, its own tooltip and its opaque private pattern are present. This does not
establish cross-process visual-margin forwarding, physical monitor/DPI changes or Windows 10.

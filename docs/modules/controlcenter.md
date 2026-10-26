# Quick controls

Requirements FR-026/FR-015/FR-035 and D26/D44. `controlcenter` is a transient accessible panel, not
another persistent desktop surface. Production Top Bar groups network, sound and battery into one
action; individual preferences still determine which status components are displayed.

## Owned Quick Settings panel (D44, T41, 2026-10-26)

The owner chose a complete Aether-owned recreation of their private Windows 11 Quick Settings
reference instead of relocating Windows' own panel. `ControlCenterWindow` now draws that panel:

- **Layout** (DIPs, 360 wide, 8-DIP corner radius): a three-by-two tile grid (96 × 48 tiles, labels
  below), a page column (two dots and a next-page caret), brightness and volume sliders (speaker/mute
  on the left, output-and-mixer button on the right), a footer band with the battery reading and the
  Settings gear. Without a media session the panel is 388 DIPs high, matching the reference. A compact
  media card (72 DIPs) appears above the grid **only** while a real SMTC session is selected; it never
  displaces the first page and no empty media card is drawn.
- **Tiles**: page 1 — Wi-Fi and Bluetooth (split: the main part toggles, the arrow opens details),
  Airplane mode, Accessibility (opens its detail view), Energy saver, Live captions; page 2 — Night
  light, Mobile hotspot, Nearby sharing, Cast, Project, All settings. Pages turn with the caret, the
  dots, Page Up/Down or the mouse wheel over the grid (a touchpad needs half a notch per page).
- **Detail views** slide in from the right and back: Wi-Fi (switch, *Show available networks* →
  Windows' own list via `ms-availablenetworks:`, more settings), Bluetooth (switch, settings),
  Accessibility (Magnifier, Narrator, Live captions, On-screen keyboard via their documented Windows
  shortcuts; Sticky keys as a real switch; more settings) and Sound output (Mute switch, volume mixer,
  output choice in Sound settings). Escape or Back returns to the main view with focus on the opener.
- **Symbols**: `graphics::SymbolRenderer` draws the icon fonts Windows installs (Segoe Fluent Icons,
  then Segoe MDL2 Assets, then original vectors) as DirectWrite glyph runs, pixel-snapped and centred.
  No font is bundled or redistributed (FR-022); no reference pixels are shipped.
- **Material**: Aether's neutral frosted glass with grain (`GlassStyle::neutral`), complete light and
  dark palettes; high contrast uses the opaque system roles. Accent fills follow the Windows accent
  when the accent source is Windows.
- **Motion**: hover arrives in 0.11 s and leaves in 0.16 s, presses settle in 0.07/0.14 s, on/off
  fills in 0.18 s, pages slide in 0.24 s and views in 0.22 s; every tween retargets from its current
  value. Reduced motion snaps. A settled or hidden panel renders no frames (`RenderCount()` seam).
- **Anchoring**: the visible panel's right edge meets the clicked group's right edge and its top sits
  6 DIPs below it, clamped to the work area of the group's monitor (8-DIP edge margin, negative
  coordinates included), before the window becomes visible. A short downward settle (6 DIPs,
  0.985 scale) is the only entrance movement. A small work area scales the whole panel down; below
  0.55 the request fails as unavailable instead of drawing an unreadable panel.
- **Input**: one action per click on release over the pressed target; Tab/Shift+Tab, geometric arrow
  keys, Enter/Space, Page Up/Down and Escape; keyboard opening shows focus cues from the first frame.
  Losing activation (an outside click) or a DPI change closes the panel once (`Closed` is announced
  once even though hiding re-enters through deactivation).
- **UI Automation**: one pane with stable `item-N` children: buttons (Invoke; Toggle only for genuine
  two-state controls: the radios, detail switches, Sticky keys and Mute), sliders (RangeValue only
  when a reading exists), item status on the current page dot, help text for tile captions.

### Capability and honesty matrix

| Control | Direct behavior | Route / readback | Unknown, unsupported or failure |
|---|---|---|---|
| Wi-Fi, Bluetooth | toggle on the main part; details on the arrow | `Windows.Devices.Radios` after explicit consent on the first direct radio action; state reread | unknown/mixed states show *state unknown* and open details instead of toggling; hardware/policy refusal is reported |
| Wi-Fi caption | connection state only | network snapshot | FR-008: never a network or profile name; *Connected* / *Not connected* / *Wi-Fi* |
| Airplane mode, Energy saver, Night light, Mobile hotspot, Nearby sharing | open the matching Windows Settings page | energy saver state is read from `SYSTEM_POWER_STATUS` when available | no documented global switch: marked with an open-in-Settings corner symbol and ", opens Windows Settings" in UIA; never drawn as a working toggle |
| Accessibility | detail view | Magnifier, Narrator, On-screen keyboard and Live captions use documented Windows shortcuts; Sticky keys uses `SPI_SETSTICKYKEYS` and confirms by rereading | captions require build 22621; Sticky keys unavailable when Windows does not offer it; screen-reader presence is read only |
| Live captions tile | documented Win+Ctrl+L | build gate | disabled with an explanation below build 22621 |
| Cast, Project | documented Win+K / Win+P | — | — |
| Brightness | slider | matched internal display only; observed value | unavailable layouts show *Unavailable* with no range |
| Volume, Mute | slider and mute | async Core Audio provider, external changes republished immediately | no device: disabled, no fabricated zero |
| Output selection | detail view: mute, volume mixer, Sound settings | `ms-settings:apps-volume`, `ms-settings:sound` | direct per-device switching is not implemented (no documented public API used) |
| Battery | footer reading, opens power settings | battery snapshot (percent, charging, saver) | absent battery hides the reading; unknown shows *Battery unknown* |
| Media | card with previous/play-pause/next and source cycling | SMTC session capabilities | absent session: no card; unsupported transport disabled |

The panel receives immutable real snapshots and emits explicit intents. It never discovers devices,
opens Settings, changes audio/radios/brightness or injects input itself. The app bridge admits actual
controls only for the production native-access instance, queues blocking work on a dedicated serial
MTA worker, cancels and joins it before teardown. Test instances cannot invoke the bridge. A pending
slider request is shown for at most 1.5 s until Windows reports it; it is never claimed as applied.

### Routing and migration (D44)

`topBar.windowsQuickSettings` (default **false**) chooses Windows' own Quick Settings (Win+A,
Windows 11). The Top Bar page labels it **Use Windows Quick Settings (Windows 11)**. The legacy Alpha 2
`topBar.openWindowsFlyouts` defaulted to true, so a stored true cannot be told apart from an untouched
default: V3 still reads and writes it unchanged for older builds sharing the file but no longer routes
on it. Every existing Alpha 2 file therefore opens the Aether panel; choosing Windows' panel again is
one explicit click and persists. Installed Alpha 2 itself is not modified. When the native option is
chosen, the app closes only its own panel before submitting the documented chord; no blind retry or
second panel follows an uncertain request.

## Earlier history

V3 source follow-up (T41/S-036) routed the grouped action to Win+A when the old flyout flag was on;
S-026 recorded the previous two-card media-first panel. Both are superseded by the panel above.
Real radio/media/brightness actions require separate manual checks and are never automated by
integration tests; automated tests use synthetic snapshots and collect intents without a host.

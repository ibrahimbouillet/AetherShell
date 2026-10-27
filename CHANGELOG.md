# Changelog

All notable changes to Aether Shell are documented here. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/) and the project uses
[Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

## [0.3.0-alpha.3](https://github.com/ibrahimbouillet/AetherShell/releases/tag/v0.3.0-alpha.3) — 2026-10-27

Third alpha, Alpha 3 (owner decisions D48 and D49): the V3 design and interaction work since Alpha 2.
Release notes: [docs/releases/0.3.0-alpha.3.md](docs/releases/0.3.0-alpha.3.md). Owner acceptance of
the latest behavior (S-054 to S-057) is pending.

### Added

- **Auto-Hide for the Dock and the Top Bar** (D47): two independent, persisted settings, off by
  default (Settings → Dock → *Automatically hide the Dock*, Settings → Top Bar → *Automatically hide
  the Top Bar*). An enabled bar slides away while an application window covers it and slides back
  when the pointer rests on its screen edge (bottom for the Dock, top for the Top Bar). It stays while
  in use, while one of its menus or panels is open, and on the uncovered desktop. Auto-hidden bars
  reserve no screen space, so maximized windows use the full screen; full-screen suppression keeps
  priority.
- **Notifications and calendar** (D45): the Top Bar date and time opens the Windows notification
  center (Win+N on Windows 11, Win+A on Windows 10); the clock menu adds the calendar (Win+Alt+D).
  Aether never reads, clears or badges notifications.
- **Hidden icons** (D45): a Top Bar chevron (Settings → Top Bar → *Show hidden icons button*, on by
  default) opens Windows' own notification-area overflow directly below it, in front of other
  windows. It is a toggle: the arrow points up while the overflow is open, and a second click closes it.
- **Aether Quick Settings** (D44): the grouped network, audio and battery action opens a rebuilt
  quick-controls panel with split tiles, two pages, sliders, conditional media controls and detail
  views for Wi-Fi, Bluetooth, accessibility and sound output. Switches that Windows does not expose
  to applications open the matching Windows Settings page. *Use Windows Quick Settings (Windows 11)*
  routes the action to Windows' own panel instead.
- **Dock window previews** (T42/T43): hovering a running app widens its marker and, after a short
  dwell, shows a glass card with genuine previews of its windows. Each preview has a close button for
  that window only; pointer travel into the card keeps it open.
- **Rounded maximized windows** (D40/D46, ADR 0012): while Aether replaces the taskbar, eligible
  maximized windows are clipped to rounded corners, with a thin continuous rim drawn by a
  click-through window beneath them. Placement and maximize state are preserved and every change is
  undone on exit. A maximized Settings window draws its own rounded frame and caption buttons with
  native Snap layouts, system commands and accessibility.

### Changed

- **One glass design across Aether** (D41/D42): Settings, the logo and context menus, setup,
  maintenance, the updater and confirmations share the neutral frosted-glass material, controls,
  typography, glyphs and motion. Native text editing, keyboard input and accessibility are kept.
- **Fluid Settings** (D43): Settings stays coherent while it moves and scrolls (spring scrolling,
  retained layers, animated hover), and its title strip shares the window material while keeping the
  native caption buttons, Snap, system menu and resizing.
- A more compact Dock (3 DIPs less top padding, same icon sizes and bottom gap). Running markers
  animate from dot to pill on hover and activation, and hover cards fit their thumbnails.
- The Top Bar's focus ring appears only after keyboard navigation, as Windows shows focus cues; a
  click no longer outlines the chevron.
- Dock and Top Bar input regions are narrowed to their visible panels.

### Fixed

- Grey rectangular shadow fragments around raised Dock icons on light windows.
- Rectangular system backdrops behind rounded maximized-window corners. Chromium windows that
  re-apply Mica are clipped again, so they no longer stay square behind other windows.
- Settings now appears even when it is the first window of a process that was started hidden.
- Executable-only desktop windows stay on their unique pinned Dock icon.
- Preview close gestures cancel when capture is lost or the window list changes; keyboard selection
  follows the chosen window.
- Interrupted Quick Settings detail transitions continue from where they are; precision scrolling,
  media arrival and press-and-release targets behave correctly.

### Known limits

Edge reveal and Auto-Hide with real applications, the hidden-icons toggle on every Windows build,
multi-monitor and DPI changes, Windows 10 and some window-rendering families are not yet qualified
on real hardware. See [KNOWN_LIMITATIONS](KNOWN_LIMITATIONS.md).

## [0.2.0-alpha.2](https://github.com/ibrahimbouillet/AetherShell/releases/tag/v0.2.0-alpha.2) — 2026-10-23

Second public alpha, including the previously local V2 candidate below.

- Fresh setup discloses sign-in startup with an opt-out; existing choices and Task Manager disables
  remain respected. The stable installed launcher retries at most three abnormal exits.
- Observed audio changes reach an open Quick Controls slider immediately, without waiting for the
  periodic media refresh or issuing new audio commands.
- Native reusable capture/review tools retain strict local privacy boundaries. Fresh presentation
  media remains pending and is not bundled with the installer.
- Exact-revision Windows CI, Release `/W4 /WX`, package audit and scoped actual installed
  startup/recovery checks are recorded in HANDOFF and TEST_STATUS S-034.

### V2 changes

- Reversible visual taskbar replacement enabled by default (D23), preserving explicit opt-out, with Desktop Apply controls,
  a separate recovery helper, status/errors, and existing-instance quit/restore commands.
- Dock bottom-margin correction and mandatory animated-footprint AppBar reservation in replacement
  mode, keeping maximized application content above the Dock.
- Balanced Start, Search and Task View icon sizes with application icons by removing duplicate
  padding from their Dock drawing boxes; shared status-bar glyph sizes are unchanged.
- Framed maximized windows no longer suppress both Aether bars as fullscreen. Raw AppBar hints
  recheck current foreground policy, and registered Dock activation/position notifications reach
  the shell without overriding the host's temporary-reveal policy.
- Read-only best-effort Windows pinned-shortcut import with retained launch arguments and deduplication.
- More visible glass translucency, shadow restricted to the outside, opaque Windows-transparency-off
  and high-contrast fallback, and distinct Wi-Fi waves/Ethernet/cellular glyphs.
- Taskbar recovery without Aether Shell running (D21):
  - the taskbar state is saved before anything is hidden;
  - `--restore-taskbar` restores it with no instance running, and setup repair/uninstall restore it
    too;
  - restoration always shows the taskbar (D22).
- Fixed: replacement mode never activated on a real desktop because the Dock's first AppBar
  reservation was rejected.
- Owner-approved D19 / ADR 0009 records the narrow taskbar-hiding exception. Explorer remains intact;
  native Windows Settings takeover is unsupported. Full V2 delivery qualification is pending.
- Interrupted batch recovery adds shared theme/glyph/menu/transition primitives, compact Dock tiles,
  pin and running-item reordering, profile-aware presentation, one-time Explorer pin initialization,
  additive pin sync with dismissal/revision protection, and an Appearance settings page.
- Top Bar actions include keyboard/network/weather/clock/power/sound access and temporary native
  taskbar reveal with manual or 60-second resume. The Dock hides only after native restoration.
- The microphone menu requests Windows voice typing (Win+H) in the previous application; endpoint
  mute and microphone privacy settings remain separate actions. Aether does not record speech.
- Review fixes reject unknown profile relaunches, validate icon-file ancestors, reject foreign menu
  item pointers and stale modal actions, and prevent hidden animated Search rows from activating.
  Fresh combined native/desktop qualification is in progress; see TEST_STATUS S-022.

## [0.1.0-alpha.1](https://github.com/ibrahimbouillet/AetherShell/releases/tag/v0.1.0-alpha.1) — 2026-10-18

First manual-install native development alpha.

### Added
- Modular CMake build with presets, a Visual Studio environment build script and CI.
- Core library (results, strict JSON, cancellation, worker pool, dispatcher, signals, atomic files)
  and rotating diagnostic log.
- Win32 platform layer (DPI, monitors, system events, single instance, instance channel, message loop).
- Composition graphics layer: Direct2D on Windows.UI.Composition, glass material with Windows 11
  backdrop blur and a translucent fallback, design tokens, interruptible animations, tooltips.
- Real clock/battery/input/network/audio providers and opt-in Open-Meteo weather with bounded cache.
- Async settings persistence, migrations, startup consent and configurable global shortcuts.
- Immutable application search index, native search/settings windows and UI Automation surfaces.
- Native animated Dock and separate Top Status Bar with original branding and synthetic-window captures.
- Application lifecycle, versioned install/repair/uninstall, health-checked update/rollback and setup.
- Public specification, traceability, handoff, end-user guides and clean-public-export policy.

### Qualification
- Complete Release /WX build and public Windows CI pass. Local evidence: 426 unit cases,
  91 integration cases across scoped serial runs and 32 release-tool cases. Aggregate desktop
  interference/refusals remain recorded in TEST_STATUS; full Version 1 acceptance is open.
- Setup and checksums are published. Automatic updates are unavailable for this alpha; the unsigned
  manifest remains local. See KNOWN_LIMITATIONS.

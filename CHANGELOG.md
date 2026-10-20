# Changelog

All notable changes to Aether Shell are documented here. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/) and the project uses
[Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### 0.2.0-alpha.1 candidate — V2

- Reversible visual taskbar replacement enabled by default (D23), preserving explicit opt-out, with Desktop Apply controls,
  a separate recovery helper, status/errors, and existing-instance quit/restore commands.
- Dock bottom-margin correction and mandatory animated-footprint AppBar reservation in replacement
  mode, keeping maximized application content above the Dock.
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

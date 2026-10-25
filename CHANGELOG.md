# Changelog

All notable changes to Aether Shell are documented here. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/) and the project uses
[Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### V3 development

- Narrow Dock/Top Bar native input regions to their panels/current icons so decorative outer
  margins do not rely on same-thread-only input forwarding; external shadows are currently clipped.
- Reduce Dock panel top padding by 3 DIPs while preserving icon sizes and the visible bottom gap.
- Request native Windows 11 Quick Settings from the grouped status action when enabled; provide
  an explicit Top Bar preference to retain Aether controls. Native opening still needs device review.
- Record the V3 design/motion requirements and the unresolved maximized-corner prototype plan.
- Animate observed running markers from dot to pill on hover/foreground with continuous native
  springs, preserved active-state identity and immediate reduced-motion/high-contrast feedback.
- Add an unwired owned-window corner prototype with DPI clipping, true-maximize preservation,
  region/identity ownership and conditional restore; visible/external qualification remains open.
- Hovering a running background app expands its marker to exactly the active marker's width with a
  dedicated, calmer marker spring; after a deliberate dwell a rounded glass card shows genuine DWM
  previews of its windows, each with a close button that requests a normal close of that window
  only. Pointer travel into the card keeps it open; activation continues the expanded marker.

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

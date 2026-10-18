# Changelog

All notable changes to Aether Shell are documented here. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/) and the project uses
[Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

Future work.

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

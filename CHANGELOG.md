# Changelog

All notable changes to Aether Shell are documented here. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/) and the project uses
[Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

Development towards `0.1.0-alpha.1`. Nothing has been released yet.

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
- Scoped unit/own-window gates are recorded in TEST_STATUS. Full Release/lifecycle/public CI validation
  is in progress; no public release is claimed. See KNOWN_LIMITATIONS before distribution.

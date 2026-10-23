![Aether Shell — a modular native desktop interface for Windows](resources/branding/png/readme-banner.png)

<p align="center">
  <a href="https://github.com/ibrahimbouillet/AetherShell/releases/tag/v0.2.0-alpha.2">Download Alpha 2</a>
  · <a href="USER_GUIDE.md">User guide</a>
  · <a href="KNOWN_LIMITATIONS.md">Known limitations</a>
  · <a href="docs/knowledge/TEST_STATUS.md">Test evidence</a>
</p>

<p align="center">
  <a href="https://github.com/ibrahimbouillet/AetherShell/actions/workflows/ci.yml"><img alt="Windows CI" src="https://github.com/ibrahimbouillet/AetherShell/actions/workflows/ci.yml/badge.svg"></a>
  <a href="LICENSE"><img alt="MIT license" src="https://img.shields.io/badge/license-MIT-2ea44f.svg"></a>
  <img alt="Windows x64" src="https://img.shields.io/badge/platform-Windows%20x64-0078D4.svg">
</p>

# Aether Shell

A native Windows desktop interface by **Ibrahim Bouillet**. Aether Shell pairs a floating Dock with
a compact Top Status Bar, using a softly blurred, translucent material. It is built in C++20 with
Win32, Direct2D/DirectWrite and Windows.UI.Composition, and is released under the MIT License.

> **Alpha software:** 0.2.0-alpha.2 is an early public preview. It is not a fully qualified Windows
> shell replacement. Save your work before testing and read the [known limitations](KNOWN_LIMITATIONS.md).

## What it does

- **Dock:** launch and switch between applications, keep favorites, reorder pinned icons, see running
  apps, open Search, and reach Start and Task View.
- **Top Status Bar:** show Windows-provided clock, input language, network, audio and battery status,
  with supported system actions and a grouped quick-controls panel.
- **Weather:** optionally use Windows location permission for a coarse position and a forecast. If
  location or the network is unavailable, weather remains unavailable rather than guessing.
- **Motion and appearance:** animated Dock interactions, dark/light appearance, keyboard navigation,
  reduced-motion support and a translucent Windows composition surface.
- **Recovery:** a per-user installer, a separate recovery helper, repair/uninstall support and a
  Windows-taskbar restore command.

## Replacing the Windows taskbar

The V2 experience hides the Windows taskbar when Aether Shell starts, then displays the Aether Dock
in its place. This behavior is enabled by default and can be switched off in **Aether Shell Settings
→ Desktop**. Explorer continues running; Aether Shell does not replace `explorer.exe`, change the
Winlogon shell, or register itself as the native taskbar in Windows Settings. The recovery helper
restores the Windows taskbar when Aether Shell exits. Read the [recovery guide](USER_GUIDE.md) before
trying the alpha.

## Download and install

Download the **x64 setup executable** from the [0.2.0-alpha.2 GitHub Release](https://github.com/ibrahimbouillet/AetherShell/releases/tag/v0.2.0-alpha.2)
and compare its SHA-256 with the release's checksums file. Run setup as your regular Windows user.
The installer explains the taskbar behavior and does not require administrator rights. Fresh setup
visibly selects **start at sign-in**; you can clear that option before accepting. Existing saved
choices and Task Manager startup disables are respected. The installed launcher can retry a limited
number of abnormal crashes; Quit, recovery and uninstall remain available. Windows controls startup
timing, so Aether cannot guarantee that its interface appears before the native taskbar at sign-in.

This alpha is distributed for manual installation. Automatic updates require a signed release
manifest; no unsigned manifest is published. The setup binary is not Authenticode-signed, so Windows
may show a SmartScreen warning. See [installation and verification](INSTALLATION.md).

## Compatibility

- Windows 11, x64: current development target.
- Windows 10 2004+ x64: supported by the design, but not runtime-qualified in this release.
- ARM64: not runtime-qualified.

Windows 10 backdrop blur uses a simpler tint. The Dock does not reproduce every native taskbar
feature, and Windows Settings continues to manage the Windows taskbar. See the full [known
limitations](KNOWN_LIMITATIONS.md).

## Visual preview

Fresh screenshots and recordings of Alpha 2 will be added to the [versioned gallery](docs/media/README.md)
after capture and privacy review. The installer is the current delivery priority. Earlier component
fixtures are not presented as this version's appearance.

## Build and test

Build on Windows with Visual Studio C++ Build Tools, a Windows SDK, CMake, Ninja and Python:

```powershell
./scripts/build.ps1 -Config release -BuildDir build/release -Test -TestExclude 'network|integration' `
    -CMakeArgs '-DAETHER_WARNINGS_AS_ERRORS=ON'
```

To build the per-user installer and run its package privacy/freshness audit:

```powershell
./scripts/package.ps1 -BuildDir build/release
```

See [BUILDING.md](BUILDING.md) for focused module tests, [TESTING.md](TESTING.md) for test isolation
and manual qualification, and [the executed test record](docs/knowledge/TEST_STATUS.md) for exact
results. A successful build or test suite does not by itself qualify every Windows version or device.

## Project documents

- [Architecture](ARCHITECTURE.md) · [Project specification](PROJECT_SPEC.md) · [Task status](TASKS.md)
- [User guide](USER_GUIDE.md) · [Installation](INSTALLATION.md) · [Update and rollback](UPDATE_AND_ROLLBACK.md)
- [Security policy](SECURITY.md) · [Contributing](CONTRIBUTING.md) · [Third-party notices](THIRD_PARTY_NOTICES.md)

For contributors and coding assistants, start with [AGENTS.md](AGENTS.md) and [HANDOFF.md](HANDOFF.md).

## License

MIT. See [LICENSE](LICENSE).

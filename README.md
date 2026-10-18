![Aether Shell](resources/branding/png/readme-banner.png)

A native Windows desktop interface by **Ibrahim Bouillet**: a floating application Dock and a
separate Top Status Bar, running alongside Windows Explorer. Built with C++20, Win32,
Direct2D/DirectWrite and Windows.UI.Composition. MIT licensed.

**V2 development candidate: `0.2.0-alpha.1`.** The changes below are being qualified; no V2 release
or CI result is claimed yet. The [published 0.1.0-alpha.1](https://github.com/ibrahimbouillet/AetherShell/releases/tag/v0.1.0-alpha.1)
is the earlier manual-install alpha, with its own [passing Windows CI receipt](https://github.com/ibrahimbouillet/AetherShell/actions/runs/39123368917).
See [known limitations](KNOWN_LIMITATIONS.md) and [executed evidence](docs/knowledge/TEST_STATUS.md)
for the scoped tests and remaining Version 1 qualification. Automatic updates are unavailable for
this alpha; no unsigned manifest is published.

- Dock: pinned and running applications, icons, search, keyboard navigation and motion preferences.
- Top Bar: real clock/date, battery when present, input language, network, audio and microphone state.
- Search: application catalogue matching, accent-insensitive ranking and keyboard selection.
- Settings: Dock, Top Bar, opt-in weather, updates, shortcuts, startup consent and diagnostics.
- Delivery: per-user setup, versioned installation, repair, uninstall and authenticated update design.

V2 adds **Settings → Desktop → Replace the Windows taskbar → Apply**. This optional mode is off
by default. It hides the native taskbar while Aether Shell runs, reserves the Dock's space below
maximized windows, and restores Windows on disable/exit with a separate recovery helper. Available
Windows pinned shortcuts can be imported without modifying the native pins. Glass now reveals the
backdrop more clearly, and Wi-Fi uses curved waves instead of cellular bars.

Windows Settings keeps its native taskbar page; configure Aether Shell in its own Desktop page.
Pin coverage/order and recovery across all Windows/Explorer failures remain limited. Read the
[mode and recovery guide](USER_GUIDE.md) and [known limitations](KNOWN_LIMITATIONS.md) before enabling it.

Windows 10 2004+ and Windows 11, x64. Windows 10 and ARM64 remain unqualified. The native
Windows taskbar stays visible in the default coexistence mode. Explorer keeps running in either
mode. Weather is disabled by default; enabling it sends the selected
coordinates to Open-Meteo. No location detection or Wi-Fi network names are used.

Read the [user guide](USER_GUIDE.md), [installation guide](INSTALLATION.md), and
[update and rollback guide](UPDATE_AND_ROLLBACK.md).

These dark-theme previews come from isolated rendering tests. Application names, status values and
the search error are synthetic fixtures; the images contain only the project's own test windows.

![Dock rendering fixture](resources/screenshots/dock-dark-rest.png)
![Top Bar rendering fixture](resources/screenshots/topbar-dark.png)
![Search rendering fixture](resources/screenshots/search-dark.png)

Light-theme fixtures: [Dock](resources/screenshots/dock-light-hover.png),
[Top Bar](resources/screenshots/topbar-light.png), [Search](resources/screenshots/search-light.png).

Build on Windows with Visual Studio C++ Build Tools, a Windows SDK, CMake and Ninja:

```powershell
./scripts/build.ps1 -Config release -BuildDir build/release -Test -TestExclude 'network|integration'
./scripts/package.ps1 -BuildDir build/release -SkipBuild
```

See [BUILDING](BUILDING.md) for focused modules and interactive tests. Python is needed for release
packaging/branding tools, not to run the app. No third-party C++ runtime library is bundled.

For contributors and coding assistants: start with [AGENTS](AGENTS.md), [HANDOFF](HANDOFF.md),
[PROJECT_SPEC](PROJECT_SPEC.md) and [TASKS](TASKS.md). The Markdown/source files are portable;
Obsidian and Graphify are optional local development tools. Never commit personal information,
credentials, build output or local tool state. Read [CONTRIBUTING](CONTRIBUTING.md),
[SECURITY](SECURITY.md) and [third-party notices](THIRD_PARTY_NOTICES.md).

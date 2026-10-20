<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Ibrahim Bouillet -->
# Architecture

Aether Shell is a native C++20 Windows application running alongside Explorer. Its persistent
desktop surfaces are the bottom Dock and Top Status Bar; search, settings and update windows are
supporting interactions. Product requirements and approval states belong to
[PROJECT_SPEC.md](PROJECT_SPEC.md). This file describes implementation boundaries; current
qualification belongs to [HANDOFF.md](HANDOFF.md) and
[TEST_STATUS.md](docs/knowledge/TEST_STATUS.md).

## Layers and ownership

The [CMake module registry](cmake/AetherModules.cmake) defines the actual dependency graph and
resolves dependencies for focused builds. Modules normally produce `aether_<module>` static
libraries; the application and delivery tools have executable targets.

| Layer | Responsibility | Reference |
|---|---|---|
| `app` | Own services, connect requests, apply settings, coordinate startup/shutdown and recovery | [App](docs/modules/app.md), [ShellApplication](src/app/ShellApplication.cpp) |
| `dock`, `topbar`, `search_ui`, `settings_ui` | Present models, maintain input/accessibility geometry, emit or route explicit user actions | [Dock](docs/modules/dock.md), [Top Bar](docs/modules/topbar.md), [Search UI](docs/modules/search_ui.md), [Settings](docs/modules/settings.md) |
| `applications`, `search`, `status`, `status_audio`, `weather`, `settings`, `updates`, `install` | Application identity and launch, search index, provider snapshots, persistence and delivery policy | [Module index](docs/knowledge/HOME.md) |
| `ui`, `accessibility`, `graphics` | Win32 surface lifecycle, menus/tooltips, UI Automation, text, composition, glass and motion | [UI](docs/modules/ui.md), [Graphics](docs/modules/graphics.md) |
| `core`, `diagnostics`, `platform`, `net` | Results/cancellation/workers, bounded logging, Windows APIs and WinHTTP transport | [Technical map](PROJECT_CONTEXT.md) |
| `launcher`, `updater`, `installer` | Stable entry point, independent package verification, transactional per-user delivery and recovery | [Install](docs/modules/install.md), [Installer](docs/modules/installer.md) |

The application supplies catalogs and provider interfaces to the surfaces. Application identity,
profile launch routes and icon sources are resolved in `applications`; Dock rendering does not
discover profiles or inspect browser account data. Status snapshots preserve unavailable/unknown
values. Weather remains optional and uses only the user's chosen configuration.

## Threads, rendering and lifetime

The UI thread owns Win32 windows, COM STA presentation, composition and mutable UI models.
Blocking discovery, icon extraction, network, install and persistence work belongs on workers;
delivery returns through `IUiDispatcher`. Icon extraction has its own STA worker requirements.
Public module operations use `aether::Result<T>` and callbacks contain exceptions at their boundaries.
Provider shutdown and weak lifetime/generation checks prevent late deliveries from acting on a
destroyed window. The app closes input admission and settles settings before teardown.

Each surface uses `WS_EX_NOREDIRECTIONBITMAP` and a composition target with a backdrop visual and
a Direct2D drawing surface. DirectWrite and layout use DIPs; native bounds/regions use pixels.
`FrameScheduler` requests frames only after changes or while an animation needs them. Finite
window transitions run on the compositor, with cancellation and completion lifetime handled by
`CompositionSurface`. Shared tokens cover theme, contrast, transparency, text and motion policy.

Input regions and optional visual regions are distinct: exterior shadow/entrance space must not
turn unused window area into a large invisible input target. Visual-only margins return
`HTTRANSPARENT`, whose forwarding guarantee is limited to same-thread windows. See the
[surface contract](docs/modules/ui.md) for these limits and the module fixtures for geometry checks.

## Windows integration and recovery

[ADR 0003](docs/decisions/0003-window-behavior.md) describes window behavior;
[ADR 0009 and D23](docs/decisions/0009-reversible-taskbar-replacement.md) describe reversible taskbar
replacement. Replacement defaults on at launch and preserves a saved explicit opt-out. Changing
that choice in Desktop settings requires Apply; startup registration remains consent-only.

The production app permits native taskbar access only when both its native-access flag and the
production instance name allow it. Isolated lifecycle tests use a separate instance and disable
native access. Dock/AppBar reservation is coordinated before hiding the native taskbar; full-screen
suppression and temporary reveal are runtime state. A worker-owned recovery helper restores the
captured taskbar state on normal stop or parent death. Parent fallback observes helper failure;
a persisted baseline supports `--restore-taskbar` and setup repair without a running app.

This is a Windows compatibility feature with documented limits. Explorer, Winlogon and Windows
Settings remain system components. Test doubles establish controller policy; actual taskbar,
Explorer restart and physical monitor behavior require separate desktop qualification.

## Update and installation trust

The [update trust model](docs/decisions/0004-update-trust-model.md) treats GitHub metadata, release
notes and downloads as untrusted. Only the configured official repository is accepted. The exact
manifest bytes must verify against compiled-in ECDSA P-256 public keys before manifest fields are
trusted; asset size and SHA-256 must then match that signed manifest. Signing secrets and their
locations never belong in this public repository.

The updater independently verifies the package and extracts resources without executing the
downloaded package. The [installation transaction](docs/decisions/0005-installation-layout-and-rollback.md)
stages a version, switches activation, waits for health and commits or rolls back. A working version
is retained until its replacement commits. Registry/process/filesystem interfaces support temporary
isolated fixtures. See [RELEASE_PROCESS.md](RELEASE_PROCESS.md) for publication gates and
[TESTING.md](TESTING.md) for the evidence each layer can establish.

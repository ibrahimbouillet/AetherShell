<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Ibrahim Bouillet -->
# Testing

Read [AGENTS.md](AGENTS.md) and the current [handoff](HANDOFF.md) before running checks.
[BUILDING.md](BUILDING.md) covers tool installation and build options;
[PROJECT_SPEC.md](PROJECT_SPEC.md) defines acceptance criteria.
[TEST_STATUS.md](docs/knowledge/TEST_STATUS.md) records executed commands, revisions, failures and
remaining scope. This guide is not a test-results snapshot.

## Automated gates

Run commands from the repository root in PowerShell. `scripts/build.ps1` imports the Visual Studio
environment and uses CMake/Ninja. Give concurrent workers separate `build/wf-<name>` folders and
coordinate compile jobs. Do not use `-Fresh` on another worker's build directory.

```powershell
# Complete Release build and CI unit gate; no desktop or Internet suites.
./scripts/build.ps1 -Config release -BuildDir build/ci `
    -CMakeArgs '-DAETHER_WARNINGS_AS_ERRORS=ON' `
    -Test -TestExclude 'network|integration' -JUnitFile build/ci/test-results.xml

# Focused Dock build and unit execution, with both Dock test executables available.
./scripts/build.ps1 -Config release -BuildDir build/wf-dock -Modules dock -Jobs 2 `
    -Target @('aether_dock_tests','aether_dock_integration_tests') `
    -CMakeArgs '-DAETHER_WARNINGS_AS_ERRORS=ON' -Test -TestRegex '^aether_dock_tests$'

# Inspect the desktop suite selection before a separate serial execution.
ctest --test-dir build/wf-dock -N -R '^aether_dock_integration_tests$'
ctest --test-dir build/wf-dock -R '^aether_dock_integration_tests$' --output-on-failure -j 1
```

CTest registers module executables; the in-house framework reports individual cases inside each
executable. Preserve both counts when reporting a run. Use exact test names or labels to avoid
accidentally running unrelated suites. `-Test` excludes the `network` label by default; it does not
exclude integration tests. An unqualified `./scripts/build.ps1 -Test` therefore needs an interactive
desktop and must not be treated as a unit-only command.

| Label | Scope and execution conditions |
|---|---|
| `unit` | Pure models, policy, validation, rendering helpers or controlled local fixtures; included in CI. Some cases measure synthetic performance. |
| `integration` | Interactive Windows desktop and native own-window/read-only fixtures; inspect the selected tests first and run GUI suites serially. |
| `network` | Explicit live Internet access; excluded from ordinary runs and CI. Review its requests and configuration before selecting it. |

The label and target conventions are defined in [AetherTargets.cmake](cmake/AetherTargets.cmake).
The checked-in [CI workflow](.github/workflows/ci.yml) builds Release with warnings as errors and
executes the unit gate. CI artifacts and successful compilation do not establish desktop behavior.

## Isolation and capture privacy

Automated checks must not change the real taskbar/work area, audio state, startup or uninstall
registry entries, or launch external applications. Use fake launchers/providers, injected registry
and process operations, temporary settings/install roots, and explicit reservation/native-access
guards. Isolated full-app sessions use a separate instance name and disable native taskbar access;
synthetic Dock/Top Bar bounds prohibit AppBar operations. Inspect these protections before execution.

Capture only project-owned test windows over an opaque private synthetic backdrop. The backdrop
must cover the crop and its blur guard band. Check for foreign overlap before and after capture;
an occlusion/privacy refusal is a failed or blocked capture, never permission to weaken the guard.
Do not move the physical cursor or manipulate unrelated application windows to make a fixture pass.
Current examples are [GlassProbeTests](tests/integration/ui/GlassProbeTests.cpp),
[DockIntegrationTests](tests/integration/dock/DockIntegrationTests.cpp) and the
[Search capture fixture](tests/integration/search_ui/CaptureFixture.h).

Do not commit raw local logs, dumps, build output or owner desktop captures. Public evidence must
exclude credentials, personal paths, account identifiers, machine names and unrelated application
content. Review generated Release binaries/payloads separately before publication, following
[RELEASE_PROCESS.md](RELEASE_PROCESS.md).

## Manual desktop and performance qualification

Real taskbar hide/restore, process failure recovery, Explorer restart, startup, audio controls,
external launch, physical monitor/DPI transitions and assistive-technology workflows are separate
checks. Obtain the owner's explicit authorization for the actual desktop operations, agree the
bounded procedure and recovery path, and keep their evidence distinct from automated fixtures.
Do not kill Explorer or mutate real settings as part of an automated integration suite.

For performance measurements, finish builds and other competing work first and use a quiet,
representative desktop. Record configuration, source revision, workload, sampling method and raw
observations. Preserve a timing failure observed under concurrent compilation; a later quiet rerun
adds evidence rather than erasing that failure. An unchanged-model/idle-frame assertion demonstrates
the tested scheduler behavior, not whole-product idle CPU, memory or startup performance.
Global release budgets remain an owner decision under Q-001 in
[PROJECT_SPEC.md](PROJECT_SPEC.md); module engineering targets do not settle that decision.

## Reporting and delivery

After a relevant change, run its focused checks and the required combined gate. Record the exact
revision or uncommitted candidate, build configuration, command/selection, result, failures and
remaining limitations in [TEST_STATUS.md](docs/knowledge/TEST_STATUS.md), the affected module notes
and [HANDOFF.md](HANDOFF.md). Keep implemented, tested, visually inspected and manually qualified
claims distinct. A test that did not execute contributes no passing evidence.

Use [KNOWN_LIMITATIONS.md](KNOWN_LIMITATIONS.md) and the specification's acceptance criteria for
remaining platform/release work. A green unit matrix does not qualify a live signed update,
desktop recovery, visual approval or stable publication.

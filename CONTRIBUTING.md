# Contributing to Aether Shell

Thanks for your interest! Aether Shell is created and maintained by Ibrahim Bouillet and released
under the MIT License. By contributing you agree that your contribution is licensed under the same
terms.

## Scope

Version 1 contains exactly two desktop surfaces: the **Dock** and the **Top Status Bar**, plus their
supporting windows (search, settings, updates, installer). Please open an issue before starting work
on anything outside that scope.

## Building

See [BUILDING.md](BUILDING.md). Short version (Visual Studio 2022+ Build Tools with the C++ workload):

```powershell
./scripts/build.ps1 -Config debug -Test
```

## Engineering rules

- C++20, one module per directory (`cmake/AetherModules.cmake`), public APIs return `aether::Result<T>`.
- Never block the UI thread; background work goes through `aether::WorkerPool`.
- Use documented Windows APIs. If something cannot be done reliably, report it as unsupported rather
  than guessing.
- Never fabricate data (time, battery, network, weather...). Unknown stays unknown.
- Build warning-free at `/W4`; CI treats warnings as errors.
- Add tests: `tests/unit/<module>` (pure logic), `tests/integration/<module>` (real desktop),
  `tests/network/<module>` (Internet). Do not claim results you did not run.
- Every source file starts with the SPDX MIT header and the copyright line.
- Keep documentation accurate: rules in `AGENTS.md`, module docs in `docs/modules`, decisions in
  `DECISIONS.md` and `docs/decisions`, progress in `TASKS.md`, session state in `HANDOFF.md`.

## Pull requests

1. Fork and branch from `main`.
2. Keep changes focused; describe what you verified and how.
3. CI must pass. Integration tests that need a desktop should be run locally and reported in the PR.

## Optional development tools

[Graphify](https://github.com/Graphify-Labs/graphify) (code knowledge graph) and
[Obsidian](https://obsidian.md) (knowledge vault rooted at `docs/`) are optional developer aids. They
are never required to build or run Aether Shell, and their generated outputs are not committed.

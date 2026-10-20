# AGENTS.md — shared contract for every coding assistant

**Read this file before modifying anything.** Then read `HANDOFF.md` ("NEXT SESSION START HERE"),
then the applicable requirements in `PROJECT_SPEC.md`, `TASKS.md` and `DECISIONS.md`.
Open other documents only when the task needs them. `PROJECT_SPEC.md` defines the intended product;
source and executed checks establish current behavior. A previous assistant's completion claim is
not verification. Preserve requirement/decision IDs, link significant tasks to them, and obtain owner
approval before changing a fundamental objective or protected requirement.
The owner starts every session with [docs/SESSION_START_PROMPT.md](docs/SESSION_START_PROMPT.md);
keep that prompt and this file consistent. Work only in the checkout whose `origin` remote is the
official repository; the original development checkout is a local history archive (D18, GAP-012).

## Project
Aether Shell: a native Windows 10 (2004+)/11 desktop interface with exactly two desktop surfaces in
Version 1 — a floating **bottom Dock** (application icons, launch, search entry, pinning, running
indicators, animations) and a floating **Top Status Bar** (clock/date, battery, input language,
network, volume, microphone, optional weather). It runs beside Explorer and never replaces it.
The owner-approved V2 iteration (`0.2.0-alpha.1`, D19/D23/ADR 0009) adds reversible visual replacement
of the Windows taskbar. It starts enabled unless the user has saved an explicit opt-out (D23);
Settings → Desktop requires Apply to change that choice. Quit and `--restore-taskbar` restore the
native taskbar. It does not replace the native Windows Settings taskbar page.
Owner/author: Ibrahim Bouillet. License: MIT. Official repository: `github.com/ibrahimbouillet/AetherShell`
(created; delivery receipts are in `HANDOFF.md` and `TASKS.md`). Technical reference: `PROJECT_CONTEXT.md`.

## This repository is PUBLIC
Everything committed here will be published in the public GitHub repository. Never write private
information into code, comments, tests, fixtures, logs, documentation or commit messages: no
credentials, tokens, API keys or private keys (or their file locations), no personal data (e-mail
addresses, phone numbers, account names other than the public owner name "Ibrahim Bouillet"), no
user-specific absolute paths (use `%LOCALAPPDATA%`, `%USERPROFILE%`, `<repo>`), no machine names, no
screenshots or captures that show anything other than the project's own test windows, and no content
from the owner's other files or applications. When in doubt, leave it out and ask the owner.

## Non-negotiable constraints
- C++20, Win32, Direct2D/DirectWrite, Windows.UI.Composition. No Electron, no new third-party libraries
  without a recorded decision in `DECISIONS.md`.
- Never replace `explorer.exe`, edit the Winlogon `Shell` value, disable security features, or require
  administrator rights. D19/D23 permit hiding the native taskbar only in the reversible replacement
  mode with its recovery helper, disclosed default and persistent opt-out. Do not extend that exception to permanent
  disabling, shell replacement or Windows Settings takeover. No silent elevation or hidden autoruns.
- Never fabricate data (time, battery, network, weather, test results). Unknown stays unknown.
- No system status items in the Dock. No new desktop surfaces in Version 1.
- Updates only from official GitHub Releases, verified by the signed manifest + SHA-256 (ADR 0004).
  Never execute downloaded content before verification; never delete the working installation before
  the replacement is committed (ADR 0005).
- Public module APIs return `aether::Result<T>`; exceptions never cross module or thread boundaries.
- Never block the UI thread (network, discovery, icon extraction, disk) — use `aether::WorkerPool` and
  `IUiDispatcher::Post`. Idle UI requests no frames.
- Prefer documented Windows APIs; mark anything unreliable as unsupported, in code and docs.
- Accessibility, Per-Monitor-V2 DPI, reduced motion and high contrast are requirements, not extras.

## Code conventions
- One module per directory, static library `aether_<module>`, sources globbed non-recursively; the
  module registry and dependencies are in `cmake/AetherModules.cmake`.
- Every source file starts with `// SPDX-License-Identifier: MIT` and
  `// Copyright (c) 2026 Ibrahim Bouillet`. Match the style of `src/core`.
- UTF-8 in `std::string`, UTF-16 in `std::wstring` (`aether::text::Widen/Narrow`). RAII everywhere;
  `winrt::com_ptr` for COM. Log with `AETHER_LOG_*` (no secrets, no per-frame logging).
- Do not rewrite working code for taste. Keep changes focused.

## Build, test, validate (Windows, PowerShell)
```powershell
./scripts/build.ps1 -Config debug -Test                       # everything, unit+integration tests
./scripts/build.ps1 -BuildDir build/<name> -Modules <module> -Test -TestRegex <module>   # focused
./scripts/build.ps1 -Config release -BuildDir build/ci -CMakeArgs '-DAETHER_WARNINGS_AS_ERRORS=ON' `
    -Test -TestExclude 'network|integration'                  # what CI runs
```
The script imports the Visual Studio environment itself. Lint gate: warning-free at `/W4`
(`/WX` in CI); there is no separate formatter configured. Test labels: `unit`, `integration`
(needs an interactive desktop), `network` (needs Internet). Details: `BUILDING.md`.
Integration tests that capture the screen must capture only their own test windows over a private
test-pattern backdrop (see `tests/integration/ui/GlassProbeTests.cpp`) and must never change real
settings, registry Run/Uninstall keys, the work area, audio state, or launch applications.

## Git and repository rules
- Branch `main`; author identity is the configured `Ibrahim Bouillet` identity. Inspect
  `git status` first; never discard or overwrite uncommitted work you did not create.
- No force-push, history rewriting or indiscriminate resets. Commit only verified, coherent changes;
  end commit messages with the attribution line your environment requires.
- Pushing verified milestones to the official repository is covered by the owner's instructions once
  the repository exists; publishing a GitHub Release or changing repository settings needs the owner's
  explicit approval.
- Never commit: secrets, keys (`*.pem` etc.), build output (`build/`, `dist/`), generated graphs
  (`graphify-out/`, `.local/`), local tool state (`.claude/`, `.obsidian/`).
- Parallel sessions: one owner per module directory at a time; each builds in its own `build/wf-<name>`.

## Documentation duties (keep intended requirements and observed behavior distinct)
- Requirements/acceptance changed → `PROJECT_SPEC.md` (preserve IDs and approval state), then affected
  task/evidence links. Progress changed → `TASKS.md`. Decision made/changed → `DECISIONS.md` (+ an ADR in `docs/decisions/`
  when significant). Session ends → `HANDOFF.md`. Test results → `docs/knowledge/TEST_STATUS.md`.
- Module reference docs: `docs/modules/<module>.md`. Never mark anything verified without running it.

## Tools (recorded observations, 2026-10-18)
Build tools, Graphify, Obsidian and Codex orchestration were rechecked in this continuation session;
other version/identity rows retain the prior session's observations unless explicitly refreshed.
| Tool | Status | Use |
|---|---|---|
| VS Build Tools 2026 (MSVC 14.51), SDK 10.0.26100, CMake 4.4.3, Ninja 1.13 | working | via `scripts/build.ps1` |
| Python 3.14 (+ Pillow, numpy), OpenSSL (Git for Windows) | working | release scripts, branding renders |
| `gh` (GitHub CLI) | working, authenticated as `ibrahimbouillet` | repository operations (owner-approved only) |
| **Graphify** (historically 0.9.92, PyPI `graphifyy`) | works from an ordinary shell (`graphify --version` → 0.9.92, rechecked by Claude 2026-10-18); fails inside the Codex sandbox (`uv trampoline failed to canonicalize script path`) | Historical verified interface: `query "<question>"`, `explain <node-id>`, `path <id-a> <id-b>` (exact IDs for ambiguous names), `update .`, `export obsidian --dir .local/graphify-obsidian`. `[EXTRACTED]` edges differ from inferred edges. No graph exists for this checkout yet; build one with `graphify update .` from an ordinary shell if useful, otherwise use source/Markdown. |
| **Obsidian** 1.14.4 + CLI (`obsidian`, on PATH) | working **only while the Obsidian app runs** | vault `docs` = the repository's `docs/` folder (config in `docs/.obsidian/`, local, git-ignored); `obsidian search query="..." limit=5`, `obsidian read path="knowledge/HOME.md"`; plain file reads work without it |
| Claude Code project skill/hooks (`.claude/`, local only) | set up only in the archive copy, not in this checkout | optional; other assistants ignore it |
| Codex session tools | verified: PowerShell execution, file editing and parallel review/build agents | Each agent owns assigned files and a private build directory. Other connectors/browser tools were available in the environment but not exercised for this task. |

Access requirements: Graphify output (`graphify-out/`) and the Obsidian app/config exist only on the
machine where they were set up; another assistant may have neither. The Markdown files in the
repository are the portable source of truth. Sync workflow: change code → `graphify update .` (optional)
→ update the affected Markdown notes → commit the Markdown (never `graphify-out/`, `.local/`,
`.obsidian/`, or the owner's own vault files such as `docs/Untitled.canvas`).

Discovering tools in a new environment: check each command above with `Get-Command <name>` (Graphify
also at `%USERPROFILE%\.local\bin`), check whether `graphify-out/graph.json` exists and how old it is,
and whether the Obsidian app is running before using its CLI. Your own assistant's tools, skills and
MCP servers are listed by your environment — never assume another assistant has them, and record newly
verified tools in this table.

Efficient context gathering: read `HANDOFF.md` → applicable `PROJECT_SPEC.md` IDs → `TASKS.md` /
`DECISIONS.md` → the module's `docs/modules/*.md` →
targeted `grep` for known identifiers (cheapest) or `graphify query` for dependencies/unknown areas →
only the source sections you change. Do not load whole directories or the whole graph report.

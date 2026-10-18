# Development log

Short milestone summaries; use `git log` for detail.

## 2026-10-17
- Project created in `Documents/AetherShell`; Git initialized (`main`), identity Ibrahim Bouillet.
- `647701d` foundation: CMake module registry, presets, `scripts/build.ps1`, MIT licence, test
  framework, core, diagnostics, platform. All tests pass.
- Graphify 0.9.92 installed (uv tool, project-scoped skill and hooks under `.claude/`, local only).
- Release signing key pair (ECDSA P-256) generated outside the repository; public key compiled into
  the updates module.
- `158ffe1` graphics + UI base: composition glass with Windows 11 blur verified by a screen-capture
  probe; shared contracts for status, applications and settings.
- ADRs 0001, 0002, 0003, 0005, 0006 written. Knowledge base created (this vault).
- Phase A module workflow interrupted by a usage limit; relaunched to continue from partial files.

## 2026-10-18
- `af0e20f` WinHTTP client (net) reviewed and committed; 55 tests pass.
- Continuity files added at the repository root (AGENTS, PROJECT_CONTEXT, TASKS, DECISIONS, HANDOFF);
  module work orders saved in `docs/specs/`; superseded vault notes (PROJECT_STATE, ARCHITECTURE,
  DECISIONS) merged into the root files.
- Obsidian CLI verified (vault `docs`) while the app runs; Graphify `query`, `explain` and `path`
  verified (the latter two need node ids when a name is ambiguous).

### Continuation and specification session
- Owner requested one authoritative product specification. Added root PROJECT_SPEC with stable
  FR/NFR IDs, acceptance, verification methods, scope/vision, decision caveats and task/evidence links.
  Canonical roles/read order reconciled across AGENTS, TASKS, DECISIONS, PROJECT_CONTEXT, HANDOFF,
  CLAUDE and vault notes; legacy unsafe audio test permission and absent-installer wording corrected.
- Preserved interrupted status/audio/weather implementations; focused fixes and independent review.
  Status fresh /WX: 45 unit + 8 read-only integration; audio /WX: 23 + 5; weather fresh /WX: 54 unit.
  Exact commands and coverage limits in TEST_STATUS. No runnable app, remote, commit, push or release.
- Open findings remain: status concurrent lifecycle/MTA ownership (GAP-003), weather six-hour expiry
  during blocking HTTP (GAP-006), settings persistence thread model/hotkey contract (GAP-007/GAP-008).
- Obsidian 1.14.4 rechecked working; Graphify launcher now fails, no installed uv package found.
  Stale graph left untouched; Markdown/source used. Owner canvas and local configuration preserved.


### Native alpha continuation
- Implemented Dock/Top Bar, Search/Settings UI, application ownership and per-user delivery.
  Closed status/weather/settings lifecycle gaps with independent reviews and regressions.
- Complete Release /WX build and 426 unit cases pass; all 91 native/loopback integration cases have
  scoped passes. Aggregate GUI interference/refused captures remain recorded in TEST_STATUS S-011.
- Release tooling 32 cases pass; actual Release binaries and public-source candidates audited.
  Original history stays local under D18; clean official public source repository created.
- Owner-authorized alpha installation observed separately: Installed apps entry/launcher, startup
  disabled. Public push/CI/release receipts follow; no live signed-update qualification claimed.

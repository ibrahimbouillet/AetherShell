# Tasks

Never mark a task done until its acceptance criteria were verified (command run, result observed).
Requirements and definitions: [PROJECT_SPEC.md](PROJECT_SPEC.md). Test evidence:
[TEST_STATUS](docs/knowledge/TEST_STATUS.md). Every task also follows NFR-004/NFR-008/NFR-010
(privacy, safe tests and continuity); listed links below identify its main product contribution.

## Current priority
Reach **Milestone 1 — first runnable application**: Dock + Top Status Bar visible on the desktop,
real icons, launching, real clock, safe startup/shutdown.

## Current continuation priority
The owner explicitly resumed implementation through the Dock, Top Bar, runnable app, installer,
public handoff and delivery. Build the product first; publish only a reviewed clean snapshot with
independent Git history, retaining the existing development history locally (D18).

## T5 Phase A — current scope
Module gates are narrower than complete product acceptance. Exact executed evidence is in
TEST_STATUS S-011. The native alpha is installed; the public source uses independent clean history.

| Task | Requirement links | Current progress / next acceptance |
|---|---|---|
| T5c status | FR-005–FR-008; NFR-002, NFR-004, NFR-005 | GAP-003 resolved and independently reviewed. Fresh /WX: 49 unit + 10 read-only integration + 1 isolated shutdown cases pass. Explicit owned WinRT runtime cleanup, ADR 0008. |
| T5d status_audio | FR-009; NFR-004, NFR-005, NFR-008 | 23 unit + 5 read-only integration cases pass /WX; real audio changes remain unqualified. Shared exception-log hardening passed the settled regression. |
| T5e weather | FR-010; NFR-002, NFR-004, NFR-005 | GAP-006 resolved: HTTP and expiry scheduling separated. 56 cases pass /WX, including expiry while HTTP is still blocked; independently reviewed. No live API request. |
| T5f settings | FR-014, FR-016; NFR-002, NFR-005 | Async model/persistence and chord validation implemented, 33 unit cases pass /WX; five native UI cases pass below. GAP-007 resolved by ADR 0007; editor regression closes remaining GAP-008 evidence. |
| T5g search | FR-012; NFR-002 | Implemented and reviewed. Seven Release cases pass, including cancellation/closed-dispatcher; 2000-app p95 0.601 ms and 5000-app p95 1.962 ms in the recorded run. |
| T5h accessibility | NFR-007, NFR-006 | 3 unit + 3 own-window integration cases pass /WX; independent review. Actual screen-reader/Windows 10 qualification remains. |
| T5i branding | FR-022; NFR-009, NFR-011 | Original SVG/PNG/ICO/wordmarks/banner/installer art generated and visually inspected twice, 12 raster sizes and 8 ICO entries checked. Alpha proposal; final owner direction remains Q-002. |

## Done (verified)
- T0 → NFR-009, NFR-010: workspace/toolchain/Git (`main`) and GitHub CLI identity historically checked.
- T1 → NFR-001, NFR-008, NFR-009, FR-023: build system, MIT license, test framework (scaffolding only).
- T2 → NFR-002, NFR-005, NFR-006, FR-017: core/diagnostics/platform tests (`647701d`), historical scope.
- T3 → FR-001, NFR-002, NFR-007, NFR-011: graphics/UI base and glass probe (`158ffe1`), historical scope.
- T4 → FR-018, NFR-003, NFR-005: net, 55 cases after review (`af0e20f`), historical evidence.
- T5a → FR-018, FR-019: updates, 117 cases, prior fresh lead recheck (`c568564`); live release path pending.
- T5b → FR-003: applications, 72 cases, prior fresh lead recheck (`90fca92`); actual launch/UWP-frame qualification pending.
- T-DOC-1 → NFR-010: PROJECT_SPEC and canonical links independently reviewed; all 34 requirement IDs
  covered by task/evidence map, 22 document link/whitespace checks passed (TEST_STATUS S-004).
- Docs: ADRs 0001–0003, 0005, 0006; BUILDING, SECURITY, CONTRIBUTING, CHANGELOG, THIRD_PARTY_NOTICES;
  knowledge vault; continuity files (this set).
- Tooling: Obsidian 1.14.4 + CLI works while its app runs. Prior Graphify success is historical;
  current launcher fails and the graph is stale (GAP-004). Neither is a product dependency.

## Remaining (priority order)
| # | Task / requirements | Depends on | Acceptance criteria |
|---|---|---|---|
| T6 | Dock (`src/dock`) → FR-001–FR-004; NFR-002, NFR-007, NFR-011 | applications, settings, accessibility | Implemented; final 14 unit + 2 own-window cases pass /WX, including cross-review fixes/drag springs. Final fixture has 128px opaque containment and unchanged foreign guards. |
| T7 | Top Bar (`src/topbar`) → FR-001, FR-005–FR-011; NFR-002, NFR-007, NFR-011 | status, status_audio, weather, settings, accessibility | Implemented; 7 unit + 2 own-window/read-only-provider integration cases pass /WX. Dark/light captures inspected; independent review. Real AppBar/full-screen/Explorer qualification remains. |
| T8 | Search/settings/update UI → FR-013, FR-015, FR-019; NFR-007, NFR-011 | search, settings, updates, weather | Search: five native cases pass, including finite transitions; Settings: five native cases pass, including delayed startup/reset/modifier/DPI/shutdown. Real IME remains unverified. |
| T9 | Install/launcher/updater/setup → FR-016, FR-020, FR-021; NFR-003, NFR-005 | updates, settings, branding | Install library: 10 unit + 7 integration cases pass /WX. Setup: five native cases pass, including maintenance relocation/viewport. Tests use temp roots/fakes/injected keys. Production alpha delivery observed separately. |
| T10 | App lifecycle → FR-001, FR-016, FR-017, FR-021; NFR-002, NFR-005 | T6–T9 | Native Release executable, three CLI cases and isolated lifecycle/health/channel/shutdown case pass. Health-before-discovery ordering and lock-before-cleanup reviewed; updater handoff reviewed without live signed execution. |
| T11 | Full build/desktop checks → FR-001, FR-003, FR-005–FR-021, FR-023; NFR-002–NFR-009 | T10 | full build and relevant tests; separate explicitly authorized desktop checks for DPI, Explorer restart, full-screen, AppBar, real launches/controls/startup; record gaps |
| T12 | Official public repo/push/CI → FR-023; NFR-004, NFR-009 | T11 (first runnable build) | official repository created; audited clean export/push and actual CI receipts being completed |
| T13 | Release pipeline/signing → FR-018, FR-020, FR-021, FR-023; NFR-003, NFR-009 | T9, T12 | real assets, valid manifest/signature/hash, draft release receipt; owner approval for publication/settings |
| T14 | Release qualification → FR-001, FR-003, FR-009, FR-023; NFR-002, NFR-006, NFR-007, NFR-011 | T11 | Windows 10, monitor changes/removal (primary-monitor V1, not extra bars), accessibility/performance evidence; resolve Q-001 |
| T15 | End-user docs/screenshots → FR-022, FR-023; NFR-004, NFR-009 | T11 | Guides implemented; six individually inspected synthetic screenshots included. Keep CI/release receipts current; no complete Version 1 acceptance implied. |

## Dependencies and unresolved acceptance gaps
- Complete Release /WX and 426 unit cases pass. All 91 integration cases have scoped passing runs;
  aggregate interactive runs also refused foreign-overlapped captures or lost Search activation.
  See S-011 for per-run evidence; no uniformly green aggregate desktop run is claimed.
- D18 keeps the original development checkout/history local. The official public repository now
  exists; audited independent-source push, CI and first manual alpha receipts are being completed.
- Signing remains owner-controlled. Unsigned manifests stay local; manual alpha assets are only
  audited setup/checksums. Never relax verification for a missing signature.
- Q-001 global budgets, Q-002 branding direction, Windows 10 and wider real desktop/platform
  qualification remain open. Graphify repair is optional and does not block source/Markdown work.

# Decision log

Significant decisions have a full record in `docs/decisions/`. Status: **final** (do not reopen
without new evidence) or **provisional** (a qualification caveat, not automatic rejection of the
chosen design). Product requirements and their IDs: [PROJECT_SPEC.md](PROJECT_SPEC.md).
Approval and implementation verification are separate: ADRs 0002, 0003 and 0005 are accepted choices;
D3/D4/D6 retain their existing provisional qualification status until platform/behavior tests run.

| # | Decision | Reason | Alternatives | Consequences | Status |
|---|---|---|---|---|---|
| D1 | Native C++20/Win32 + Direct2D on Windows.UI.Composition (ADR 0001) | small footprint, fast start, zero idle CPU, real translucency | Electron, WinUI 3, Qt | more code for UI; full control of rendering | final |
| D2 | Blur via HostBackdropBrush on Windows 11 only; no undocumented accent API (ADR 0001) | documented API only | `SetWindowCompositionAttribute` | Windows 10 shows an opaque-ish tint | final |
| D3 | Minimum Windows 10 2004 (19041), x64 (ADR 0002) | composition interop + modern APIs | older builds | Windows 10 still untested | provisional (until tested) |
| D4 | Top bar = top AppBar (optional), Dock floating non-AppBar, hide for full-screen apps, hotkeys Ctrl+Alt+Space / Ctrl+Alt+D (ADR 0003) | maximized windows stay usable without wasting space | both AppBars; both overlays | needs Explorer-restart and full-screen handling | provisional (until desktop-tested) |
| D5 | Update trust: ECDSA P-256 signed manifest + per-asset SHA-256; Authenticode optional until a certificate exists (ADR 0004) | checksum alone does not prove publisher | checksum only; Authenticode only | owner must protect the private key; CI secret required | final |
| D6 | Custom per-user native installer, versioned folders + launcher, journaled update transaction with health check and rollback (ADR 0005) | shared code path for install/update/rollback; no elevation | WiX/MSI, NSIS, MSIX | no MSI enterprise features | provisional (until implemented and tested) |
| D7 | In-house test macros + CTest labels unit/integration/network (ADR 0006) | no downloads, offline reproducible | GoogleTest, Catch2, doctest | fewer conveniences | final |
| D8 | Static CRT (`/MT`) | users need no redistributable | dynamic CRT + VC++ redist | slightly larger binaries | final |
| D9 | Modules that only need another module's contract header do not link its library (status_audio, weather, search) | lets modules be built in parallel | full linking | contracts must stay header-only | final |
| D10 | Weather provider Open-Meteo (no key, CC BY 4.0), disabled by default | no secret in the app, privacy | key-based providers | attribution must be displayed | final |
| D11 | Wi-Fi SSID never displayed; "microphone in use" reported unsupported | location-privacy rules on Windows 11 24H2+; no documented cross-version API | registry consent-store heuristics | less information in the bar | final |
| D12 | Development tools (Graphify, Obsidian) are never product dependencies; their outputs stay out of Git | public repo hygiene | committing generated graphs | each developer regenerates locally | final |
| D13 | Root continuity files are canonical by responsibility; PROJECT_SPEC owns objectives/acceptance, AGENTS rules, TASKS backlog, DECISIONS choices, PROJECT_CONTEXT map, HANDOFF latest session; `docs/knowledge/` keeps evidence/index/log | one source of truth for any assistant | duplicating state in the vault | the Obsidian vault (`docs/`) does not contain the root files; D15 adds requirements traceability | final |
| D14 | Parallel agent work runs in batches of three with implement → independent review, private build dirs | usage limits and file-ownership safety | all modules at once | slower wall-clock, less lost work | provisional |
| D15 | PROJECT_SPEC is authoritative for intended requirements; existing source/tests establish observed behavior; stable FR/NFR IDs link tasks and evidence; independent verification on takeover | explicit owner request, 2026-10-18 | another duplicate architecture/status document | no automatic promotion of previous AI claims; significant protected-scope changes require owner approval | final, owner-requested |
| D16 | UI-owned settings model with serialized worker persistence and UI completions (ADR 0007) | resolve GAP-007 without UI disk blocking | synchronous UI writes or waiting on a worker | Update acceptance and durable completion are separate; revisions retain dirty-state correctness; configurable chord fields preserve ADR 0003 | accepted; service32unitcases passed /WX, UI qualification in progress |
| D17 | Status sessions/impl lifetime shared across worker callbacks; Start/Stop never joins while holding lifecycle locks; owned process WinRT runtime with explicit terminal cleanup (ADR 0008) | resolve GAP-003 concurrency and self-destruction hazards while cached WinRT factories need MTA lifetime | unsafe per-provider final MTA teardown; leaked cookie | provider COM scopes balance individually; FinalizeStatusRuntime after providers/graphics stop releases the owned cookie, refuses live scopes and rejects later starts | reviewed; fresh49unit+10readonly+1shutdown cases pass /WX |
| D18 | Publish an independently initialized clean Git checkout; retain the development checkout/history locally | owner explicitly requires private/public separation; historical author metadata is not anonymized | rewriting the existing history (forbidden), pushing private metadata | only reviewed publishable files and public GitHub noreply identity enter the official repository; no remote is added to the private history. Initial path-pattern hits were triaged as example URL/key-name false positives, not private paths | accepted publication safeguard; export audit required before push |

## Open questions and implementation discrepancies

These are not approved new requirements or decisions. Keep their IDs stable; disposition is tracked
in PROJECT_SPEC section H and TASKS. No decision here blocks creating the specification.

| ID | Question / discrepancy | Rationale and alternatives | Disposition / approval needed |
|---|---|---|---|
| Q-001 | Product performance budgets not established | Measure startup/CPU/memory/search first; retain search's <2 ms/2,000 apps engineering target, while Debug p95 <20 ms is an example and neither is a universal release budget | proposed measurement in T5g/T14; owner approves release thresholds later |
| Q-002 | Final logo concept/palette not selected | Original identity is required; work-order palette is an example, not proof of owner selection | T5i presents concrete assets/options for owner direction |
| GAP-003 | Status overlapping Start/Stop, callback-side destruction and deliberate process-lifetime MTA keep-alive | Existing source behavior requires lifetime/concurrency restrictions or a fix, and explicit resource-lifetime justification; neither silently changes NFR-005 | resolved in reviewed implementation; TEST_STATUS S-005, broader OS qualification remains |
| GAP-006 | Weather cache can remain displayed beyond 6 h while synchronous HTTP occupies its scheduler | Separate expiry scheduling from blocking fetch, or propose a bounded policy; do not redefine the existing 6 h requirement to fit the code | resolved by separate request worker and independent expiry scheduling;56cases pass, TEST_STATUS S-006 |
| GAP-007 | UI-thread-only settings work order includes disk I/O, contradicting responsive-UI rule | Preserve UI-owned state/notifications; record background persistence ordering and failure semantics before implementation | resolved by ADR0007 + serialized worker persistence; no UI disk exception |
| GAP-008 | Configurable hotkeys in ADR 0003 lack settings fields/editor | Add validated key-combination model/UI, or propose a documented scope change; enabled flag alone is insufficient | chord fields/validation and native editor implemented; final UI regression pending |

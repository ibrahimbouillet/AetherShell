# Tasks

Never mark a task done until its acceptance criteria were verified (command run, result observed).
Requirements and definitions: [PROJECT_SPEC.md](PROJECT_SPEC.md). Test evidence:
[TEST_STATUS](docs/knowledge/TEST_STATUS.md). Every task also follows NFR-004/NFR-008/NFR-010
(privacy, safe tests and continuity); listed links below identify its main product contribution.

## Current priority
**T16 — V2 (0.2.0-alpha.1), owner-requested, in progress.** Replace the duplicated taskbar layout
with a consented reversible visual replacement, protected Dock reservation and lower placement;
import available pinned shortcuts, correct Wi-Fi identity and restore visible glass. Requirement
links: FR-001, FR-002, FR-008, FR-015, FR-024; NFR-002–NFR-008, NFR-011. D19/ADR0009 records
the approved change to the original coexistence requirement. Explorer remains the Windows shell;
Windows Settings cannot become Aether's settings page through a supported API.

Focused module checks pass: applications 66 unit cases, Dock 19 unit + 2 isolated native cases,
graphics/Top Bar 14 unit + 5 isolated native cases, platform 15 unit cases. Controller/settings/app
integration, full Release build, delivery and real desktop restoration checks remain in progress.
Do not infer V2 publication or full desktop qualification from these scoped checks.

**Milestone 1 implementation is delivered as 0.1.0-alpha.1**: native Dock + Top Status Bar, Search,
Settings and per-user setup. Continue the manual/platform acceptance gaps below; an alpha does not
mark complete Version 1 acceptance.

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
- T2 → NFR-002, NFR-005, NFR-006, FR-017, FR-025: core/diagnostics/platform tests (`647701d`), historical scope.
- T3 → FR-001, NFR-002, NFR-007, NFR-011: graphics/UI base and glass probe (`158ffe1`), historical scope.
- T4 → FR-018, NFR-003, NFR-005: net, 55 cases after review (`af0e20f`), historical evidence.
- T5a → FR-018, FR-019: updates, historical figure 117 cases (reviewer count, not reproduced); the current executable has 101 cases (S-014); live release path pending.
- T5b → FR-003: applications, 72 cases, prior fresh lead recheck (`90fca92`); actual launch/UWP-frame qualification pending.
- T-DOC-1 → NFR-010: PROJECT_SPEC and canonical links independently reviewed; all 34 requirement IDs
  covered by task/evidence map, 22 document link/whitespace checks passed (TEST_STATUS S-004).
- Docs: ADRs 0001–0003, 0005, 0006; BUILDING, SECURITY, CONTRIBUTING, CHANGELOG, THIRD_PARTY_NOTICES;
  knowledge vault; continuity files (this set).
- Tooling: Obsidian 1.14.4 + CLI works while its app runs. Graphify 0.9.92 runs from an ordinary
  shell but not inside the Codex sandbox; no graph exists for this checkout (GAP-004). Neither is a
  product dependency.
- T-DOC-2 → NFR-010 (Claude, 2026-10-18): PROJECT_SPEC cross-checked against the owner's original
  brief. Added FR-025 (diagnostics), the Version 1 release gate, milestones and long-term direction;
  completed FR-004/010/015/018/019/020/022/023 and NFR-001/002 acceptance from the brief; recorded
  GAP-009–GAP-012. Added the session start prompt `docs/SESSION_START_PROMPT.md` (D20).

## Remaining (priority order)
| # | Task / requirements | Depends on | Acceptance criteria |
|---|---|---|---|
| T6 | Dock (`src/dock`) → FR-001–FR-004; NFR-002, NFR-007, NFR-011 | applications, settings, accessibility | Implemented; final 14 unit + 2 own-window cases pass /WX, including cross-review fixes/drag springs. Final fixture has 128px opaque containment and unchanged foreign guards. |
| T7 | Top Bar (`src/topbar`) → FR-001, FR-005–FR-011; NFR-002, NFR-007, NFR-011 | status, status_audio, weather, settings, accessibility | Implemented; 7 unit + 2 own-window/read-only-provider integration cases pass /WX. Dark/light captures inspected; independent review. Real AppBar/full-screen/Explorer qualification remains. |
| T8 | Search/settings/update UI → FR-013, FR-015, FR-019, FR-025; NFR-007, NFR-011 | search, settings, updates, weather | Search: five native cases pass, including finite transitions; Settings: five native cases pass, including delayed startup/reset/modifier/DPI/shutdown. Real IME remains unverified. |
| T9 | Install/launcher/updater/setup → FR-016, FR-020, FR-021; NFR-003, NFR-005 | updates, settings, branding | Install library: 10 unit + 7 integration cases pass /WX. Setup: five native cases pass, including maintenance relocation/viewport. Tests use temp roots/fakes/injected keys. Production alpha delivery observed separately. |
| T10 | App lifecycle → FR-001, FR-016, FR-017, FR-021; NFR-002, NFR-005 | T6–T9 | Native Release executable, three CLI cases and isolated lifecycle/health/channel/shutdown case pass. Health-before-discovery ordering and lock-before-cleanup reviewed; updater handoff reviewed without live signed execution. |
| T11 | Full build/desktop checks → FR-001, FR-003, FR-005–FR-021, FR-023; NFR-002–NFR-009 | T10 | full build and relevant tests; separate explicitly authorized desktop checks for DPI, Explorer restart, full-screen, AppBar, real launches/controls/startup; record gaps |
| T12 | Official public repo/push/CI → FR-023; NFR-004, NFR-009 | T11 (first runnable build) | public clean history pushed; Windows Release CI green and manual-install alpha published (S-012) |
| T13 | Release pipeline/signing → FR-018, FR-020, FR-021, FR-023; NFR-003, NFR-009 | T9, T12 | manual-install alpha setup/checksums published (S-012); authenticated update release still requires a real trusted manifest signature and live-cycle qualification |
| T14 | Release qualification → FR-001, FR-003, FR-009, FR-023; NFR-002, NFR-006, NFR-007, NFR-011 | T11 | Windows 10, monitor changes/removal (primary-monitor V1, not extra bars), accessibility/performance evidence; resolve Q-001 |
| T15 | End-user docs/screenshots → FR-022, FR-023; NFR-004, NFR-009 | T11 | Guides implemented; six individually inspected synthetic screenshots included. Keep CI/release receipts current; no complete Version 1 acceptance implied. |
| T16-gate | V2 gate status (S-017) → FR-001, FR-002, FR-008, FR-015, FR-024; NFR-007, NFR-008 | T16, T21 | Release /WX build and 480 unit cases pass. All 14 integration suites (95 cases) have passing runs of the current code, but no combined run has all 14 green. ShellApplication review done with no blocking defect; minor finding: the poll timer keeps waking after activation attempts are exhausted. Package privacy audit passed (S-018): the setup is built locally, unpublished, and its manifest is unsigned. Remaining before `0.2.0-alpha.1`: Windows 10, Explorer restart during replacement, an auto-hide-off desktop, a manifest signature from the owner's key if updates should be offered, and push/CI with the owner's agreement. |
| T18 | Owner decisions from the spec review → GAP-010, GAP-011, Q-001, Q-002 | owner | Repository description, performance targets and branding direction answered and recorded in DECISIONS.md. GAP-011 was answered on 2026-10-18 (D21 → T21). |
| T19 | `ARCHITECTURE.md` and `TESTING.md` → FR-023, NFR-009 (GAP-009) | — | Both exist as short public documents that link to PROJECT_CONTEXT/ADRs and BUILDING/TEST_STATUS instead of duplicating them; no private data. |
| T20 | Diagnostics export check → FR-025, NFR-004 | T10 | Export a diagnostics bundle from Settings with temporary data, inspect its contents for private data and the privacy statement, record the result in TEST_STATUS. |
| T21 | Taskbar recovery without Aether Shell running → FR-024, NFR-003, NFR-005, NFR-008 (D21, ADR 0009 amendment) | T16 source | **Steps 1–5 done (S-015, S-016).** The real-desktop check also fixed a V2 Dock reservation bug and made restoration always show the taskbar (D22). Open: Windows 10, Explorer restart during replacement, an auto-hide-off desktop. Blocks publication of `0.2.0-alpha.1`. (1) The baseline is persisted atomically in the per-user data directory before any hiding and deleted only after a successful restore. (2) `--restore-taskbar` with no running instance restores from it, never starts the bars, is idempotent, and without a baseline only shows the taskbar windows. (3) Setup repair performs the same restore. (4) Fake-backend and temporary-root tests cover baseline present, absent and corrupt, a running instance, and repair. (5) An owner-authorized desktop check kills the app and helper, runs `--restore-taskbar` and sees the taskbar return. Optional proposal, not approved: a Start-menu "Restore Windows taskbar" shortcut. |
| T17 | Owner's next usability corrections and interactive Top Bar → FR-001–FR-004, FR-007–FR-011, FR-015, FR-024; NFR-002, NFR-004, NFR-007, NFR-008, NFR-011 | Preserve T16 working tree/receipts, then reproduce the reported behavior | Requested, not implemented or verified as a set. Detailed acceptance and ambiguities below; continue with Claude from HANDOFF. |

## T17 — owner-requested next iteration (2026-10-18)

The owner requested these concrete usability changes for the continuation by Claude. These
are follow-up requirements, not claims that the current candidate already provides them. Preserve
T16's source and actual checks; use HANDOFF for its final build/delivery boundary. No new persistent
desktop surface is approved. Existing stable FR/NFR IDs remain; T17 expands their interaction work.

| Work item / requirement links | Observed source or owner report | Acceptance / next action |
|---|---|---|
| Hold/drag Dock icons left or right; reorder/swap and persist — FR-002, FR-004, FR-014 | Owner reports this does not work in use. Source has a six-DIP drag threshold and `MovePin` during pointer motion, limited to pinned source/target items; that does not disprove the report. | Reproduce the exact pointer workflow in a safe own-window fixture, inspect capture/region/hit testing during magnification, then fix. A held pinned icon must move to the intended left/right position without launching on release, and order must survive settings save/restart. Clarify behavior for unpinned running items rather than silently pinning them. |
| Right-click Dock icons — FR-004 | Owner clarified the target is Dock icons, not application pages. Source offers Open/New instance, Pin/Unpin, Move left/right, Dock settings, pin import and Windows Start, according to item kind. Exact additional desired commands are unspecified. | Verify these menus open reliably on the intended icons and preserve selection/launch identity. Clarify any additional command the owner expects before inventing it; no application-page or desktop-background takeover is requested. |
| Distinguish open Chrome profiles with their avatars — FR-002, FR-003, FR-004; NFR-004 | Owner explicitly clarified **Chrome profiles**, rather than generic duplicate Chrome glyphs. Profile-avatar-specific completeness is not established by the generic icon pipeline. | Assess supported local window/profile identity and icon sources. Show a real available avatar only when its association is reliable; otherwise explain the limitation and retain an honest fallback. Do not read private account databases/secrets or fetch account photos by assumption. Public fixtures use synthetic identities/art; never commit account names, photos or profile paths. Validate profile grouping/activation separately. |
| Accessible pinned File Explorer entry — FR-002, FR-003, FR-014 | Owner requests File Explorer directly accessible from the Dock. Automatic taskbar shortcut import alone cannot guarantee that entry exists. | Provide a supported discoverable/pinnable Explorer identity and an accessible Dock entry while preserving existing pins/order. Verify persistence and the actual user-triggered Explorer launch/activation separately; automated tests must use fakes and never start Explorer. |
| One visible bottom Dock in replacement mode — FR-001, FR-024 | This is also T16's requested outcome; the published 0.1 coexistence alpha and an unqualified V2 candidate are different states. | Verify the exact installed version and Desktop consent state. When mode activation succeeds, only Aether's bottom Dock should be visible, with applications clear of its reserved footprint; the separate Top Bar remains. Disable/quit/parent/helper failure must restore the Windows taskbar within the documented recovery limits. Real desktop evidence is separate from fake tests. |
| Keyboard/input-language control — FR-007, FR-011, FR-015 | Urgent correction adds `KeyboardRequested` from pointer/keyboard/UIA with Win+Space action text; app wires the explicit user route. Fresh execution is pending. | Verify synthetic signal delivery and real user-triggered language switching separately, with keyboard/UIA access, honest failure and no silent changes. No global key input in automation. |
| Internet/network management — FR-008, FR-011, FR-015 | Urgent correction adds `NetworkRequested`; app wires Windows network settings on a worker. Fresh execution is pending. | Verify synthetic requests first, then the actual user-triggered settings route separately. This is access to management, not an automatic connect/disconnect action. Never expose an SSID or invent success; no real network changes in automation. |
| Microphone/voice interaction — FR-009, FR-011; NFR-004 | Current microphone click toggles the default endpoint's mute. Output click toggles mute and its wheel adjusts volume. There is no recording/dictation/voice-session implementation; microphone-in-use detection remains unsupported under D11. | Preserve working mute/volume controls and clarify what the owner's requested "speaking" interaction means before choosing recording, dictation or another voice action. Never interpret it as always-on recording or start capture/network transport without explicit user action. Any approved capture needs visible state, stop/cancel, failure handling and privacy review. |
| Weather interaction — FR-010, FR-011, FR-015 | Urgent correction adds `WeatherRequested` from the visible weather item; app routes it to its existing Weather settings page. Fresh execution and live forecast qualification remain open. | Verify pointer/keyboard/UIA requests, retain opt-in configuration/attribution/cache limits, and qualify real fetching separately. No invented readings or new persistent weather surface. |

The handoff must retain the distinction between existing source behavior, an owner's reported
failure and a verified fix. T17 is not complete as a whole; urgent source corrections above need their
own executed receipts, and any remaining ambiguity stays explicit.

## Dependencies and unresolved acceptance gaps
- Complete Release /WX and 426 unit cases pass. All 91 integration cases have scoped passing runs;
  aggregate interactive runs also refused foreign-overlapped captures or lost Search activation.
  See S-011 for per-run evidence; no uniformly green aggregate desktop run is claimed.
- D18 keeps the original development checkout/history local. The official public repository now
  exists; audited independent-source push, successful Windows CI and the first public manual alpha
  are recorded in S-012.
- Signing remains owner-controlled. Unsigned manifests stay local; manual alpha assets are only
  audited setup/checksums. Never relax verification for a missing signature.
- Q-001 global budgets, Q-002 branding direction, Windows 10 and wider real desktop/platform
  qualification remain open. Graphify repair is optional and does not block source/Markdown work.

# Aether Shell — project specification

This is the authoritative reference for **what the product must achieve**. The repository and
executed checks establish **what it currently does**; implementation does not redefine requirements.
Owner/author: Ibrahim Bouillet. License: MIT. Baseline: 2026-10-18, existing project decisions and
module work orders, plus the owner's request for one traceable specification. No product is released.

## A. Identity and mission

Aether Shell is a modular native Windows desktop interface for people who want a floating
application Dock and a separate Top Status Bar while continuing to use Windows Explorer and the
Windows taskbar. It makes applications and real system information accessible through a coherent,
lightweight glass interface. The intended audience is desktop users; no narrower user research or
enterprise deployment commitment is established.

Success means a real native application that starts both bars, launches or activates applications,
reports real status, supports keyboard and assistive technology, shuts down cleanly, and can be
installed, repaired, updated and rolled back without administrator rights or loss of a working
installation. A rendering probe, service library or passing unit suite alone is not that outcome.

## B. Vision and scope

The confirmed Version 1 direction includes application discovery, search, persistent customization,
optional weather, accessible supporting windows, original branding, per-user installation and
authenticated GitHub Releases updates with transactional recovery. Milestone 1 is the first runnable
Dock + Top Status Bar application; it is an intermediate milestone, not a reduction of that direction.

Version 1 has exactly **two persistent desktop surfaces**, the bottom Dock and Top Status Bar.
Transient search, settings, update dialogs, menus and tooltips are supporting UI permitted by ADR 0001
and the module work orders. They do not authorize a third persistent desktop surface. The Dock holds
applications and the search entry; system status belongs to the Top Status Bar.

Version 1 targets the primary monitor, Windows 10 2004+ and Windows 11 x64. Qualification on Windows 10,
monitor/DPI changes and Explorer restart remains required. Per-monitor expansion and ARM64 are future
possibilities, not committed Version 1 deliverables. Enterprise MSI deployment is outside Version 1
(ADR 0005). Exact branding palette and release performance budgets remain unresolved. There is no
approved replacement-shell, extra-panel or cross-platform roadmap.

## C. Functional requirements

IDs are permanent; retire an ID explicitly rather than reusing it. P0 = integrity/release gate;
P1 = required Version 1 capability; P2 = optional user-enabled capability. These priorities organize
the existing scope; the next implementation order remains in [TASKS.md](TASKS.md).
Module work orders supply detailed algorithms, bounds and fixtures beneath these requirements.
Dependencies describe functional integration, not a strict build-order graph; use TASKS and the
CMake module registry for implementation/build ordering.

| ID | Description and rationale | Priority | Acceptance criteria | Verification method | Dependencies |
|---|---|---|---|---|---|
| FR-001 | Two separate desktop bars alongside Explorer, keeping the standard shell usable. | P0 | Dock at bottom and Top Status Bar at top of primary work area; no taskbar replacement; top reservation defaults on, Dock reservation off; both hide for foreground full-screen apps and recover after Explorer/display/DPI changes. | Own-window model/capture tests; separately authorized desktop checks of reservation, full-screen and restart, per ADR 0003. | FR-017; NFR-003, NFR-006 |
| FR-002 | Dock item model and persistent pinning so users control application access. | P1 | Search entry, ordered pins, then optional unpinned running apps; identities deduplicated; running indicators; pin/unpin/reorder persist; narrow width uses documented shrink/overflow behavior. | Model tests including duplicates, ordering and overflow; temporary-settings integration. | FR-003, FR-014 |
| FR-003 | Discover applications, obtain real icons, launch and activate existing windows. | P1 | Catalog covers supported desktop/packaged apps; launch uses validated application identities; activation/cycling tracks running windows; errors return useful failures; icon fallback is honest. | Catalog/icon/window integration, launch/activation fakes, later explicit manual launch and UWP-frame qualification. | NFR-001, NFR-002 |
| FR-004 | Usable Dock input and animation with accurate hit targets. | P1 | Click/keyboard/context menu and drag reorder follow dock work order; magnification never overlaps items; hit-testing follows drawn geometry; tooltips/focus indicators work; reduced motion removes bounce/magnification animation. | Layout/model tests; fake-launcher window captures at rest/hover; keyboard/UIA checks. | FR-002; NFR-007, NFR-011 |
| FR-005 | Real localized clock/date, including time and zone changes. | P1 | Default minute ticks and optional seconds align to boundaries; immediate refresh on time/locale/resume notifications; system/12/24-hour and date formats retain locale conventions; unavailable time is never fabricated. | Fixed en-US/fr-FR/de-DE format tests; real-clock boundary, refresh and stop integration. | NFR-002, NFR-005 |
| FR-006 | Honest battery/power information across available hardware. | P1 | Real presence/percent/source/charge/saver/time-left when known; absent is unavailable, unknown percent/time stays absent; PowerManager plus Win32 fallback; repeated identical readings suppressed. | Pure raw-reading mapping and real read-only/fallback integration; absent/unknown fixtures. | NFR-005 |
| FR-007 | Foreground application's input language with explicit limitations. | P1 | Foreground thread HKL supplies localized label/layout; foreground hook plus default 1 s comparison; fallback marked when no foreground window; console/elevation/IME limitations documented. | Injected HKL changes/refresh/restart tests and read-only installed-layout integration. | NFR-002, NFR-005 |
| FR-008 | Network status without exposing location-derived identifiers. | P1 | Real connectivity/kind and reported wireless bars; unknown stays unknown; no profile name/SSID read or shown; event bursts debounced on background thread; no network polling. | Pure mapping; real read-only subscription and stop integration; source inspection for identifier access. | NFR-004, NFR-005 |
| FR-009 | Default audio/microphone status and explicit user controls. | P1 | Real endpoint availability/volume/mute; controls report failures; endpoint changes handled; microphone-in-use reported unsupported; no state changes without user input. | Mapping, publisher and worker tests; read-only endpoint/stop integration; explicit later manual controls check. | NFR-003, NFR-005 |
| FR-010 | Optional weather for a location explicitly chosen by the user. | P2 | Disabled by default; no automatic forecast requests while disabled/unconfigured (explicit location-search geocoding is separate); Open-Meteo attribution; valid units/WMO states; fresh/cached/offline/failure distinguishable; never display cached data older than 6 h; cancellation/config changes cannot publish stale-location results. | Fake HTTP/clock/cache/geocoding tests; later consent-based end-to-end UI/network check. | FR-014, FR-015; NFR-004 |
| FR-011 | Top Status Bar renders real providers clearly and remains usable on narrow screens. | P1 | Clock/date, battery, language, network, volume, microphone and optional weather reflect snapshots/visibility settings; no invented zeroes; logo menu and tooltips; absent battery handled; low-priority items collapse predictably. | Model/layout fixtures, dark/light own-window captures, read-only real-provider integration. | FR-005–FR-010, FR-014, FR-022; NFR-007 |
| FR-012 | Deterministic application-only search service. | P1 | Case/accent normalization; exact > name prefix > word prefix > acronym > substring > fuzzy; stable ties/original-coordinate highlights; empty query gives no results; background cancellable rebuild; typed text is never executed. | Ranking/Unicode/highlight/concurrent-rebuild tests and measured synthetic-index benchmark. Release latency budget unresolved (Q-001). | FR-003; NFR-002 |
| FR-013 | Keyboard-accessible search window from Dock or hotkey. | P1 | Real EDIT/IME input, current-index results on edit, arrows/Enter/Escape and empty/error states; launch only selected results; Escape clears then dismisses; deactivation closes. | Fake catalog/launcher integration, keyboard/IME/UIA checks and own-window capture. | FR-012, FR-003; NFR-007 |
| FR-014 | Durable versioned settings that survive invalid files and binary rollback. | P1 | Schema-v1 JSON validates documented model ranges; defaults on missing file without writing; atomic saves, corruption recovery and bounded backups; newer schemas protected before overwrite; write failures reported and pending state retried. | Temporary-file round trips, wrong types/ranges, migration/newer-schema/corruption/failed-write tests. | NFR-005 |
| FR-015 | Settings UI applies customization through the settings service. | P1 | Dock, Top Bar/weather, updates, general and about controls work; standard Win32 controls; changes persist; errors visible; about shows version, author and MIT; diagnostics export follows privacy rules. | Temporary-service control-message integration, keyboard/DPI/high-contrast checks and own-window captures. | FR-014, FR-018, FR-022; NFR-004, NFR-007 |
| FR-016 | Consent-only startup and safe global keyboard entry points. | P0 | Start with Windows off until consent; per-user launcher registration respects Task Manager's disabled state; hotkeys configurable/disable-able, default Ctrl+Alt+Space and Ctrl+Alt+D; conflicts visible and nonfatal. | Injected test registry and hotkey failure fixtures; later explicit consent-based startup verification. | FR-014, FR-017, FR-020; NFR-003 |
| FR-017 | Native application lifecycle integrates the modules reliably. | P1 | One instance, second-launch forwarding, both bars wired, provider results marshalled to UI, ordered shutdown/unregistration and update health signal; no dangling callbacks or hidden autostart. | Full app start/stop/second-instance integration; shutdown/failure fixtures and desktop observation. | FR-002, FR-004, FR-011, FR-013–FR-016, FR-019, FR-021 |
| FR-018 | Updates authenticated against the official release source. | P0 | Only configured official GitHub Releases; newer compatible SemVer/channel; signed manifest verified before fields are trusted; download size/SHA-256 checked; redirects bounded; never execute unverified content; no token needed. | Malformed/replay/channel/signature/hash/size/redirect/cancellation fixtures; later live official-release qualification (ADR 0004). | NFR-003, NFR-004 |
| FR-019 | User-facing update flow with transparent errors and safe release notes. | P1 | Check, download/install, progress, postpone/skip/disable notifications; verification state visible; plain-text sanitized notes only; allowed release-details links; installation disabled while busy. | Coordinator fakes and temporary UI integration; malicious-note/link fixtures. | FR-018, FR-021, FR-014 |
| FR-020 | Per-user native install, stable launcher, repair and uninstall. | P1 | Complete versioned payload verified on extraction; native wizard and silent mode; no elevation; Start Menu/uninstall registration; repair preserves settings; uninstall keeps user data unless removal requested; errors/exit codes documented. | Temp install/data roots, fake/injected registry/process operations; install/repair/uninstall and payload-containment tests. | FR-014, FR-016, FR-018, FR-022; NFR-003 |
| FR-021 | Transactional update and recovery retain a working version. | P0 | Updater independently re-verifies package; journal follows ADR 0005; stage before switch; health check decides commit/rollback; interruption recoverable at each state; active/previous versions never removed prematurely; binary rollback preserves newer user data. | Injected failures at every state, temporary-root recovery/rollback, health success/timeout, malicious payload/path fixtures. | FR-018; shared install library of FR-020 |
| FR-022 | Original coherent identity and native visual assets. | P1 | Recognizable original mark at small sizes, consistent icon/installer art and light/dark variants; reproducible one-source asset generation; no copied third-party artwork or redistributed font binaries (installed system-font rasterization/references permitted); visual inspection recorded. | Asset inventory, dimensions/ICO sizes/regeneration, small-size light/dark visual review. Palette/concept selection remains Q-002. | NFR-009, NFR-011 |
| FR-023 | Reproducible public delivery with honest end-user documentation. | P1 | Official public repository and green Windows CI; qualified native setup/manifest/checksums; approved release publication; README/screenshots and install/update/rollback/limitations docs describe tested behavior. | Local build/package/signature checks, actual CI/release receipts, privacy/license and documentation review. | FR-017–FR-022; NFR-004, NFR-009 |

## D. Non-functional requirements

| ID | Requirement and rationale | Priority | Acceptance / verification | Dependencies |
|---|---|---|---|---|
| NFR-001 | Native modular architecture without a heavyweight runtime. | P0 | C++20, Win32, Direct2D/DirectWrite and Windows.UI.Composition; no Electron; static CRT; module registry defines dependencies; no new third-party libraries without a recorded decision. Inspect CMake and dependency closure. | D1, D8, D9 |
| NFR-002 | Responsive UI and event-driven idle behavior conserve resources. | P0 | Network/discovery/icon extraction/disk on workers with dispatcher delivery; UI requests no idle frames; animation uses elapsed time. Instrument/inspect affected paths, measure startup/CPU/memory/search on stated configuration. No invented global numeric budget. | Q-001 |
| NFR-003 | Preserve shell integrity and limit system changes. | P0 | No Explorer replacement, Winlogon Shell edit, taskbar/security disabling, elevation or hidden autoruns; documented Windows APIs; unsupported behavior explicit. Code/security review plus isolated integration. | FR-016, FR-018 |
| NFR-004 | Public repository privacy and runtime data minimization. | P0 | No credentials, private keys or their locations, personal addresses/accounts/paths, machine names or unrelated content in public material; owner attribution allowed. No SSID or private request/exception details in logs; captures only own test windows/backdrop. Review source/docs/fixtures/diffs and bounded log contents; do not claim a complete privacy audit without one. | AGENTS public-repository rule |
| NFR-005 | Reliable failure and lifetime handling. | P0 | Public module operations use Result<T>; exceptions do not escape module/thread boundaries; RAII; unavailable/error explicit; Stop quiesces listeners and releases provider resources. Exercise failures, cancellation, restart and listener lifetime; disclose unsupported concurrency/ownership cases. | NFR-001 |
| NFR-006 | Qualified platform/DPI behavior rather than assumed compatibility. | P1 | Target Windows 10 2004+/11 x64, asInvoker and Per-Monitor-V2; relayout on DPI/display change; Windows 10 tint fallback; claim only tested configurations. Record OS/DPI/monitor qualification. | D2–D4 |
| NFR-007 | Accessibility, keyboard, reduced motion and high contrast are required. | P0 | UIA names/control types/bounds/actions/focus and safe disconnect for custom surfaces; standard controls for supporting dialogs; keyboard completion and visible focus; reduced motion/high contrast verified. UIA and visual/manual review. | accessibility module |
| NFR-008 | Tests must protect the real desktop while providing relevant evidence. | P0 | Unit/integration/network labels correct; integrations capture only their test windows over private backdrop; temporary config/install roots and injected test registry; no real work-area/audio/Run/Uninstall changes or application launches in automation. Inspect tests before executing them. | D7 |
| NFR-009 | Reproducible quality, attribution and documentation. | P1 | Focused /W4 build warning-free, /WX Release CI, appropriate executed tests and independent review; MIT/SPDX/copyright in sources; English public docs/comments; no generated/build/local-tool state in Git; module docs and evidence updated. | FR-023; D7, D8, D12 |
| NFR-010 | Continuity and preservation across assistants. | P0 | Read handoff/spec/tasks/decisions, inspect Git and verify relevant prior work; preserve uncommitted/unrelated work, no unapproved destructive/publication actions; small coherent changes; no silent architectural reversal; update canonical handoff and requirement evidence. | D13, D15 |
| NFR-011 | Coherent native visual/input quality. | P1 | Floating rounded glass with honest fallback, consistent tokens/original glyphs, light/dark and contrast support, interruptible animation and hit targets matching geometry; clipping/alignment/readability checked in own-window renders at relevant DPI. No claim of visual approval without inspection. | FR-004, FR-011, FR-022 |

## E. Technical architecture and authority

The verified build structure is the CMake module registry in `cmake/AetherModules.cmake`; the service
contracts are `src/status/StatusTypes.h`, `src/applications/ApplicationTypes.h` and
`src/settings/Settings.h`. The existing base is `core → diagnostics → platform/windows → graphics → ui`.
Workers deliver to `IUiDispatcher`; windows/rendering belong to the STA UI thread. UTF-8 strings and
UTF-16 Windows strings use core conversion helpers. Module operations use `aether::Result<T>` and RAII.

Implemented consumers are Dock, Top Bar, search/settings UI and the `src/app` lifecycle. The install
library, launcher, updater and native installer are separate implemented targets, sharing trust and
transaction logic. Scoped test evidence and remaining product qualification are recorded below;
declaring a target alone is not implementation or acceptance evidence. Data roots are
`%LOCALAPPDATA%\AetherShell\{config,logs,cache,updates}`; installation layout is ADR 0005. Weather must
not depend on search; update verification must not depend on rendering. Detailed architecture and
the current file map remain in [PROJECT_CONTEXT.md](PROJECT_CONTEXT.md).

| Canonical file | Owns | Must not become |
|---|---|---|
| `PROJECT_SPEC.md` | Mission, scope, requirements, acceptance and traceability IDs | A rewritten reflection of whatever code currently does |
| `AGENTS.md` | Shared working rules, public privacy contract and discovery/build instructions | A competing product roadmap |
| `TASKS.md` | Ordered implementation tasks, dependencies and requirement links | Evidence that an unrun test passed |
| `DECISIONS.md`, `docs/decisions/` | Decisions, rationale, approval state and significant ADRs | An implicit approval of a proposal |
| `PROJECT_CONTEXT.md` | Repository map, architecture and limitations | A second requirements catalog |
| `HANDOFF.md` | Latest session's changes, exact checks, Git state and next steps | Permanent acceptance criteria |
| `docs/knowledge/TEST_STATUS.md` | Executed commands/results and qualification gaps | Product requirements or unverified claims |
| `docs/specs/`, `docs/modules/` | Detailed work orders and implementation references | Authority to override this spec or claim absent modules exist |

The Obsidian vault is the same `docs/` directory, not a second copy to synchronize. Root files sit
outside that vault and use relative Markdown links. Graphify data and its export are optional,
generated, local and uncommitted. Inferred graph edges are hints, never proof of behavior.

## F. Invariants and change control

- Preserve working behavior and all unrelated/pre-existing local work; avoid taste-driven rewrites.
- Preserve defined contracts/data compatibility; newer settings must survive a binary rollback.
- Never publish secrets/private material; keep the privacy rule in `AGENTS.md` authoritative even
  when a short reminder appears in a handoff or work order.
- Never execute unverified downloads, destroy the active installation prematurely, or make hidden
  system/startup changes. Keep exactly two persistent desktop surfaces in Version 1.
- Do not infer permission to publish a release/change repository settings from permission to code.
  Observe the current user's instructions for commits/pushes/destructive actions.
- A fundamental objective/protected-requirement change requires owner approval and a decision
  record; a source/spec mismatch is a defect or an explicit proposal, not an automatic spec edit.
- Update a requirement's acceptance, dependent tasks and evidence together. IDs remain stable.

## G. Definition of done and traceability

**Not started:** no implementation beyond declarations/contracts. **Implemented:** code/configuration
exists, possibly partial. **Tested:** named relevant checks actually passed on a stated revision and
configuration. **Verified:** every applicable acceptance criterion has evidence, independent review
issues are resolved/documented, and required manual/platform/security checks are complete.
**Blocked:** a named dependency or unresolved issue prevents the specified work; difficulty or a quota
interruption alone does not prove a technical blocker. **Unverified** qualifies any claim lacking
adequate evidence. Historical recorded checks retain their stated scope; label them historical and
not independently reverified this session rather than invalidating them solely because they are old.

Do not promote a whole requirement when only its pure helper or a fake passed. Record source/test
paths, command, configuration, result/count, revision plus dirty-tree qualification, date, review and
remaining manual checks in TEST_STATUS; link from task and requirement below. Re-run when affected
code/dependencies change or evidence is insufficient; do not repeat unrelated expensive checks.

Continuation traceability (2026-10-18): **H** means historical recorded evidence in TEST_STATUS, not a new
execution or independent re-verification. **S** means this session's scoped evidence there. No entire
Version 1 requirement is declared verified by this table. Git/current-session details live in HANDOFF.
The complete Release /WX build and 426 unit cases in 17 executables pass. All 91 integration cases
in 14 executables have passing scoped final-binary runs; there is **no uniformly green combined
31/31 run**. The settled receipts and negative evidence are TEST_STATUS S-011. Aggregate GUI runs
encountered foreign full-screen/topmost overlap or focus-dependent
fixture closure; capture guards refused unsafe images, and isolated passing runs are recorded in
TEST_STATUS. Product source stayed fixed between those scoped runs; the final Dock changes were
fixture-only guard-band/z-order corrections. Top Bar idle assertions require bounded render
quiescence. The alpha was installed through production setup with startup absent, separately from
automated tests. The official repository exists without pushed source or a CI receipt. These
observations do not establish whole-product or Version 1 acceptance.

| Requirement | Implementation / verification entry points | Tasks | Current scope/evidence |
|---|---|---|---|
| FR-001 | `src/dock`, `src/topbar`, `src/app`; [surface evidence S-008 and lifecycle S-009](docs/knowledge/TEST_STATUS.md) | T3, T6, T7, T10, T11, T14 | Both bars and app wiring implemented; the isolated app lifecycle case and scoped own-window checks pass. Real reservation/full-screen/restart and physical DPI qualification remain pending. |
| FR-002, FR-004 | [Dock reference](docs/modules/dock.md); `tests/{unit,integration}/dock`; TEST_STATUS S-008 | T6, T11, T14 | Implemented; current Release /WX 14 unit + 2 native cases pass in scoped runs, including reviewed UIA/drag/AppBar fixes and guarded captures. Unsafe aggregate captures were refused; real activation/focus/desktop behavior remain unqualified. |
| FR-003 | `src/applications`; `tests/{unit,integration}/applications`; [applications reference](docs/modules/applications.md) | T5b, T11, T14 | Implemented; current Release 57 unit + 15 read-only integration cases pass (72 total). Actual external launches/UWP-frame qualification remain unverified. |
| FR-005–FR-008 | [status reference](docs/modules/status.md); `tests/{unit,integration}/status`; TEST_STATUS S-005 | T5c, T7, T11 | Implemented; fresh /WX 49 unit + 10 read-only integration + 1 isolated runtime-shutdown cases pass; independent review. GAP-003 resolved in scoped implementation; wider OS/hardware qualification pending. |
| FR-009 | [audio reference](docs/modules/status_audio.md); `tests/{unit,integration}/status_audio`; TEST_STATUS S-002 | T5d, T7, T11 | Implemented; scoped /WX 23 unit + 5 read-only integration cases and current dependency regression pass. Real controls/notification echo/device-service failures remain unqualified. |
| FR-010 | [weather reference](docs/modules/weather.md); `tests/unit/weather`; TEST_STATUS S-006 | T5e, T7, T8, T11 | Implemented; /WX 56 fake-based cases pass, including expiry while HTTP remains blocked. GAP-006 resolved; live forecast/geocoding and consent/attribution workflow remain unqualified. |
| FR-011 | [Top Bar reference](docs/modules/topbar.md); `tests/{unit,integration}/topbar`; TEST_STATUS S-008 | T7, T11, T14 | Implemented; current Release /WX 7 unit + 2 integration cases pass, including update-badge identity/clearing/idle assertions. Dark/light captures inspected and real providers sampled read-only; real AppBar/full-screen/restart pending. |
| FR-012 | [search reference](docs/modules/search.md); `tests/unit/search`; TEST_STATUS S-007 | T5g, T11, T14 | Implemented/reviewed; current Release /WX 7 cases pass. Synthetic p50/p95: 5000 apps 1.409/1.962 ms; 2000 apps 0.512/0.601 ms. Cancellation/closed-dispatcher checks pass; global release budgets remain Q-001. |
| FR-013 | [Search UI reference](docs/modules/search_ui.md); `tests/integration/search_ui`; TEST_STATUS S-008 | T8, T10, T11, T14 | Implemented; current Release /WX 5 integration cases pass with fake launches, guarded captures, finite GPU transitions and native EDIT name/Value/focusable checks from MTA. Actual IME/screen-reader flow remains unverified. |
| FR-014 | [settings reference](docs/modules/settings.md); `tests/unit/settings`; TEST_STATUS S-007 | T5f, T11 | Implemented; current Release /WX 33 unit cases pass for async persistence, validation, preservation/recovery, failures and shutdown quiescence. GAP-007 resolved through ADR 0007; isolated app shutdown also passes. |
| FR-015 | [settings native editor](docs/modules/settings.md); `tests/integration/settings_ui`; TEST_STATUS S-008 | T8, T11, T14 | Implemented; final scoped /WX 5 native cases pass after consent/model-merge/viewport corrections. Temporary files/injected callbacks only; physical DPI, screen-reader and live update/weather actions remain unqualified. |
| FR-016 | Settings chord/startup model and native editor; `src/app`, `src/install`; TEST_STATUS S-007–S-009 | T5f, T8, T9, T10, T11 | Configurable chords, consent-only startup and registration wiring implemented; model/fake-consent and isolated app gates pass. Authorized alpha installation observed startup absent. Real startup consent/Task Manager and shortcut conflicts remain unqualified. |
| FR-017 | [app reference](docs/modules/app.md); `src/app/ShellApplication.*`; `tests/integration/app`; TEST_STATUS S-009 | T2, T10, T11 | Native target/wiring implemented; current Release isolated lifecycle/health/channel/shutdown case passes with temporary data and private options. Health-before-discovery and cleanup-before-unlock reviewed; wider desktop/update handoff qualification remains. |
| FR-018 | `src/updates`, `src/platform/net`, `scripts/release`; [updates reference](docs/modules/updates.md); TEST_STATUS S-009 | T4, T5a, T11, T13 | Implemented; current Release net 24 unit + 28 read-only integration and updates 101 unit cases pass; 32 Python release/package cases pass. Three live-network cases excluded. Invalid signatures fail before shutdown; live owner-signed official update remains unverified. |
| FR-019 | Updates coordinator; [settings editor](docs/modules/settings.md); `src/app`, `src/topbar`; TEST_STATUS S-008–S-009 | T5a, T7, T8, T10, T11 | Coordinator, native controls and existing Top Bar update signal/menu/app wiring implemented; current editor/indicator regression passes. Launch observation has a timer fallback for failed UI completion posting. Real signed download/install flow remains unqualified. |
| FR-020, FR-021 | [install reference](docs/modules/install.md), [setup reference](docs/modules/installer.md); `launcher/`, `updater/`; TEST_STATUS S-009 | T9, T10, T11, T13 | Implemented; Release /WX 10 unit + 7 install integration and 5 setup cases pass, including maintenance/viewport and isolated app health checks. Authorized production setup installed the alpha without startup. Real signed update/recovery remains unqualified. |
| FR-022 | `resources/branding`; [branding reference](docs/BRANDING.md); TEST_STATUS S-007 | T5i, T15 | Original assets implemented as an alpha proposal; 12 raster sizes/8 ICO entries checked, contact sheet/banner inspected twice. Final owner concept/palette selection remains Q-002. |
| FR-023 | CMake/build/package scripts, `.github/workflows/ci.yml`; TEST_STATUS S-009–S-010 | T1, T9–T15 | Native delivery targets and packaging/CI configuration exist; Release /WX build and 426 unit cases pass, with 91 integration cases passing across scoped runs. No uniformly green combined matrix. Official repository created but not pushed; final payload/export privacy, actual CI and approved release receipts remain pending. |
| NFR-001 | CMake module registry, native targets and ADRs; TEST_STATUS S-009 | T1–T5, T10, T11 | Native implementations and complete Release /WX build with 426 unit cases pass; 91 integration cases have passing scoped runs. Target presence and this environment-limited aggregate evidence do not establish complete acceptance. |
| NFR-002 | WorkerPool/dispatcher/FrameScheduler; module threading/idle/search checks; TEST_STATUS S-005–S-008 | T2, T3, T5–T11, T14 | Current worker, quiescence/idle-frame and synthetic Release search checks pass in their stated scopes. Whole-product startup/CPU/memory/latency and owner-approved Q-001 budgets remain unqualified. |
| NFR-003, NFR-004 | App/install trust and mutation policy; privacy checks; TEST_STATUS S-009–S-010 | T4, T5, T9–T15 | Temporary/fake/read-only checks and 457-file candidate review performed. Old Debug app/setup contain private paths and are excluded; filename-only PDB/WinRT source-path mitigations pass preliminary scans of four Release binaries. Final package/export scan pending; no complete security certification claimed. |
| NFR-005 | Result/RAII, status/settings/transport and install failure/lifetime tests; TEST_STATUS S-005–S-009 | T2, T4, T5, T9–T11 | GAP-003/GAP-006/GAP-007 fixes, shutdown quiescence, scoped install failures and isolated app health/channel/shutdown have tests/review. Terminal runtime/ownership limits documented; real signed production update/recovery remains unqualified. |
| NFR-006 | Manifest/platform DPI, scrollable native editors, ADR 0002; TEST_STATUS S-007–S-009 | T2, T11, T14 | Scoped Windows 11 x64 and synthetic DPI/viewport checks only. Windows 10, physical monitor/DPI transitions and real desktop behavior remain unqualified. |
| NFR-007 | [accessibility reference](docs/modules/accessibility.md), Dock/Top Bar/Search/settings editors; TEST_STATUS S-007–S-008 | T3, T5h, T6–T8, T14 | /WX 3 unit + 3 own-window UIA cases pass; separate-MTA consumer checks and keyboard/viewport regressions added/tested in scope. Successful foreground focus, actual screen-reader/IME/high-contrast product workflows remain unqualified. |
| NFR-008 | Test labels; temporary/fake/read-only/guarded-capture fixtures; TEST_STATUS S-005–S-009 | T1–T11, T14 | Reviewed temporary/fake/read-only fixtures and guarded captures; foreign overlap safely refused. Glass 2/Dock 2 and other native gates have passing scoped runs, with aggregate GUI environment failures retained. Automated tests do not alter real work area/audio/startup or launch external applications; authorized alpha delivery is separate. |
| NFR-009 | Build flags, SPDX/license, module references, packaging/CI and public docs; TEST_STATUS S-007–S-010 | T0–T5, T11–T15 | Complete Release /W4 /WX build, 426 unit cases, 91 integration cases across scoped runs and independent reviews recorded. No uniformly green combined matrix. Final export/license/privacy review, public CI and approved delivery receipts remain pending. |
| NFR-010 | PROJECT_SPEC, AGENTS, TASKS, DECISIONS, HANDOFF and knowledge evidence | T-DOC-1, every task | Continuation evidence/gap dispositions maintained; stable requirement definitions preserved. D18 keeps original history local; final export/handoff audit remains required. |
| NFR-011 | Graphics/UI, original branding and Dock/Top Bar/Search captures; TEST_STATUS S-007–S-008 | T3, T5i, T6–T8, T14, T15 | Scoped light/dark synthetic captures inspected, geometry/idle checks and current GPU transition/update-badge cases pass. Physical DPI, high contrast, owner branding selection and complete product visual qualification remain pending. |

All requirements have backlog coverage above. T0–T4 are prerequisite/maintenance work that also
support cross-cutting NFRs; they do not demonstrate the product is runnable. A requirement without a
task/evidence path must be recorded as a gap before it is called complete.


## H. Decisions, discrepancies and open questions

Decision IDs D1–D18 are retained in [DECISIONS.md](DECISIONS.md); ADRs 0001–0008 contain the significant
architecture rationale and alternatives. D15 records this specification/workflow and D18 the clean
public-history safeguard. A work-order example or future idea is not owner approval. Preserve
proposed/provisional/accepted distinctions.

| ID | Finding / current disposition | Owner / next action |
|---|---|---|
| Q-001 | No approved global startup, memory, idle-CPU or animation/search release budget. Preserve the existing search module engineering target <2 ms/2,000 apps; the suggested Debug p95 <20 ms assertion is an engineering example. Neither defines a universal release budget. | Release search cases pass: 2000-app p95 0.601 ms, 5000-app p95 1.962 ms. Owner still approves global budgets before whole-product performance qualification. |
| Q-002 | Original two-ribbon SVG/PNG/ICO/installer identity exists and has been visually inspected as an alpha proposal; no final owner-selected concept/palette is recorded. | T5i/T15 retain the concrete assets for owner direction; do not infer final branding approval from generation or scoped captures. |
| GAP-001 | ADR 0002/0003/0005 are accepted design choices; D3/D4/D6 retain provisional platform/product qualification. Scoped implementation tests do not settle those wider checks. | Preserve design and approval/verification distinction; T11/T14 record remaining desktop/platform qualification. Owner approval is needed for a design reversal, not routine implementation. |
| GAP-002 | Source-presence discrepancy resolved: settings/search/accessibility/Dock/Top Bar/app/install/launcher/updater/setup exist and the complete Release /WX build passes. Official repository created; no source push or actual GitHub CI run yet. | Use TEST_STATUS S-005–S-010 for current scope; T11/T12 retain the scoped-versus-aggregate GUI distinction and finish public source/CI receipts. |
| GAP-003 | Resolved in scoped implementation: shared sessions/lifetime, Stop outside lifecycle locks and explicit terminal owned-runtime cleanup replace the unsafe overlap/self-destruction/MTA lifetime behavior. | ADR 0008 and TEST_STATUS S-005: fresh /WX 49 unit + 10 read-only integration + 1 isolated shutdown cases pass, independently reviewed. Preserve runtime ownership limits and wider OS qualification. |
| GAP-004 | Current Graphify launcher remains unavailable and its graph stale; historical success is not current tool evidence. | Source/Markdown navigation remains usable. Repair optional local tooling separately; never make it a product or evidence prerequisite. |
| GAP-005 | Runnable app/installer source, Release build, 426 unit cases, isolated lifecycle and five setup cases pass. Production setup installed the alpha without startup; all 91 integration cases have passing scoped runs, but no uniformly green combined matrix. Package/export/public CI/release receipts remain pending; older leaking binaries are excluded. | T9–T15 finish remaining gates and final package/export privacy checks, then public CI and approved release evidence. Preserve wider platform/manual/update qualification limits. |
| GAP-006 | Resolved without changing FR-010: the serial HTTP worker no longer blocks the scheduler that publishes six-hour cache expiry. | TEST_STATUS S-006: /WX 56 cases pass, including expiry before a blocked request returns; independently reviewed. Live forecast/geocoding and consent/UI qualification remain separate. |
| GAP-007 | Resolved by UI-owned settings state/notifications and FIFO worker disk persistence; no UI-thread disk exception was introduced. | ADR 0007 and TEST_STATUS S-007/S-009: current Release /WX 33 unit cases and isolated app shutdown pass. Retain accepted-versus-durable completion, model quiescence and flush ordering. |
| GAP-008 | Validated configurable chord fields and native editors now exist, including all fifteen nonzero modifier masks; the enable-only contract discrepancy is resolved in implementation. | TEST_STATUS S-007/S-008: Release /WX 33 model and 5 native editor cases pass. Isolated app regression passes; real global shortcut conflicts, startup policy and physical desktop qualification remain T10/T11/T14. |

None of these questions requires inventing a requirement or blocks documenting the confirmed scope.
No new owner decision is required to finish this documentation session. Ask when a proposed palette,
performance budget, architectural reversal or publication action is concrete and actually needed.

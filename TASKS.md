# Tasks

Never mark a task done until its acceptance criteria were verified (command run, result observed).
Requirements and definitions: [PROJECT_SPEC.md](PROJECT_SPEC.md). Test evidence:
[TEST_STATUS](docs/knowledge/TEST_STATUS.md). Every task also follows NFR-004/NFR-008/NFR-010
(privacy, safe tests and continuity); listed links below identify its main product contribution.

## Current priority
**V3 starts here (D38):** the owner accepted Alpha 2. Continue FR-031–FR-036 using
[the V3 work order](docs/specs/v3-design-and-interaction.md) and
[the English continuation prompt](docs/V3_CONTINUATION_PROMPT.md). This is a broader design/motion
overhaul, not a finished release after one batch. Preserve the installed Alpha 2; no V3 publication yet.

**Current owner priority, 2026-10-25 — T47:** Codex resumed Claude's interrupted visual redesign across
the logo menu, every Settings page/control and all Aether-owned supporting interfaces. Follow
[the full English implementation brief](docs/CLAUDE_V3_VISUAL_UNIFICATION_PROMPT.md), FR-037/D41.
Reuse the neutral glass identity while preserving native input/accessibility, real actions,
consent/save behavior and the current private trial. The reviewed source is committed locally as
`2e4ff0d`; corrections cover build, retry, compact/text-scaled layout, page drafts/refresh,
accessibility and progress contrast. Complete Release `/W4 /WX` and 24 unit executables pass;
all 11 affected native executables have passing final scoped runs (S-049 records the initial
Dock fixture failure and separate corrected rerun). The private committed candidate is running
with Settings verified visible and five pins preserved. Owner visual/motion acceptance remains
separate; the installed Alpha 2 and startup registration are preserved. No push/install/release.
Owner clarification: T47 also requires the complete installer/maintenance UI, from welcome/licence/
options through progress, completion/errors, upgrade, repair and uninstall; a branding-only pass
does not satisfy it. Preserve the installation engine and validate in isolated setup fixtures.

**Earlier owner priority, 2026-10-25:** Claude implements the focused T42/T43 hover-preview and
slower marker-motion batch; Codex independently reviews afterward. See
[the exact implementation prompt](docs/CLAUDE_V3_HOVER_PROMPT.md). Other V3 tasks remain open but
are outside that first Claude batch. Claude implemented it locally on 2026-10-25 (S-041, T42/T43
rows below). Codex's focused independent review and corrections pass S-042. The owner requested a
local prototype launch; its runtime receipt and the owner's visual acceptance are separate gates.
The owner accepted the reviewed `f692b9f` prototype's basic interaction and requested tighter card
spacing. The compact `fdc2691` prototype preserved image/X size (S-043). The subsequent pin/corner
prototype `eae2684` was reviewed in a private profile (S-044/S-045), with app/taskbar/corner-helper
readiness checked. The owner withdrew an apparent acceptance reply and rejected the dark remainder
around the rounded corner; no acceptance of either fix may be inferred from that reply. S-046
corrects the full-window material and passes the composed-pixel/cleanup gate (58 unit, 12 native).
The combined corrected `661a4cd` trial was verified independently, then superseded by the T47
`2e4ff0d` trial above; owner visual verification, including the single-icon scenario, remains pending.
The owner then reported rectangular grey patches around magnified Dock icons on white backgrounds.
S-047 removes the clipped panel/lift shadows; two dark/light own-window pixel probes pass. Both
corrections are in that active private runtime, preserving the exact input footprint.
The installed Alpha 2 files and startup remain unchanged.

| Task | Requirement links | Current boundary |
| --- | --- | --- |
| T47 — Coherent Settings, menus and supporting UI | FR-037, FR-011, FR-015, FR-022, FR-026, FR-034; NFR-001–NFR-011; D41, D42 | Implemented and independently reviewed in local source commit `2e4ff0d`: shared forms/menus across all Settings pages, setup/maintenance, updater and ordinary confirmations. Release `/W4 /WX`, 24 unit executables and final scoped runs of all 11 affected native executables pass (initial Dock fixture failure and corrected full rerun recorded in S-049). All 16 Settings and 32 setup/updater stills inspected; private runtime/hash/helpers/pins and Settings visibility verified. See the [surface inventory](docs/specs/v3-visual-unification-inventory.md) for native exceptions and unqualified states. Owner visual/motion acceptance, physical DPI, screen-reader/IME and clean-machine qualification remain; V3 is not complete or published. |
| T45 — One active shared working folder | NFR-004, NFR-008, NFR-010; D18, D39 | Executed by Claude (S-040). Codex confirmed the official origin, single worktree, local lineage and a fresh successful build (S-042); an unexpected byte-identical launcher folder move was reversed without content edits. Private recovery/archive restore verification remains Claude's receipt. [Addendum](docs/CLAUDE_SINGLE_CHECKOUT_PROMPT.md). |
| T46 — Attach executable-only windows to their unique desktop pin | FR-002, FR-003, FR-004, FR-034; NFR-004, NFR-008 | Implemented (S-044): same validated full executable path, argument-free desktop AUMID pin, no conflicting catalog/running identities or ambiguous pins. Explicit profiles remain separate; pin ID/launch route retained. 65 Dock unit cases and one own-window merged-pin preview/activation/close regression pass. Updated runtime is active; owner confirmation pending. |
| T39 — Exact bar input footprint | FR-031; NFR-006–NFR-008, NFR-011; D38 | Source patch removes expanded Dock/Top Bar visual HWND regions (S-035). S-047 additionally disables Dock panel/lift shadows that were clipped into square patches at raised icon slots; dark/light composed white-backdrop probes pass without expanding input regions or changing magnification. Separate-process input delivery, rounded-corner input and physical visual acceptance remain. Not installed into the owner's Alpha 2. |
| T40 — Compact Dock and symmetric usable edges | FR-031, FR-032, FR-027; D38 | Compact panel implemented: top padding 12→9 DIPs, default height 70→67, unchanged icons/custom sizes/bottom gap; 96/144/192-DPI geometry checks pass (S-036). No settings migration or installed change. Symmetric work-area prototype and physical visual acceptance remain. |
| T41 — Native system controls, real signal and rounded icons | FR-035, FR-026; D38 | Guarded native Windows 11 Quick Settings request and visible fallback preference implemented; fake injection/OS/partial-release and own-window preference tests pass (S-036). Native opening/placement under replacement, signal freshness and icon redesign remain unverified/unstarted. |
| T42 — Living dot/pill indicators | FR-033; D38 | Claude implementation (S-041), independently reviewed and scoped tests rerun by Codex (S-042): dedicated calmer `Motion().marker` spring (≈0.35 s to 90 %, 0.52 s to 0.1 DIP; magnify unchanged); hover/card emphasis expands a background dot to exactly the foreground width without activation or active colour; activation hold preserves width through the observed transfer. Owner normal-speed real-desktop acceptance pending. |
| T43 — Live hover cards and selected-window actions | FR-034; D38 | Claude implementation (S-041), Codex input corrections (S-042); owner confirmed basic hover/preview works, requested tighter spacing. Compact content-fitted cards preserve thumbnail size and 28-DIP X; S-043 passes 62 unit and 10 executed native cases. New spacing acceptance, card anchor after Dock reflow, source-only resize crop/aspect, physical DPI and later media controls remain open. |
| T44 — Window choreography and corners | FR-036; D38, D40 | Guarded policy implemented (S-045), then owner-rejected rectangular material corrected in S-046: independent exact material/region leases, app replacements preserved, partial-error recovery and bounded retry. 58 app unit and 12 native cases pass, including actual cutout pixels over two changing private backdrop phases with genuine composition content and material restoration. Corrected runtime/owner acceptance remains separate; unsupported windows, antialiasing, guard-self-crash recovery, broad compatibility and icon-anchored choreography remain open. |

**Delivered (D37):** [Alpha 2](https://github.com/ibrahimbouillet/AetherShell/releases/tag/v0.2.0-alpha.2)
is published from `ad77019`, with exact-revision Windows CI, audited setup/checksums and a scoped
installed startup/recovery gate. Alpha 2 remains installed; see HANDOFF for the active local trial. Continue T38's native action diagnosis
and fresh gallery afterward; D36 readiness/privacy still apply. No new gallery/export is claimed.

**Owner continuation (2026-10-23, D34/D35):** proceed to Alpha 2 build/install/publication. Add
disclosed installed sign-in startup and bounded abnormal-crash recovery while preserving Quit,
opt-outs, Task Manager disable and reversible native-taskbar recovery. Generalize capture scenarios,
review desktop/quick-controls/settings/volume media and retain a versioned Alpha 2 comparison gallery.
Fresh release build, tests, package/privacy audit and exact-revision CI precede publication. Source
work and gallery are in progress; no new startup/release qualification is claimed yet. The previous
T36 score remains explicitly unverified and is no longer the sole continuation priority.

**Capture coordination (D36):** the owner uses the computer during preparation. Ask a readiness
question and wait for an explicit ready reply before each assistant-operated real recording attempt.
No successful new gallery export exists yet. The narrow shell-helper policy passes read-only
preflight; the latest owner-ready take reached the real-volume acknowledgement check and failed.
A missing live audio-to-panel bridge is being corrected and tested. Keep scene/privacy guards strict.
Code/build/read-only preflight may continue.

**Delivery authorization (2026-10-22, D29):** the owner lifted D25 for the second public release,
`0.2.0-alpha.2`, and authorized the reviewed push, tag and GitHub Release. Exact receipts are in
HANDOFF and S-028.

**Earlier T36 checkpoint (2026-10-23; continuation priority superseded by D34/D35):** The native pipeline
implements protected clean-session closure/relaunch, isolated real pins/Code profile, nonblocking
DXGI/cursor capture, a 512-MiB bounded lossless tile buffer, validated archive/offline recipes, frame-ID
GPU caching, H.264/AAC and WIC GIF. D33 fixes the exact sequence: two-second opening with pointer,
left approach and full Dock travel, one-second Code hover, actual loaded workbench, three seconds
total after readiness (reveal included), observed close and two captured body/backdrop matches before
three visually empty seconds, then old-app restoration and disk flush/render. Return the pointer to
its neutral start during reveal. The focused Release `/W4 /WX` suite passes 66 cases (2.17 seconds),
and own-window integration passes two cases (105 ms). S-033 records a real 1,030-frame 1080p60 MP4
with complete successful decode and scoped every-frame contact/privacy review. Cursor, toast,
orientation, GIF channels, footer framing and the full empty ending are corrected. Source samples
average 57.3721/s with variable intervals; the uniform 60-FPS camera export does not invent source
UI updates. Normal-speed playback/listening and full 60-check/ten-gate acceptance remain unverified;
no 100/100 score is claimed. The current worker is finished and the normal preview is restored.
Inspect live state/artifacts before restarting; prefer offline rerendering to another app closure.
Official `origin`
and HEAD `322594b1b3486f319c9bf7144bd683d7890e44b5` were rechecked; existing mixed staged/unstaged work
is preserved. Local recovery branch `codex/t36-showcase-checkpoint` contains the source/docs snapshot
and follow-ups without changing `main` or its mixed index. No push, tag or release was made. Recheck
D29/current instructions before Alpha 2 delivery; do not publish a later version.

| Task | Requirement links | Current boundary |
|---|---|---|
| T37 — Installed Alpha 2 session lifecycle | FR-016, FR-017, FR-020, FR-024; NFR-002–NFR-005; D34 | Implemented/tested and delivered in Alpha 2. Actual per-user upgrade, stable-launcher startup registration, controlled abnormal-child relaunch, intentional Quit/native-taskbar restoration and final relaunch verified (S-034). Reboot timing and broader hardware/platform recovery remain unverified; no immortal-process promise. |
| T38 — Versioned product media and reusable capture scenarios | FR-029, FR-030; NFR-004, NFR-008, NFR-012; D35 | In progress: genuine desktop, grouped quick controls, settings and owner-authorized volume demonstration; safe close/restore lifecycle, PNG/MP4, every-frame privacy review and versioned gallery for Alpha 3 comparisons. |
| T22 — Review/fix/integrate batch 1 | FR-003, FR-012–FR-017, FR-020, FR-024; NFR-002–NFR-011 | Three independent source reviews completed. Fixes address default-on consent fixtures/D23 docs, unknown-profile relaunch, declared-icon reparse ancestors, owner-drawn menu data, Search visual/hit geometry and mid-transition accessibility policy. Combined Release /WX, 669 unit and 122 isolated integration cases pass (S-022). Integration merge b87be0d committed/pushed; real desktop remains separate. |
| T23 — Dock and Top Bar consumers | FR-001–FR-011, FR-014, FR-024; NFR-006, NFR-007, NFR-011 | Shared roles/glyphs/menus/motion, fixed Start/Search/Task View entries, profile presentation and session-only running-item reorder, settings-driven layout and runtime Windows taskbar access implemented. Review fixes cover empty input regions, stale modal actions, shutdown admission and late avatars. Combined Dock31 unit/3 native and Top Bar20 unit/4 native pass, Release /WX (S-022); real desktop/profile checks remain open. |
| T24 — App/settings integration and delivery gate | FR-014–FR-025; NFR-002–NFR-010 | Appearance, guarded worker pin sync, shell actions, focus-guarded Windows voice typing (D24) and timed suspend/resume wired; policy/native regressions added. Release /WX, 669 unit/122 native/32 release-tool cases pass (S-022); setup payload/freshness privacy audit clear. Source b87be0d pushed; Windows CI passed for b87be0d; authorized desktop gate found the S-023 framed-fullscreen defect, now under correction. Flyout preference routing and manual desktop/voice qualification remain open. No V2 publication or install claimed. |
| T25 — Framed fullscreen/native ownership follow-up | FR-001, FR-002, FR-011, FR-024; NFR-005–NFR-008 | Style-aware classifier and single suppression authority pass zero-warning Release /WX, 672 unit and 123 native cases (S-023). Corrected bounded desktop launch keeps both bars visible, reflows an own pre-maximized window and restores exact native state. Explorer pin confirmed. Subsequent Dock lifecycle gate has an intermittent UIA root-type failure followed by unchanged-binary pass; both retained. Running-icon pixels and reveal/resume remain open. |
| T26 — Dock optical icon balance | FR-002; NFR-002, NFR-007, NFR-011 | Removed duplicate 18-percent drawing inset from fixed glyphs; layout/hit/hover geometry and Top Bar unchanged. Release /WX and own synthetic dark/light visual inspection pass (S-024); Dock31 unit/app2 native pass. Light native UIA root-type failure recurred (Pane 50033, own HWND); qualification remains open. |
| T27 — Native UIA discovery/teardown investigation | FR-015; NFR-004, NFR-008, NFR-010 | Local milestone9bec731 retains strict failure diagnostics. Preserved S-025 metadata investigation is now committed with RangeValue in d7e17c4. All6 accessibility native cases pass in S-026, but earlier intermittent MTA discovery failures remain unexplained. Repeated-disconnect experiment is inconclusive and preserved outside the default suite. Measure first cleanup HRESULT and first failing provider/client stage before choosing a correction. No causal fix or release qualification claimed. |
| T28 — Grouped quick controls | FR-026; FR-007–FR-010, FR-015; NFR-002–NFR-008 | Implemented/reviewed modules, production grouping and guarded app bridge. Release /WX698 unit cases pass; native controls4/accessibility6 and Dock3/TopBar5/app2 scoped gates pass (S-026). Real device controls/artwork/manual qualification remain open. |
| T29 — Neutral bar glass | FR-004, FR-011; NFR-011 | Uniform border/centered shadow and real/unknown Wi-Fi glyph implemented; graphics68/68 passes. Own-window Dock/TopBar dark/light captures inspected. Moving radial overlay above panel content remains unexplained; broad visual approval pending (S-026). |
| T30 — Aligned maximized-window margins | FR-027; FR-001; NFR-003, NFR-006 | Owner clarified maximized only. Side leases and5 geometry cases pass; final bounded150-percent desktop gate aligns work area/own pre-maximized frame and restores native state on quit. Shell-approved horizontal exclusions preserved. Foreign AppBar/physical multi-monitor qualification remains open (S-026). |
| T31 — Living Dock gestures and neutral minimal chrome | FR-002, FR-004, FR-011; NFR-007, NFR-011; D27 | Implemented and tested in S-027: Release `/W4 /WX`, 21/21 unit executables, and Dock-own-window drag/settling integration pass. Captured pointer-follow, neighbor displacement and final settling are covered. Owner review of actual appearance, hover neutrality and reduced motion remains. |
| T32 — Rounded status/weather icons and battery thresholds | FR-006, FR-008, FR-010, FR-011; D27 | Implemented and tested in S-027: proportional glyph/battery unit coverage, Top Bar and Control Center native integration, all pass. Unknown/high-contrast behavior remains explicit. Owner review at physical DPI and live provider conditions remains. |
| T33 — Genuine application attention | FR-028; D27 | Implemented and tested in S-027: app/Dock unit and own-window lifecycle/attention integrations pass. No forced foreground or title-derived alerts. Real shell attention and fullscreen-peek behavior remain owner/device acceptance. |
| T34 — Automatic Windows weather location | FR-010, FR-026; D28 | Implemented and tested in S-027: coarse consent-gated location policy, settings and fake UI/integration cases pass; stale consent callbacks are fenced against changed location policy. No real OS location consent/read or forecast request was made. Owner opt-in, Windows permission and live forecast remain unverified. |
| T35 — Alpha 2 README showcase | FR-022–FR-023; NFR-004, NFR-007, NFR-012 | README text and release links updated. T36 now provides a genuine locally reviewable 1080p60 MP4/GIF and scoped frame-contact privacy evidence (S-033). Whole-film acceptance, selected README imagery, a smaller GIF and publication are still pending. Never reuse rejected fixtures/takes. Exact application state restoration is not guaranteed. |
| T36 — Native cinematic showcase engine | FR-029; NFR-001, NFR-004, NFR-005, NFR-009, NFR-011–NFR-012; D30–D33 | **Implemented/tested; whole-film acceptance in progress.** Native lifecycle, isolated real pins, nonblocking DXGI/cursor, bounded tile RAM, validated archive/recipe, shutter-sampled GPU composition, H.264/AAC/GIF are runnable. S-033: 66 focused Release `/W4 /WX` cases plus two own-window cases pass. Exact C5FF64A3 export: 1,030 frames, 1080p60, full decode and scoped every-frame contact review; observed clear central ending 3.0239 seconds. Toast/hover/footer/orientation/channel defects corrected. Source intervals remain variable; no native constant-60 claim. Normal-speed playback/listening and complete 60-check/ten-gate acceptance remain unverified; no 100/100 claim. Preserve the exact D33 sequence. Forced-termination and unsaved-state recovery remain limited. |

**T16 — V2 (0.2.0-alpha.2), owner-requested, in progress.** Replace the duplicated taskbar layout
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

## Historical D27/D28 continuation priority — superseded by D38 above
The D27/D28 source and automated gates are complete for this slice. The visible worktree candidate is
ready for owner review of Dock motion, status glyphs, attention, reduced motion and the Windows
weather permission flow. Continue from the owner's feedback and the remaining manual acceptance gaps
in `HANDOFF.md`; keep the preview active between bounded tests. Check D25/D29 before any later
publication; D29 authorizes only the 0.2.0-alpha.2 release. Preserve the independent public Git history (D18).

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
| T16-gate | Historical V2 gate status (S-017) → FR-001, FR-002, FR-008, FR-015, FR-024; NFR-007, NFR-008 | T16, T21 | Release /WX build and 480 unit cases pass. All 14 integration suites (95 cases) have passing runs of the S-017 revision, but no combined run has all 14 green. ShellApplication review done with no blocking defect; minor finding: the poll timer keeps waking after activation attempts are exhausted. Package privacy audit passed (S-018): the setup is built locally, unpublished, and its manifest is unsigned. Pushed with the owner's approval; Windows CI passed on `e29e83e` after an 8.3 short-path fix (S-019, S-020). Remaining before `0.2.0-alpha.1`: Windows 10, Explorer restart during replacement, an auto-hide-off desktop, and a manifest signature from the owner's key if updates should be offered. Current combined candidate qualification belongs to T22–T24 / S-022; S-021 supersedes older Explorer/auto-hide observations. |
| T18 | Owner decisions from the spec review → GAP-010, GAP-011, Q-001, Q-002 | owner | GAP-011 scope was answered by D21. Repository description, global performance budgets and final branding remain unresolved in the canonical spec; do not infer approval from a proposed task outcome. |
| T19 | `ARCHITECTURE.md` and `TESTING.md` → FR-023, NFR-009 (GAP-009) | — | Implemented and link-reviewed on 2026-10-20. Public entry points describe module/trust boundaries and safe test/performance/manual qualification, linking canonical live sources. GAP-009 resolved in source. |
| T20 | Diagnostics export check → FR-025, NFR-004 | T10 | Export a diagnostics bundle from Settings with temporary data, inspect its contents for private data and the privacy statement, record the result in TEST_STATUS. |
| T21 | Taskbar recovery without Aether Shell running → FR-024, NFR-003, NFR-005, NFR-008 (D21, ADR 0009) | T16 source | Implemented and checked with simulated/temporary fixtures and an owner-authorized older desktop revision (S-015/S-016). S-021 additionally records auto-hide off and Explorer restart on e29e83e; it records the owner dropping Windows 10 qualification because no machine is available, not a Windows 10 pass. D23 adds the disclosed restore shortcut. The newly combined candidate needs its own authorized desktop gate; automated/native-guard tests do not replace it. |
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
| Microphone/voice interaction — FR-009, FR-011; NFR-004 | Owner chose Windows dictation plus settings/mute (D24). Microphone click retains endpoint mute; its context menu adds Windows voice typing after verified previous-window focus. App uses the shared Win+H helper, with no Aether capture service. | Verify fake menu/focus admission and unchanged audio state; actual Windows dictation in an editable field remains a manual owner check. Never start dictation in automation. Microphone-in-use detection stays unsupported (D11). |
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

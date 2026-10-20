# Handoff

> **PUBLIC REPOSITORY — READ THIS FIRST.** Never commit credentials, private keys or their locations,
> personal addresses/accounts/paths, machine names, unrelated app content or captures outside the
> project's own isolated test windows. Public owner attribution: Ibrahim Bouillet. See AGENTS.md.

## Active continuation — Codex, 2026-10-20–21 (Europe/Paris)

Long-term objective: a native C++20 Windows Dock and Top Status Bar alongside Explorer, with real
status, accessible interaction, durable settings, safe per-user installation and authenticated
updates. This session recovered Claude's interrupted `0.2.0-alpha.1` batches, implemented their
consumers/app integration, independently reviewed them and completed the automated candidate gates.
The published `0.1.0-alpha.1` does not include this work.

### Git and preservation checkpoint

- Official public `origin`: `https://github.com/ibrahimbouillet/AetherShell.git`; a fresh fetch confirms
  recorded remote `main` at `8e11a68`. The original clean local `main` starts at `d5ccbd6`.
- Recovered streams: `wf/b1-lifecycle` = `5cf6e72`, `wf/b1-parity` = `1ef9d72`,
  `wf/b1-visual-core` = `4b7c8a8`. Five dirty/untracked visual files were copied byte-for-byte into
  the integration candidate before review fixes. Original Claude worktrees/files remain untouched.
- The owner confirmed Claude stopped after quota interruption. Implementation continues in a linked
  public integration worktree, discoverable with `git worktree list`; it has detached HEAD at
  `d5ccbd6` with an uncommitted octopus merge of the three streams plus the edits described below.
  Do not reset/discard it. Coherent commit, safe fast-forward of `main`, push and new CI are next;
  a later delivery receipt must state their actual outcome before claiming them complete.
- The archive checkout has no remote and private history; never develop or push from it. No private
  archive history, owner canvas, private capture, browser profile database or assistant state was copied.

### Implemented and reviewed

- D23: taskbar replacement defaults on, explicit opt-out/reset semantics preserved, setup disclosure
  and restore shortcut. Canonical default-on docs and native Settings consent fixtures agree.
- Applications: trusted profile launch/pin routes, best-effort Taskband pin reconciliation, one-time
  Explorer default, worker-only 8.3 planning, ancestor-junction icon rejection, late profile avatar
  presentation without changing launch routes. Unknown profiles can activate existing windows but
  cannot silently launch the default browser profile. Actual Chrome behavior still needs manual checks.
- Dock: shared roles/glyphs/glass/menus, configurable Start/Search/Task View, saved pin order and
  session-only running-item drag, dismissal persistence, source-sensitive icon refresh, compact
  shadow reservation. Reviews fixed invisible empty regions, modal-menu lifetime/stale selection,
  shutdown/entrance admission, DPI cache retention and queued/async tooltip resurrection.
- Top Bar: configured height/weather side/keyboard format/battery preference, shared material/glyphs,
  anchored menus, keyboard/network/weather/clock/power/sound/privacy actions and temporary native
  taskbar reveal/manual resume. Dock remains visible until native restoration completes; a monotonic
  60-second timer then resumes replacement. Stale timer messages cannot shorten a newer reveal.
- Owner clarified **Chrome profile avatars**, **Dock-icon context menus**, and **Windows dictation
  plus settings/mute** (D24). Do not ask those scope questions again. Voice typing requests Win+H
  only after verified previous-window focus; Aether does not capture/store speech. No real dictation
  or audio control was exercised by automation.
- App/settings: Appearance page, shared theme/motion application, worker pin sync with settings
  revision/retry/opt-out protection and bounded newest dismissal retention. Native taskbar access
  remains restricted to the production instance; isolated full-app tests cannot hide it.
- Graphics/UI/Search: foreign menu-pointer rejection, correct shadow/entrance regions, actual drawn
  row hit/UIA geometry, entrance pointer guard and accessibility-motion cancellation. A reproduced
  CompositionScopedBatch crash was fixed with explicit strong batch/event-token ownership and
  lifetime/cancel/detach/destruction/throwing-callback regressions.
- Continuity: reusable English `docs/SESSION_START_PROMPT.md` works for Claude/Codex. Added required
  `ARCHITECTURE.md` and `TESTING.md` (T19/GAP-009). Refreshed six public screenshots from independently
  inspected own-window synthetic fixtures; no owner desktop screenshot is public material.

### Executed verification — exact scope in TEST_STATUS S-022

- Frozen full Release /W4 /WX build: app/launcher/updater/setup and all tests, zero warnings.
- Combined unit gate: **18/18 executables, 669/669 cases pass** (15.18 s).
- Combined serial non-network integration gate: **14/14 executables, 122/122 cases pass** (79.94 s).
  Includes Dock3, Top Bar4, Settings UI10, app2, UI12 and Search UI10. These are own-window/fake or
  read-only checks; no actual taskbar/work-area/audio/startup/keyboard/dictation mutation or external
  app launch. Raw logs/JUnit stay in ignored `build/wf-codex-integrated/evidence`.
- Release-tool Python tests: **32/32 pass**. Baseline Search initially failed timing under concurrent
  compilation; its quiet rerun passes, and candidate p95 is 0.231 ms (2,000 apps), 0.435 ms (5,000),
  with unchanged budgets. Original native crashes and premature 700-ms idle assertion remain recorded;
  corrected tests retain a bounded two-second settlement deadline and <=2 idle-frame requirement.
- Local package privacy/freshness audit passes, including setup and uncompressed embedded binaries.
  `dist/AetherShell-0.2.0-alpha.1-win-x64-setup.exe`: **4,957,184 bytes**, SHA-256
  `6d0f54e03171aa4f5d80198a20fe5f431633f689fc2d84fb3fdffe024568e70b`.
  Setup and checksums are review assets only. **Unsigned manifest stays local.**
- Initial source/history privacy review found no private material; synthetic rejection-test strings
  were inspected separately. All 17 unpublished commit identities use GitHub noreply addresses;
  165 unpublished history blobs/commit messages had no environment-specific/secret-pattern finding.
  Final staged diff/identity audit must precede the source push.

## NEXT SESSION START HERE

1. Read this active section, AGENTS and `docs/SESSION_START_PROMPT.md`; verify official origin, Git
   status/HEAD/merge parents and worktrees. If the pending merge still exists, finish its final audit
   and coherent commit; preserve every original worktree. Recheck the original `main` before any
   fast-forward, then use the already authorized public source push and observe Windows CI. Never
   force-push or claim a publication/CI receipt that has not happened.
2. Read the latest delivery receipt added below, TASKS T22–T24 and TEST_STATUS S-022. Automated gates
   above are complete for the frozen candidate; do not repeat them unless source changes or an actual
   concern requires it. New CI, actual desktop/voice qualification and V2 publication are separate.
3. Obtain the owner's explicit current-session authorization before changing the real desktop
   (ADR0009/T21; this was also required by the previous handoff). With an agreed recovery procedure,
   check one visible bottom bar, maximized work-area protection, normal quit/restore and temporary
   reveal/resume; qualify crash/double-failure and Explorer restart separately if authorized. Do not
   restart Explorer, change actual audio/startup or start dictation in automated tests. S-021 is an
   older e29e83e desktop receipt, not qualification of this new combined candidate. It records the
   owner dropping Windows 10 qualification for lack of a machine, not a Windows 10 pass.
4. Check real Chrome grouping/avatar/launch identity and voice typing in the intended editable field
   with the owner. HWND focus admission and SendInput acceptance do not prove dictation success.
   `topBar.openWindowsFlyouts` is persisted but has no routing consumer; fixed status routes are
   documented honestly. T20 diagnostics export remains unverified. Q-001/Q-002/GAP-010 stay open.
5. Prepare/review any changed package again, retain unsigned manifests locally, then obtain explicit
   publication approval for the concrete prerelease assets. No V2 tag/release/install was performed
   in this session. Never silently enable startup, read private account databases or publish logs,
   PDBs, keys/locations, owner captures, local assistant state or private archive history.
6. Update this canonical handoff and TEST_STATUS at every meaningful checkpoint. Use the portable
   English prompt for the next assistant; proprietary tools/skills are optional and not continuity.

## Historical receipts — previous sessions, superseded as continuation instructions

### Claude, 2026-10-18, about 15:55–16:40 CEST
Task: the owner asked for one authoritative specification, checked against the original brief, and for
a standard prompt to start every session. Claude changed no product source and made no commit in this
checkout. Nothing was pushed or released, and no repository setting was changed.
- **Independent checks (TEST_STATUS S-014):**
  - A fresh Release /WX build of the uncommitted V2 tree has 0 warnings; all 17 unit executables
    (468 unit cases) pass.
  - Read-only GitHub checks confirm the public repository, the green CI run 39123368917, and the two
    published prerelease assets with the recorded size and SHA-256.
  - A privacy scan of tracked and untracked files found nothing.
- **PROJECT_SPEC.md:**
  - The sources of requirements and their authority.
  - The problem statement and use cases.
  - The owner's Version 1 definition of done as the release gate (section G).
  - Milestones M0–M5 and the long-term direction from the brief (section B).
  - New FR-025 (logging and diagnostics).
  - Acceptance criteria completed from the brief for FR-004, FR-010, FR-015, FR-018–FR-020, FR-022,
    FR-023, NFR-001 and NFR-002.
  - The owner's decision priorities and prohibitions (section F).
  - GAP-009–GAP-012 and an updated GAP-004 (section H).
- **New:** `docs/SESSION_START_PROMPT.md` (D20).
- **Updated:** DECISIONS (approval-source note, D20), TASKS (T-DOC-2, T18–T20, T5a count), AGENTS and
  CLAUDE (session prompt, archive copy, Graphify status), TEST_STATUS S-014.
- **Archive copy:** a "do not develop here" notice was committed there locally (`99ec016`); nothing
  else changed there.
- **Not checkable or not run:**
  - Owner approvals recorded by Codex (D18, D19, T17 wording) come from its own conversation.
  - V2 integration/native suites and real desktop behaviour were not run in this session.
- **Owner decision after the review (D21):** the owner rejected the double-failure risk (GAP-011).
  The taskbar must be recoverable without Aether Shell running, through `--restore-taskbar` and setup
  repair using a persisted baseline, before `0.2.0-alpha.1` is published. Task T21; not implemented yet.
- **Owner decisions still pending (T18):** GAP-010 (repository description), Q-001 (performance
  targets), Q-002 (branding).
- **T21 implemented (same session, about 16:30–17:00), TEST_STATUS S-015:**
  - The taskbar baseline is saved atomically to `<config>\taskbar-baseline.json` before the helper can
    hide anything. A baseline left by a crash is restored first.
  - `--restore-taskbar` with no instance running restores from that file (or shows the taskbar),
    turns replacement off and reports the result.
  - Setup repair and uninstall restore a saved baseline when no instance runs.
  - Results: 480 unit cases and 3 silent setup cases pass, with 0 warnings.
  - The first test run briefly called the real taskbar once to make it visible (it already was) and
    waited on a message box. Claude stopped that test process and fixed the code (see S-015).
  - **Real-desktop check (owner-authorized, S-016):**
    - It first exposed a V2 bug: the Dock's first AppBar reservation was always rejected, so
      replacement never activated. Fixed in `src/dock/DockWindow.cpp`.
    - After the fix, the taskbar was hidden, both processes were killed and `--restore-taskbar`
      reported success. The saved state was removed and the mode was turned off.
    - Explorer had already shown the taskbar again by itself.
    - Because the saved state recorded the taskbar as not visible (an Explorer re-layout at capture
      time), restoration now always shows the taskbar (D22).
    - The owner's installed 0.1 alpha was closed for the check and restarted; the taskbar is back to
      its pre-test state.

## Historical continuation list (use the current list above)
1. Follow `docs/SESSION_START_PROMPT.md`. Check that `git remote -v` shows the official repository: this
   checkout is the only one for development. The original checkout is a local history archive (GAP-012).
2. The V2 work (Codex) and Claude's T21 fixes and documentation are committed as one commit, "Add
   reversible taskbar replacement with recovery without Aether Shell running", followed by gate,
   audit and CI-fix commits.
   - Before the commit: a privacy check of every changed or new file and the four changed screenshots.
   - **Pushed with the owner's approval** (S-019, S-020): `origin/main` contains all of it. No tag and
     no release were created; `0.2.0-alpha.1` is not published.
3. V2 gate (S-017):
   - **Done:**
     - V2 steps 1 and 2: the integration suites and the `ShellApplication.cpp` review (no blocking
       defect).
     - All 14 integration suites (95 cases) have passing runs. A Top Bar fixture fix moved its guard
       band off the screen corner.
     - No combined run has all 14 green yet; desktop activity interrupts the GUI suites.
   - **Package privacy audit (S-018): passed.**
     - The Release setup `AetherShell-0.2.0-alpha.1-win-x64-setup.exe` (4,617,216 bytes) is in the
       local `dist/`, unpublished.
     - Its manifest is unsigned and must stay local.
   - **Pushed (S-019, S-020).** The owner approved both pushes; no tag, no release.
     - CI run 39139547133 on `9a2b1bb` **failed**: 2 pin-import unit cases broke on the runner's 8.3
       short temp path.
     - The fix (path identity in catalog matching, plus the tests) is commit `e29e83e`. Its CI run
       39156124337 **passed**: Release build and 17/17 unit executables.
   - **Remaining:**
     - Windows 10 and Explorer restart during replacement;
     - the owner's manifest signature if automatic updates should offer it.
4. T21 is implemented, unit-tested and checked once on the real desktop (S-015, S-016), including the
   Dock reservation fix and D22. Review it with the rest of V2. Still open before `0.2.0-alpha.1` is
   published:
   - Windows 10;
   - Explorer restart during replacement;
   - a desktop whose taskbar does not use auto-hide (the owner's does);
   - the rest of the V2 gate.

   The remaining owner answers are in T18.
5. Then T17 (the owner's newest requests: drag reorder, Chrome profiles, File Explorer entry,
   interactive status controls), T19 (`ARCHITECTURE.md`, `TESTING.md`) and T20 (diagnostics export check).
6. Real desktop checks (taskbar hide and restore, crash recovery) need the owner's explicit authorization
   in the current session; follow V2 step 3.
7. Update TASKS, TEST_STATUS, DECISIONS and this file at each checkpoint, not only at the end.

## V2 continuation — IN PROGRESS, 2026-10-18

**Read this section before the older alpha receipt below.** The public checkout contains uncommitted
V2 work. Version in CMake is `0.2.0-alpha.1`; it is not yet installed, pushed or released. The running
installed application is still the old `0.1.0-alpha.1`. Never discard these edits or restart from the
old completion claims. Work only in the public clean checkout; the original development history
remains local (D18). User-supplied desktop screenshots remain private and must never enter Git.

The owner approved reversible visual taskbar replacement, superseding the original no-hide rule
only within D19 / ADR0009 / FR-024. Explorer keeps running. Native Windows Settings cannot become
Aether's taskbar settings page through a supported API; the Desktop page owns this mode. Settings
default false and require Apply. The owner also authorized enabling this mode for their own use.

Implemented, with focused checks:
- Dock placement and forced AppBar reservation while replacement is requested; lower visible margin,
  animated footprint protection, import/start context actions and pin argument preservation.
  Debug /WX: 19 unit + 2 synthetic native cases pass; real shell geometry remains unqualified.
- Read-only best-effort Windows `.lnk` pin import, catalog identity reconciliation, complete arguments.
  Release /WX: 66 applications unit cases (9 new) pass. Windows 11 pin completeness/order not promised.
- Correct Wi-Fi arcs/dot, distinct Ethernet/cellular, visible glass, exterior-only shadow, opaque
  high contrast/transparency-off. Release /WX: 14 graphics/Top Bar unit + 5 native cases pass.
  Synthetic paired-background RGB deltas: 26.827 dark, 18.968 light, 0.000 high contrast.
- Desktop consent/import/status page: 7 Debug /WX native settings cases pass (15.14 s); a separate
  Release /WX settings build passes 36 unit cases (3 new). Temporary settings/fake callbacks only;
  no actual taskbar/work-area mutations. The earlier agent's Dock/Settings Release wording was
  corrected by inspecting CMAKE_BUILD_TYPE in each focused cache; source/test counts stand.
- New platform taskbar lease, versioned-launcher recovery helper and parent fallback: 15 platform
  unit cases pass (6 new). Guard native process checks/explicit inherited handles/restoration ack
  reviewed. Controller worker owns blocking calls; 10 injected-session cases pass /WX. None of
  those tests hides or changes the real native taskbar.
- App wiring, first-import orchestration, recovery observer, `--quit` / `--restore-taskbar`, reserved
  space preflight/error fallback and shutdown AppBar-before-guard join are implemented. **The normal
  app build/tests and real native recovery are still pending.** New CLI cases bring app unit count
  to 15 when the complete new glob is configured (5 command cases + 10 controller cases).
- Release Python tooling: 32 cases pass. Four updated Dock/Top Bar PNGs in resources/screenshots
  were independently inspected; only isolated synthetic windows/patterns, never the owner's desktop.

Immediate next steps:
1. Finish the current full Release /WX build in `build/wf-v2-release`. Rerun it afterward so
   CONFIGURE_DEPENDS includes any source/test files created during its first configure. Then run
   all unit executables and relevant native suites serially; do not run GUI suites concurrently.
   Current native checks pass in focused build trees; those do not establish a complete app build.
2. Finish the independent review of `src/app/ShellApplication.cpp`: callback must preserve preflight
   reservation while desired=true, never re-enable a failed reservation, and keep observing pending
   restoration. Check import persistence completion and case-insensitive identity deduplication.
3. Perform separately authorized actual desktop checks only after compilation/tests: orderly quit,
   mode disable, own application crash/helper crash restoration, work-area protection and one bottom
   Dock. Do not kill Explorer for an automated test. Never capture unrelated desktop content. If
   native ownership/reservation fails, restore Windows and report the failure rather than hiding it.
4. Address the owner's newest urgent requests in T17 (recorded below/TASKS): drag reorder, Chrome
   profile identity, Explorer access and interactive status controls. Distinguish existing actions
   from missing behavior; the installed old alpha does not include the current V2 fixes.
5. Final source/binary privacy audit, coherent source commit, public push, Windows CI, audited setup
   and owner-authorized delivery/release remain pending. Never publish Debug/PDB/log/private history
   or unsigned update manifests. Only verified source belongs in a milestone commit.
6. Update this section, TASKS and TEST_STATUS as checks finish. Keep remaining platform qualification
   explicit: no claim of complete native Settings replacement, all Windows pins, Windows 10 or all
   Explorer/monitor/full-screen recovery. Preserve the owner's canvas and local tool state.

Newest owner priorities for the next assistant (2026-10-18): holding and dragging an icon left/right
should move it into the other icon's place and persist; user reports this broken in the installed
alpha. They want recognizable Chrome account/profile logos for open browser instances, Explorer
available, exactly one visible bottom Dock, and a Top Bar that can change keyboard language, manage
Internet and provide useful microphone interaction. The owner clarified that avatars mean **Chrome
profiles**, and right-click means **a Dock icon's menu**. Exact desired menu actions remain unspecified;
do not modify application-page context menus or silently record audio.

## Previous delivered alpha state — 2026-10-18
The continuation implemented the native Dock, Top Status Bar, Search, Settings, application lifecycle,
install library, launcher, updater and setup. A per-user **0.1.0-alpha.1** is installed: production setup
exit 0, Installed apps entry and launcher present, startup disabled, no automatic application launch.
This is a delivery observation, separate from the fake/temporary-root automated test policy.

The official public repository now exists at https://github.com/ibrahimbouillet/AetherShell.
The clean source is pushed and [Windows Release CI passed](https://github.com/ibrahimbouillet/AetherShell/actions/runs/39123368917) on
`4eed02879fb79aa60376e9b50c698b295bae163e`. The first [manual-install prerelease is public](https://github.com/ibrahimbouillet/AetherShell/releases/tag/v0.1.0-alpha.1).
Its only assets are setup and checksums; no unsigned manifest, PDB or raw log is published.
Setup: 4,451,840 bytes; SHA-256 `edeafb8c89ca7c833b4b79dfbaee160267fd7278e3e0e91980f31df67fbb481f`.
The installed maintenance executable matches this exact CI package after a successful repair;
startup remains disabled and no application was automatically launched. The following documentation
receipt changes no product source and therefore does not rerun the same CI build. See S-012.
No live signed-update execution or complete Version 1 acceptance is claimed.

## Standing handover rules (from the 0.1.0-alpha.1 session; still valid)
1. Read AGENTS, PROJECT_SPEC, TASKS and TEST_STATUS S-011. Code and executed evidence win over old
   work orders. Do not restart already implemented modules from historical "not started" notes.
2. Inspect Git status and remotes. **D18: original development checkout/history stays local.** Public
   development uses the separate `AetherShell-public` clean checkout with independent history and
   public GitHub noreply identity. Never push the original history or copy its .git, build/logs/PDBs,
   .local/.claude/.obsidian/graphs or owner `docs/Untitled.canvas`. The canvas remains untouched.
3. Inspect the published alpha and S-012 receipts, then continue outstanding qualification or
   owner-requested features. The first alpha is manual-install only. Future release publication
   follows AGENTS and current owner instructions; the production signature verifier has no bypass.
4. Continue the remaining manual/platform qualification in KNOWN_LIMITATIONS and TASKS. The complete
   Release /W4 /WX build and all 17 unit executables pass. Every integration executable has a passing
   scoped run; aggregate desktop runs also recorded capture-guard refusals and activation interference.
   A quiet interactive desktop is required. Never weaken capture guards to obtain a green result.
5. Preserve test isolation: temporary roots, fake launch/audio/startup operations, no real work-area
   changes. Captures need opaque guard bands, containment and foreign-overlap checks before/after.
   Only individually inspected synthetic-window assets in resources/screenshots are publishable.
6. Before packaging, run scripts/package.ps1 on current Release source. It rejects Debug/stale input
   and scans setup plus embedded payload bytes for private source paths, personal directories, machine names and CodeView paths.
   Old Debug artifacts contain private build paths and must never be exported.
7. Update TASKS for progress, DECISIONS/ADRs for decisions, TEST_STATUS for actual checks and this
   handoff when ending a session. Preserve requirement definitions and scoped-versus-full acceptance.

## Implemented corrections and evidence
- Status lifecycle: shared sessions, joins outside lifecycle locks, explicit terminal owned WinRT
  runtime cleanup (ADR0008), 60 cases. Weather expiry runs independently of blocked HTTP, 56 cases.
- Settings: UI-owned model/FIFO worker persistence (ADR0007), 33 unit + 5 native editor cases.
  Quit settles editor/startup work, freezes new mutations, then flushes. Cancelled quit restores input.
- Search: 7 unit + 5 native UI cases; Release 2000-app p95 0.601 ms, 5000-app p95 1.962 ms in the
  recorded run. Native EDIT supplies selection/clipboard semantics; actual IME remains unqualified.
- Dock: 14 unit + 2 native cases; Top Bar: 7 + 2. UIA, keyboard, finite/reduced-motion transitions,
  update badge, glyphs, drag/hit testing and safe dark/light synthetic assets exist.
- Install library: 10 unit + 7 isolated integration cases. Setup: 5 native cases, including maintenance
  relocation and constrained DPI viewport. App: 3 CLI + 1 isolated lifecycle/health/channel case.
- Updates: 101 unit cases, including persisted skipped-version clearing. App updater handoff has a
  duplicate guard and observer established before launch, with shared completion surviving failed
  dispatcher delivery. This handoff is source-reviewed; its live signed-release cycle is unqualified.
- Release tooling: 32 Python cases pass. Public candidate review: 457 files, source attribution and
  Markdown link/requirement consistency checked. Final Release binaries/payload pass privacy audit.

## Remaining qualification
Windows 10/ARM64, physical monitor/DPI/removal, real AppBar/full-screen/Explorer restart, external
application launch/UWP frame identity, audio controls/startup policy, IME/screen-reader workflows,
owner-signed update/recovery and global performance budgets (Q-001). Final branding direction remains
Q-002. The original branding is an alpha proposal. See KNOWN_LIMITATIONS and PROJECT_SPEC.

Graphify launcher is unavailable/stale in this session; source/Markdown are current. Obsidian CLI
works while its app runs; docs/ is the vault. Local configuration, exports and owner canvas are not public.

# Handoff

> **PUBLIC REPOSITORY — READ THIS FIRST.** Never commit credentials, private keys or their locations,
> personal addresses/accounts/paths, machine names, unrelated app content or captures outside the
> project's own isolated test windows. Public owner attribution: Ibrahim Bouillet. See AGENTS.md.

## Active continuation — Codex, 2026-10-20–21 (Europe/Paris)

> **OWNER DELIVERY HOLD — 2026-10-21 (D25).** Local commits are permitted, but do not push,
> create a tag or publish a GitHub Release until the owner explicitly lifts this hold. More owner
> changes are expected. The next public delivery is **Alpha 2**, the project's second release.
> Existing `b87be0d` push/CI receipts below predate this instruction. The working candidate still
> reports `0.2.0-alpha.1`; align its final version/package naming with Alpha 2 before any later delivery.

Long-term objective: a native C++20 Windows Dock and Top Status Bar alongside Explorer, with real
status, accessible interaction, durable settings, safe per-user installation and authenticated
updates. This session recovered Claude's interrupted `0.2.0-alpha.1` batches, implemented their
consumers/app integration, independently reviewed them and completed the automated candidate gates.
The published `0.1.0-alpha.1` does not include this work.

### Active new work — D27/D28 (2026-10-21)

- Owner requests continuous captured Dock drag with reactive spring neighbors/drop, no hover tiles,
  separator or trailing native-taskbar arrow; neutral material also on hover/tooltips; round status
  icons, larger clear native type, green100-percent/red<=5-percent or OS low-critical battery.
- Genuine background shell attention should show a finite red Dock cue. A real new alert may briefly
  reveal only the Dock during fullscreen for6 seconds, without taking focus/raising the application.
- Owner explicitly clarified automatic Windows weather location for general installations, no fixed
  owner city. Use OS permission/coarse position, honest no-weather on denial/unavailability, preserve
  existing opt-outs/manual choices. Never scrape SSIDs/raw IP or pretend to read Windows weather.
- In progress in the integration worktree: lifecycle agent owns DockWindow/geometry/tests; visuals
  owns shared status glyphs/TopBarWindow/ControlCenter; parity owns attention tracker/model and then
  location adapter. Root owns status battery mapping, app policy/wiring/settings and continuity docs.
  Current preview remains the S-026 copied build until a new candidate is built/tested/reviewed.
  No push/tag/release. Read working diff before continuing; these changes are not yet qualified.

### Owner controls scope — 2026-10-21 (D26)

One grouped Wi-Fi/audio/battery action opens real media/system controls. Dock/Bar glass is neutral,
with grain, blur/tint and a uniform border. Side margins apply only to maximized windows; never move
manually placed owner windows. Keep the usable local preview active except bounded tests, then
restore it; no autorun. D25 still forbids push/tag/release. Current implementation/evidence follows.

### S-026 completed local checkpoint — 2026-10-21

- **The new controls preview is now active**, using copied current app/launcher binaries and the
  existing private preview data; no installation/autorun. Keep it active except bounded tests, then
  restore it. Ignored `build/manual/owner-preview.json` records the current process/root privately.
- Full Release /W4 /WX build succeeds. Aggregate units:20/20 executables,698 cases, all pass (14.75s).
  Registrations were cross-checked using --list because passed CTest XML output is truncated.
  Scoped native runs pass accessibility6/controlcenter4, then Dock3/TopBar5/app2. Final Top Bar
  correction rerun passes22 unit/5 native cases (7.67s). No real device control was automated.
- New media/system/controlcenter modules and app bridge are implemented and independently reviewed.
  Radio consent/worker actions use close-generation fencing and cancellation; shutdown clears pending
  flags safely even if the owner cancels Quit after a save error. Unknown remains unavailable;
  unsupported global toggles are clearly labelled Windows Settings actions. Artwork is not implemented.
- Final bounded desktop gate after preserving Shell-approved Top Bar horizontal exclusions passes:
  Windows11/150-percent work area and own pre-maximized DWM frame both [18,93,1902,969]. Normal quit
  returns both to [0,0,1920,1080], native taskbar visible/auto-hide true. No foreign HWND was resized,
  Explorer restarted or screenshot captured. Foreign AppBar/multi-monitor behavior remains untested.
- Dock/Top Bar and panel dark/light own-backdrop captures were inspected. A moving radial overlay
  appears above opaque panel content in captures; no matching radial painter exists in the call
  graph. Native feedback is only a hypothesis. Do not claim this artifact resolved. Neutral material
  tests pass; broader visual/hardware/screen-reader approval still requires the owner.
- Privacy review:50 changed public files,0 new private/credential findings;80 relative links and442
  requirement references valid. App/launcher binary privacy audits pass. No raw logs/private launch
  receipts/owner images, private Git history or local tool state are public material.
- All current source edits belong to this local checkpoint, including the preserved S-025 metadata
  regression plus RangeValue. One green six-case accessibility run does not prove the historical
  intermittent discovery/teardown issue causally fixed. S-025 negative evidence stays ignored locally.

### Git and delivery checkpoint — 2026-10-21

- Latest source checkpoint: **d7e17c4**, committed locally and fast-forwarded into clean local `main`.
  Documentation receipts may follow it; inspect HEAD. Integration and main contain the same source.
  Remote origin/main remains **b87be0d**. No push/tag/release occurred under D25.
- The active copied preview has this checkpoint's tested source contents, but its build metadata
  still embeds parent revision **2dabf2700610**, because it was compiled before the local commit.
  This is a local review preview, not a release package. Rebuild exact committed source and repeat
  appropriate package/freshness gates before any eventual delivery. No setup was installed here.

- Official public `origin`: `https://github.com/ibrahimbouillet/AetherShell.git`.
  Integration merge **`b87be0d68961acecc3777f07a0198c80b28f2912`** was committed, safely fast-forwarded
  into the original clean `main`, then pushed as `8e11a68..b87be0d`. Both `main` and `origin/main`
  were rechecked at that exact commit; the main checkout was clean before this delivery-note update.
- Merge parents preserve the foundation `d5ccbd6` and all three recovered streams:
  lifecycle `5cf6e72`, applications `1ef9d72`, visual core `4b7c8a8`. Five dirty/untracked visual
  files were copied into the candidate before fixes; their original Claude worktrees remain intact.
- Local `main` and the detached integration worktree contain source checkpoint **`9bec731`**, above the local-only
  documentation checkpoint `6f63ef3`. The source checkpoint commits S-023 fullscreen/AppBar fixes,
  S-024 icon balance, strict UIA diagnostics and two inspected synthetic images. Both local commits
  were privacy-reviewed and fast-forwarded without push. Later documentation-only checkpoints may
  follow; inspect HEAD. Remote `origin/main` remains b87be0d.
- Historical pre-D26 state: the integration worktree contained **two uncommitted S-025 files**. They
  are now included with the reviewed RangeValue work in this local checkpoint; preserve the evidence.
  Their earlier investigation was not part of the 9bec731 product checkpoint.
  Use `git worktree list` to locate that worktree; there is no pending merge. Build/log/experimental
  outputs are ignored local evidence. The ordinary `main` checkout has no pending source edits.
- Windows CI [39505236903](https://github.com/ibrahimbouillet/AetherShell/actions/runs/39505236903)
  for `b87be0d` **passed**, 10m33s, including Release build/unit gate and review package upload. This does not qualify the later fullscreen correction.
- The final local package was rebuilt with embedded source revision **`b87be0d68961`**, then passed
  privacy/freshness audit and focused app/setup gates. No V2 tag, release or installation was made.
  The owner explicitly authorized the bounded real-desktop hide/restore check. It found a fullscreen
  defect; all native state was restored before coding the correction (checkpoint below).
- The archive has no remote and private history. Never push/develop there. No private archive history,
  owner canvas, private capture, browser profile database or local assistant state was copied.

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
  `4e95a10a749792d90457c020029b321fbc7f4b8fefd970abb0a3e1e0fb985f6c`.
  Setup and checksums are review assets only. **Unsigned manifest stays local.**
- Initial source/history privacy review found no private material; synthetic rejection-test strings
  were inspected separately. All 17 unpublished commit identities use GitHub noreply addresses;
  165 unpublished history blobs/commit messages had no environment-specific/secret-pattern finding.
  Final staged audit covered 538 files with zero environment-specific/secret-pattern findings; the configured author identity was GitHub noreply before commit/push.

### Authorized desktop gate and follow-up correction — 2026-10-21 (S-023)

- The owner explicitly approved closing any old Aether instance, checking the candidate's native-bar
  hide/restore and returning to the initial state. No Aether instance was running initially.
- Candidate b87be0d ran from copied project binaries with `--data-dir` pointing to a private temporary
  synthetic configuration. Personal settings stayed untouched; weather/network updates/hotkeys,
  running-app display, pin import and Explorer initialization were disabled for this ownership test.
  The owner asked why Explorer's icon was absent: **expected in this restricted fixture**, which had
  only Start/Search/Task View. A new normal-running/Explorer-default check is now explicitly required.
- Initial Windows 11 state: native taskbar visible, auto-hide true, work area [0,0,1920,1080].
  Active candidate: native taskbar hidden, work area [0,93,1920,969], but both Aether surfaces could
  disappear when a normal framed foreground window covered the monitor. Explicit Normal launch
  reproduced it after removing the initial Hidden-launch confound. The owner saw the bars return
  when ordinary Explorer became foreground; their screenshot is private and was not copied here.
- Both bounded candidate runs exited, the saved baseline was removed, the native taskbar was visible,
  auto-hide true and work area exactly [0,0,1920,1080]. Read-only Win32 measurements only; no desktop
  screenshots, Explorer restart, real audio/startup/voice operation or personal settings change.
- Uncommitted correction in the integration worktree: platform fullscreen classification now rejects
  framed/child windows while preserving borderless full-monitor/maximized/frame-stripped fullscreen;
  Top Bar treats raw AppBar fullscreen notifications as reevaluation requests; Dock follows the host's
  combined suppression authority so raw notifications cannot override temporary native-bar reveal.
  Added three pure platform cases, one native Top Bar case and Dock regression assertions; cross-review
  found no blocker. Bool-only suppression logging helps diagnosis without app identity/title leakage.
- Full Release /W4 /WX rebuild passes with zero warnings. New combined gates pass: **18/18 unit
  executables, 672/672 cases** (30.16 s), and **14/14 serial non-network integration executables,
  123/123 cases** (127.00 s). Logs/JUnit are ignored local evidence under
  `build/wf-codex-integrated/evidence/fullscreen-*`. These results cover the fullscreen correction.
- A subsequent bounded Dock conformance fix forwards registered activation/position messages to
  the shell, preserving default message handling and the recursion guard. Its Release /WX rebuild
  passes; Dock31 unit and app2 native pass. The first targeted Dock native run failed its light
  fixture's UIA root-type assertion (stage 3), then the unchanged-binary serial rerun passed all
  three cases (8.28 s). Both raw receipts are retained. Root cause is unproven; no assertion was
   relaxed. Numeric/boolean-only diagnostics were added for any recurrence.
- Corrected candidate App SHA-256 `524afc253c84cb28e1523e15b531850f2a80ebf6defa5166748bf2ece221d0bb`
  repeated the authorized **Normal** desktop launch with running apps and initial pin import enabled.
  Both Aether HWNDs stayed visible, native taskbar hidden, work area [0,93,1920,969]. A synthetic
  framed window maximized before launch received two work-area notifications and its visible DWM
  frame reflowed exactly to that area. On normal CLI quit it returned to [0,0,1920,1080]; native
  taskbar visibility/auto-hide and work area were restored exactly, helper exited and baseline removed.
- Own Settings accessibility and private synthetic-data checks confirm five pins including Explorer.
  This does not prove actual running-icon pixels or Chrome profile completeness. The UI tool could
  read Settings but could not click without screenshot geometry, so temporary reveal/resume remains
  unverified. No desktop screenshot, unrelated-app control, Explorer restart, audio, startup or voice
  operation occurred. The synthetic probe closed itself after its bounded lifetime.
- The owner then requested equal visual weight for Start/Search/Task View and application icons.
  The Dock removes an extra 18-percent inset on each side: the shared glyphs already contain internal
  padding. Layout, hit targets, magnification and Top Bar glyph sizes are unchanged. This later visual
  edit and UIA diagnostics pass Release /WX and visual inspection (S-024); the targeted Dock gate
  again fails the light UIA fixture (Pane 50033, successful HRESULTs, correct own HWND). Dock unit and
  app native pass. The preceding manual binary hash does not cover these later edits. No assertion
  was relaxed; accessibility qualification is unresolved. Remote `origin/main` remains b87be0d;
  local commits are held under D25. No source or documentation was pushed after that instruction.

### Accessibility investigation checkpoint — S-025, incomplete

- Frozen uncommitted files in the integration worktree: `src/ui/accessibility/AccessibleSurface.cpp`
  and `tests/integration/accessibility/AccessibleSurfaceTests.cpp`. The source permits the immutable
  `get_ProviderOptions` capability getter after closure; model/property/action guards remain strict.
  A fourth native case checks owner/MTA metadata before/after closure and stale-element rejection.
- Focused Release `/WX` compilation passes. The new metadata case passes, but the existing MTA
  client fails before any action, including a fresh process that excludes the experiment. This is
  **not a verified Pane-fallback fix**. Final native gate after rebuilding without the experimental TU:
  **3 pass / 1 fail**, 0.52 s; exact command/logs in S-025. No changes to this investigation were committed.
- A bounded own-provider experiment confirmed synchronous-send rejection on a repeated disconnect,
  but its repeated-call success expectation was inconclusive after wrapper invalidation. Early reply
  did not establish a successful first cleanup. It was moved out of the default suite to ignored
  `build/manual/AccessibleDisconnectTests.experimental.cpp`; negative logs remain in
  `build/wf-codex-integrated/evidence/disconnect-experiment.log` and `accessibility-metadata*`.
  A copy of the uncommitted patch is preserved in ignored `build/manual/accessibility-metadata-unverified.patch`.
- Do not add sleeps/retries or relax Toolbar/count/invoke/stale-provider assertions. Next bounded
  diagnostic: record numeric first-call teardown HRESULT/send flags before wrapper invalidation,
  withholding reentrant provider exposure, and the first MTA failing stage plus own-instance provider
  callback/thread results. Synchronous teardown and Pane fallback may be different defects. No broad
  dispatcher/lifetime refactor or generic early-reply behavior has been implemented.
- The build cache has compiled this investigation; rebuild the chosen source and exact Git revision
  before preparing a package. The old b87be0d setup is historical and must not be presented as Alpha 2.

## NEXT SESSION START HERE

1. Read the PUBLIC rule, this active checkpoint, AGENTS and `docs/SESSION_START_PROMPT.md`. Verify
   official origin, HEAD/main/origin and all worktrees; preserve partial work. D25 forbids all pushes,
   tags and releases until the owner explicitly lifts it. Remote source remains b87be0d; later local
   milestones are not published. The archive is private historical material, never a development target.
2. Keep the current D26 controls preview active. Its ignored private receipt is under build/manual;
   do not publish it. Owner authorization already covers leaving the usable preview open and bounded
   tests/restoration. Do not introduce autorun or stop it throughout development. Rebuild before
   copying a changed binary; never overwrite a running binary. Preserve existing private settings.
3. Continue the owner's manual feedback: grouped media/radio/volume/brightness panel, neutral glass
   and maximized-only side margins are implemented. Real device controls/session switching/Windows
   routes, Chrome profile identity, voice typing, keyboard layouts and accessibility need manual
   qualification with the owner. Do not automate actual audio/radio/brightness/playback/startup,
   restart Explorer, capture owner windows or move manually placed windows. Media artwork is pending.
4. Investigate the moving radial overlay in synthetic panel captures without changing global feedback
   settings. Preserve S-025 UIA negative evidence: six native accessibility cases now pass once, but
   earlier intermittent fallback/disconnect behavior has no proven causal fix. Keep strict assertions.
   Foreign side AppBars, physical DPI/monitor changes/removal, reveal/resume and crash recovery need
   separate exact-revision desktop qualification. Windows10 was not tested; an owner waiver is not a pass.
5. T20 diagnostics export, topBar.openWindowsFlyouts preference routing, signed production update/
   rollback, Q-001 performance, Q-002 branding and GAP-010 remain open. No current new setup package,
   manifest signature, install or release is qualified. The eventual second delivery is Alpha2;
   reconcile final version/package naming only after the owner approves delivery and lifts D25.
6. Update canonical English Markdown at every checkpoint. The reusable prompt works for Claude and
   Codex without proprietary tools. Never include private receipts, account/media data, captures,
   keys/locations, PDB paths or old private history in commits. No publication is authorized now.

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

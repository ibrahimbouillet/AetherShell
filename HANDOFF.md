# Handoff

> **PUBLIC REPOSITORY — READ THIS FIRST.** Never commit credentials, private keys or their locations,
> personal addresses/accounts/paths, machine names, unrelated app content or captures outside the
> project's own isolated test windows. Public owner attribution: Ibrahim Bouillet. See AGENTS.md.

## Latest session — Claude, 2026-10-18, about 15:55–16:40 CEST
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

## NEXT SESSION START HERE
1. Follow `docs/SESSION_START_PROMPT.md`. Check that `git remote -v` shows the official repository: this
   checkout is the only one for development. The original checkout is a local history archive (GAP-012).
2. The V2 work (Codex) and Claude's T21 fixes and documentation are committed **locally** as one
   commit, "Add reversible taskbar replacement with recovery without Aether Shell running". It is one
   commit ahead of `origin/main`, followed by a small handoff commit.
   - Before the commit: a privacy check of every changed or new file and the four changed screenshots.
   - **Not pushed:** the owner asked not to push yet. Pushing publishes the source and runs CI; do it
     only when the owner agrees.
3. Finish the V2 gate described in "V2 continuation" below. The Release /WX build and the 480 unit
   cases pass (S-015/S-016), so V2 step 1 needs only the integration/native suites. Run them one at a
   time on a quiet desktop. Then complete the `src/app/ShellApplication.cpp` review (V2 step 2), run
   the package privacy audit, and push and confirm CI when the owner agrees.
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

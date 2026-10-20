# Session start prompt — paste this at the start of every AI session

Use this same English prompt for Claude, Codex or another coding assistant. Progress, exact branches,
worktree locations and live verification state belong in `HANDOFF.md`, not in this reusable prompt.

The owner pastes the text below (everything under the line) as the first message of every new
session, whichever assistant takes over (Claude, ChatGPT/Codex or another). It is deliberately
general: the facts it relies on live in the repository files it names, so it never goes stale.
If you change the reading order or the file responsibilities, update this file and `AGENTS.md`
together.

---

You are continuing the development of **Aether Shell**, an existing software project that is built
in alternating sessions by different AI assistants (Claude, ChatGPT/Codex and possibly others) for
its owner, Ibrahim Bouillet. Another assistant worked on it before you and may have stopped in the
middle of a task. Your job in this session: find out exactly where it stopped, check what it did,
continue the work toward the project objective, and leave the project in a state that the next
assistant can resume without your memory.

An interruption or an exhausted usage quota is not evidence that a task finished. Inspect saved
branches, linked worktrees, staged and unstaged changes, untracked files and pending merges. Preserve
them all, including another assistant's partially implemented or unverified tests. Work toward a
coherent verified milestone; do not stop at documentation or planning. Update the canonical handoff
at every meaningful checkpoint so an unexpected limit does not lose the exact point reached.

The owner explicitly uses alternating Claude and Codex sessions. Their private tools and chat
histories are optional; the English repository Markdown is their shared continuity mechanism. Do
not install assistant-specific skills merely to recreate another assistant's environment. If a tool
is useful, verify it is actually available and keep its local configuration out of public history.

## The project objective
Summary only; the authoritative text is `PROJECT_SPEC.md`, sections A and B. If this summary and
that file disagree, the file wins; tell the owner about the difference.

Aether Shell is a native Windows 10/11 desktop interface written in C++20 (Win32, Direct2D/DirectWrite,
Windows.UI.Composition). It runs beside Windows Explorer and adds exactly two persistent desktop
surfaces with an original glass design:
- a floating **bottom Dock**, with application icons only: launch/activate, search entry, pinning and
  reordering, running indicators, fluid interruptible animations;
- a floating **Top Status Bar**: real clock/date, battery, input language, network, volume, microphone
  where supported, and optional weather with explicit failure states.

They are supported by application search, a settings window, a secure update system based on official
GitHub Releases (signed manifest plus SHA-256, a dedicated updater, transactional install with
rollback), a per-user installer with repair and uninstall, accessibility, real automated tests and
honest documentation. The goal is a reliable, beautiful, lightweight and accessible product that meets
the Version 1 definition of done in `PROJECT_SPEC.md`: real software, not a prototype or a mockup.

## Step 1 — Make sure you are in the right place
- The official repository is public: https://github.com/ibrahimbouillet/AetherShell. Work only in the
  local checkout whose `origin` remote is that repository (`git remote -v`). If your working copy has
  no remote or a different one (for example an older local archive copy), stop and tell the owner
  before you change anything.
- Run `git status`, `git log --oneline -10` and `git branch -vv`, and note every uncommitted change.
  Uncommitted work was most likely left by the previous assistant: never discard, reset, overwrite or
  "clean up" it. If files changed in the last few minutes, another session may still be running; ask
  the owner before you edit those files.

## Step 2 — Read the handover and the project, in this order
1. `AGENTS.md`: the shared rules for every assistant (public-repository privacy rule, non-negotiable
   constraints, build and test commands, Git rules).
2. `HANDOFF.md`: what the previous session did, verified and left unfinished. Follow its section
   **NEXT SESSION START HERE**.
3. `PROJECT_SPEC.md`: the objective (sections A and B), the requirements `FR-xxx`/`NFR-xxx` with
   their acceptance criteria, the definition of done (G) and the open questions (H).
4. `TASKS.md` (the backlog, linked to requirement IDs) and `DECISIONS.md` (decisions `Dxx` with
   their approval status).
5. Only when needed: `PROJECT_CONTEXT.md` (architecture and file map), `docs/knowledge/TEST_STATUS.md`
   (executed evidence), `docs/modules/<module>.md`, `docs/decisions/`.

Then read the source files you will change. Use targeted searches instead of loading whole
directories.

## Step 3 — Check the previous assistant's work before you build on it
Another assistant's statement that something is done is not evidence. For the work listed in
`HANDOFF.md` and for every uncommitted change:
- compare the claims with the actual diff and commits;
- build and run the relevant tests (commands in `AGENTS.md` and `BUILDING.md`), unless reliable
  evidence for exactly the same code already exists;
- check the result against the acceptance criteria of the linked requirement IDs;
- record each item as Not started, Implemented, Tested, Verified, Blocked or Unverified (definitions
  in `PROJECT_SPEC.md` section G), with the command, revision and result;
- fix defects that fall within your task, and record regressions, unsupported claims and deviations
  from the specification in `HANDOFF.md` and `TASKS.md`.

## Step 4 — Continue the work where it stopped
- Take the next step named in `HANDOFF.md`; otherwise the highest-priority unfinished task in
  `TASKS.md`. A new instruction from the owner in this session takes priority: record it.
- Work in small, complete, testable slices: implementation, error handling, tests, build integration
  and documentation together.
- Stay within the objective and the requirements. Never silently change a requirement, the
  architecture or an approved decision. If the code contradicts the specification, treat it as a
  defect, or write a proposal and ask the owner.
- Ask the owner only about decisions that are really theirs (scope, priorities, publication,
  accounts, credentials, money), and keep working on everything else in the meantime.
- Never claim that something builds, passes, works or is published unless you ran or confirmed it
  yourself.

## Step 5 — Document everything you do, as you go
Usage limits can end a session without warning. Update the documentation at every meaningful
checkpoint, not only at the end:
- `TASKS.md`: the progress of each task you touched, with its requirement IDs.
- `docs/knowledge/TEST_STATUS.md`: every check you ran (command, configuration, revision and whether
  the tree was dirty, date, result and counts), plus what you could not run and why.
- `DECISIONS.md`, plus an ADR in `docs/decisions/` when the decision is significant: each decision
  with its rationale, alternatives and approval status (proposed, owner-approved, superseded).
- `PROJECT_SPEC.md`: only the traceability/evidence parts and section H, unless the owner approved a
  requirement change.
- `docs/modules/<module>.md`, `PROJECT_CONTEXT.md`, `CHANGELOG.md` and the user guides when behaviour
  changes.
- `HANDOFF.md`, before you stop: the date and time, what you changed (files), what you verified
  (commands and results), what is not verified, the Git state, blockers, questions for the owner, and
  a precise **NEXT SESSION START HERE** list that another assistant can follow without your memory.

## Rules that always apply
- The repository is public. Never write credentials, tokens, keys or their locations, e-mail
  addresses, personal paths, machine names, private screenshots or content from the owner's other
  files into code, documentation, tests or commit messages.
- Never replace `explorer.exe`, edit the Winlogon `Shell` value, disable security features, request
  administrator rights silently or create hidden autoruns. Never fabricate data or test results.
- Automated tests must not change real settings, the work area, audio, startup entries or the native
  taskbar, must not launch applications, and may capture only the project's own test windows.
- Git: commit only verified, coherent changes with the configured owner identity. No force-push, no
  history rewriting, no destructive resets. Push only as the owner has authorized. Publishing a
  release or changing repository settings always needs the owner's explicit approval.
- Check current delivery holds in `AGENTS.md`, `HANDOFF.md` and `DECISIONS.md` before any push, tag
  or release. A later owner hold supersedes an earlier standing publication or push authorization;
  permission to make local commits does not lift that hold.
- If you cannot verify something, say so plainly and record what evidence is missing.
- Reply to the owner in the language they write in; everything in the repository stays in English.
- Keep private screenshots, browser profile/account data, machine-specific paths and local assistant
  state out of commits, logs intended for publication, release assets and this prompt. Before any
  authorized public push or release, review exactly the source/history/assets being published.

## End of session
Give the owner a short report: what you implemented, the files you changed, the builds and tests you
ran with their real results, what you checked visually, what remains unverified, the decisions or
actions you need from them, and the next recommended task. `HANDOFF.md` must say the same thing.

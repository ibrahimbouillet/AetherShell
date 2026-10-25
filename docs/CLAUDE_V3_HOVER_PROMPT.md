# Aether Shell — hover previews and deliberate running-marker motion

You are continuing an existing native Windows application. Implement this focused V3 batch; Codex will independently review your changes afterward. Read, inspect the real implementation, implement, validate, and leave a precise handoff. Do not stop at a plan.

## Establish the actual starting point

Work only in the clean checkout whose origin is https://github.com/ibrahimbouillet/AetherShell. The original development checkout is an archive. Verify origin, HEAD, staged/unstaged/untracked work before editing; preserve existing work.

Read AGENTS.md, HANDOFF.md including NEXT SESSION START HERE, PROJECT_SPEC.md FR-033/FR-034, TASKS.md T42/T43, D38 in DECISIONS.md, docs/specs/v3-design-and-interaction.md, docs/modules/dock.md, and TEST_STATUS S-037/S-038.

At this handoff, implementation HEAD was 35746c8:
- 2e21ff4 added a first running-marker width spring: 5-DIP dot to 12-DIP hover/foreground pill.
- 35746c8 added an unwired owned-window corner experiment. Leave it unwired in this batch.
- These V3 changes were built/tested but NOT installed over the owner's Alpha 2. Identify the running executable/version before interpreting what is visible. A screenshot or source commit alone does not establish which binary is running.
- T43 real hover previews are unimplemented; the Dock still displays a name tooltip.
- Previous test receipts are historical evidence, not proof of your forthcoming changes or owner acceptance.

Inspect DockWindow.cpp, DockMotion.*, the application/window model, TooltipWindow, native surfaces and animation/scheduler infrastructure. Reuse working native code. This batch is T42/T43 only: no corner rollout, Top Bar redesign, installer changes, capture-engine work or release.

## Exact requested interaction

Use two genuinely running applications: A = ChatGPT in the foreground; B = Chrome in the background. These are examples, not hardcoded application special cases.

1. At rest, A has the wide active marker; B has its small running dot.
2. Hover B: smoothly grow B's dot to EXACTLY the same width as A's marker. A remains active and wide. Hover must never activate B or falsify foreground state. Two wide markers are valid here; only the real foreground application gets active styling.
3. After a deliberate dwell, replace B's name-only tooltip with a real miniature preview of its selected window. Use a landscape card above its Dock icon, rounded corners and Aether's neutral translucent material. Preserve the source aspect ratio without stretching/cropping important content. Show actual window content, not a fabricated image or decorative placeholder presented as live content.
4. Put a clearly usable X at the card's upper right. The pointer must be able to travel from the icon into the preview and onto X without dismissing it. Treat the icon and its preview as one hover interaction, including a short safe crossing grace if necessary. Keep B's marker expanded during this interaction. Do not create a large invisible input region over other applications.
5. If the pointer leaves both B and its preview without activating B, dismiss the card and smoothly shrink B's marker to its running dot. A remains wide. Apply this consistently to every running app/profile.
6. Clicking B's Dock icon or preview body activates the represented real window through the existing activation path. On OBSERVED foreground transfer, A's marker contracts while B's expands, concurrently and continuously. If hover already expanded B fully, keep it full: do not force an artificial shrink/grow cycle to manufacture a second animation. If activation fails, keep the truthful foreground state.
7. X requests NORMAL closure of that SELECTED window only. Never kill the process, close all windows in the group, or bypass an unsaved-work dialog. A refusal/cancelled close is not success. Validate the window identity before acting; do not let a stale preview close a replacement HWND or sibling window.

For groups with multiple windows, expose an unambiguous selection or individual previews. The X must always correspond to the content shown. A closed pinned app retains normal icon hover but has no running marker, fake preview or close button.

## Motion direction

The owner finds current transitions too fast. Make expansion/contraction perceptibly slower, calm and readable at normal playback speed. A starting target around 300–450 ms, or equivalent spring settling, is a tuning proposal rather than an approved fixed duration.

Use a dedicated marker-motion token/preset rather than globally slowing all icon magnification. Preserve width, position and appropriate momentum on rapid hover reversals, activation, reorder and model updates. Never snap to an endpoint or restart from zero mid-transition. Keep the already appreciated icon rise/magnification. Avoid flashing, exaggerated bounce, tile backgrounds, directional glow and decorative lighting.

Animate from elapsed time; use existing C++/Win32/Direct2D/composition primitives. No sleeps on the UI thread, continuous idle rendering, JavaScript UI replacement or unapproved dependencies. Reduced motion/high contrast must remain usable and appropriately immediate.

## Preview engineering and safety

Investigate documented Windows thumbnail facilities against the actual Aether rendering architecture. Keep the preview genuinely associated with the correct tracked window/profile. Handle minimized, protected, elevated, unavailable or destroyed windows honestly; never activate a window merely to obtain preview pixels. Cancel stale dwell callbacks and release resources on dismissal, source destruction, DPI/display changes, suppression and shutdown.

Provide keyboard access, Escape dismissal, accessible preview/X names and focus behavior. Preserve no-activate hover behavior; accessibility interaction must not silently switch the underlying application.

Preview pixels, titles and profile details are transient private data: no default logs, persisted screenshots or public fixtures. The owner's supplied screenshot contains unrelated content and must not enter Git.

Media play/pause remains a recorded T43 follow-up; this first implementation batch prioritizes previews, selected-window X and motion. Do not claim all of T43 or V3 complete.

## Verify and leave a reviewable result

Add meaningful deterministic tests for the indicator/preview state machine, interrupted motion, dwell cancellation and stale identities. Use owned native test windows for real preview association, selected-window close, refusal, cleanup, input bounds and DPI behavior. Tests must not close owner applications or alter real settings.

Acceptance sequence:
A foreground -> hover B -> B grows while A stays active -> dwell shows B content/X -> enter preview -> leave without clicking -> B shrinks -> hover B again -> activate B -> A contracts and B remains/grows wide -> close B through its preview X.
Also cover rapid A/B switching, multiple windows, destruction during dwell, close cancellation, reduced motion/high contrast, and idle frame cessation.

Build the integrated app and focused affected tests using scripts/build.ps1, Release /W4 /WX. Inspect a normal-speed runtime preview before calling motion polished. Do not replace the usable Alpha 2 with an unqualified candidate. Any temporary candidate must use an isolated local profile without startup registration and leave the usable installation available afterward. D36 requires an explicit fresh ready reply before each real desktop recording; isolated own-window tests are separate.

Keep scope and resource usage focused: reuse the current architecture, run relevant checks, and avoid unrelated rewrites or repeated broad test matrices. Ask only for genuinely missing information; otherwise make reversible implementation choices and record them.

Update the English HANDOFF.md, TASKS.md, TEST_STATUS and affected requirement/module records. Make coherent verified LOCAL commits, preserving unrelated work; no push, tag or release for this batch.

Finish with exact commits/files, executed commands/results, what was visually inspected, remaining limitations, and reproducible safe preview steps for Codex's independent review. Distinguish implemented, tested, observed at runtime, and accepted by the owner.

<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Ibrahim Bouillet -->
# Claude implementation prompt — stability, touch and welcome

Owner assignment, 2026-10-28: D51, T50–T52, FR-040/FR-041. Codex audits/prepares; Claude implements.
“V4” names the next development batch, not an approved version string or publication.

Read AGENTS.md, HANDOFF's latest checkpoint/NEXT SESSION, applicable PROJECT_SPEC/TASKS/DECISIONS
entries, then [the source audit](specs/v4-stability-audit.md). Its 26 entries provide exact source
references, correction/regression proposals, evidence limits and the complete acceptance matrix.
Use it as the initial worklist; investigate beyond it. Read module details only as needed. Preserve
the one shared checkout, existing work and usable installed Alpha 3.

## 1. Stability and startup first

The owner reports maximized browser/Electron pages becoming blank/grey or unresponsive, especially
when hiding both bars expands maximized windows; recovery reportedly required reboot. Sign-in
startup is unreliable too. Reproduce these release blockers; source findings are not diagnoses.

Start with AS4-01–AS4-08/16–22: unresponsive-target region writes, corner-helper health/recovery,
fullscreen classification, reservation/settings churn and startup/profile/supervisor defects.
Use disposable separate-process owned windows with changing content and delayed message handling;
exercise every Auto-Hide combination, maximize/restore/fullscreen and helper failure. Trace bounded,
sanitized stage timings and mutation counts, then qualify real rendering families with the owner.
Isolate slow targets; preserve native captions/input and exact conditional restoration. Unsupported
effects must fail open. Do not mask failures with browser resets, global GPU changes, elevated
priority, arbitrary sleeps or restart loops.

Correct source-established defects with discriminating regressions. Continue reviewing event
storms, stale identities/callbacks, device loss, lock/sleep, DPI/Explorer, shutdown, resource growth
and update/IPC/path trust boundaries. Classify each entry as reported, reproduced, source-established,
hypothesis, fixed or unverified. Preserve signature/hash/rollback protections; do not call reliability
bugs exploitable vulnerabilities without evidence.

## 2. Complete touch and scroll physics

Deliver FR-040 across forms, long menus, Search, Quick Settings, Dock/previews and hidden-bar access.
Use shared native pointer/manipulation handling with pointer identity, capture/cancellation and
promoted-mouse deduplication. Handle `WM_POINTERCAPTURECHANGED`/`WM_CANCELMODE`; arbitrate
tap/pan/hold/slider/native text selection. A pan or canceled contact must invoke zero commands.
Retain mouse, pen, precision touchpad, keyboard, UIA, IME and atomic editor presentation.

Content follows the finger directly. Fling uses timestamped release velocity, decelerates and can
be regrabbed/reversed continuously; bounded elastic displacement returns with a damped spring.
Keep logical/UIA offsets valid, bound elapsed-time integration and stop frames at rest. Do not
require pressure sensing, add inertia to sliders, or double touchpad momentum. Reduced motion
retains direct tracking while suppressing decorative fling/bounce. Fix AS4-11–15's press/release
and delta-loss defects. Every long menu/form must reach its last item by physical touch without
accidental actions; test the full cancellation/bounds/input/DPI matrix in the audit.

## 3. Handwritten sign-in/unlock welcome

Implement FR-041/ADR 0013 on the authenticated user's normal desktop, once per sign-in/unlock.
Keep Windows authentication/secure desktop intact. Use current-session notifications, bounded
registration retry and unlocked-desktop readiness; coalesce duplicates and cancel on re-lock.
Respect disclosed startup, Task Manager disable and intentional Quit; unlock never restarts services.

Setup/onboarding offers an explained, optional local display name, with skip and Settings edit/delete.
No account, account-name scraping, transmission or name logging. Support French `Salut {name}` and
English `Hello {name}`, following the chosen language or an explicit greeting preference.

With motion enabled, create original continuous handwriting with a moving stroke tip, natural speed variation and pen
lifts. Fade/typewriter/font-outline tracing alone fails acceptance. Shape/layout variable names,
accents and long text with a readable unsupported-script fallback. Prepare essential shell surfaces
concurrently and measure their first successful presents; optional network/catalog/weather never
gate completion. Dismiss after writing and essential readiness, without extra artificial delay or
truncating handwriting on fast startup. Provide immediate touch/mouse/Escape skip, an independent
hard dismissal deadline, reduced motion/high contrast and graphics-failure fallback. No input trap
or stacked greetings; keep desktop/recovery usable. macOS remains future direction only.

## 4. Verification and handoff

Run focused Release `/W4 /WX` regressions, combined non-network units and affected owned native
suites in `build/wf-<name>`. Keep failures/intermittents visible. Measure first-usable startup,
unlock, touch latency, frame/UI p50/p95/worst case, helper latency, idle CPU/memory and resource
growth on a stated configuration; Q-001 global budgets remain open. Inspect continuous owned-window
motion, then coordinate real-app, physical-touch, reboot/unlock and owner acceptance separately.
Mouse injection/still images/unit success cannot substitute for those checks.

Update the audit issue states, requirements/tasks/decisions, TEST_STATUS and HANDOFF with executed
evidence and remaining limits. D36 requires a fresh ready reply for each real-desktop capture;
never reboot/lock or close owner apps unattended. This brief requests implementation, not installed
replacement, commit/push, tag or release. Do not claim exhaustive bug freedom or security certification.

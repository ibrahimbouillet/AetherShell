# V3 continuation prompt

Continue Aether Shell in the clean public checkout whose origin is
https://github.com/ibrahimbouillet/AetherShell. Read AGENTS.md, HANDOFF.md including NEXT SESSION
START HERE, PROJECT_SPEC.md FR-031–FR-036, D38 in DECISIONS.md, TASKS.md T39–T44, and
docs/specs/v3-design-and-interaction.md. Inspect staged, unstaged and untracked work before editing.

The owner accepted the published Alpha 2 and authorized V3 development, primarily documenting this
new scope during the previous assistant's limited remaining usage. Preserve the running installed
Alpha 2; do not replace it merely to test a source patch. Do not republish Alpha 2, move its tag, or
publish V3 without new release authorization. This first batch does not finish V3.

First finish the empty-margin input fix and its verification boundary recorded in HANDOFF. Native
window regions must match actual panels/current animated icons; HTTRANSPARENT alone is insufficient
for another application's process. Work-area reservation is a separate rectangular AppBar limitation.
Then continue the compact Dock/default spacing, genuine native Quick Settings/system actions and
rounded icons, real Wi-Fi strength/freshness, dot-to-pill motion, live hover previews with close and
supported media actions, and safe window-motion/corner feasibility in the documented order.
The compact panel and guarded native Quick Settings request now have a first source implementation;
read S-036 before repeating checks. T42 dot/pill width motion now has a tested first implementation
(S-037): observed-running state only, hover/foreground targets, interruptible spring, reduced-motion
and high-contrast snap, existing animated icon center. Physical acceptance/cadence and complete
layout-origin continuity remain open; do not label all V3 motion complete.
Read docs/specs/window-corner-investigation.md for maximized
corners: insets plus DWMWCP_ROUND do not solve Windows' maximized-window policy. Do not silently
restore/resize foreign windows to disguise it. S-038 covers the now-implemented but unwired
MaximizedCornerPrototype helper: hidden own-window geometry/ownership/maximize tests pass, using
synthetic frame/work observations. Next attach an opt-in observer to the owned Settings window,
acquire fresh real DWM bounds and qualify visible caption/shadow/corner behavior. Restore on every
state/disable/exit path; never generalize the hidden tests into a universal rounding claim.

Keep observed Windows data truthful and unsupported behavior explicit. No process injection,
Explorer replacement, silent elevation, private logs/assets in Git, or invented preview pixels.
Tests use fakes/own windows. Ask and wait for an explicit ready reply before EACH real recording
attempt; earlier blanket permission is superseded by D36. Keep captures local until privacy review.
Update the English requirements, tasks, decisions, test evidence and handoff at each coherent
checkpoint. Distinguish source inspection, tests, actual runtime verification and owner acceptance.

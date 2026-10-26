<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Ibrahim Bouillet -->
# Claude implementation brief — Settings that feels as fluid and finished as the Dock

Owner direction recorded on 2026-10-26. Scope: FR-037 / FR-015 / NFR-002 / NFR-006 / NFR-007 /
NFR-011, T47, D41–D43. This is an implementation assignment for Claude. Codex prepared this brief
and inspected source; it has not implemented or reproduced the new motion fixes.
The owner's subsequent Quick Settings placement question is a separate FR-035/T41 feasibility
follow-up in section 14; preserve the existing native-panel preference while investigating it.

## 1. Your mission and the owner's actual feedback

Continue the existing Aether Shell implementation. Make Settings feel as responsive, coherent and
carefully animated as the existing Dock. Implement, measure, watch the result, correct it and leave
reviewable evidence. A recolour, a longer easing curve, a successful compile or attractive resting
screenshots will not satisfy this assignment.

The owner has tried the previous private prototype and rejects its current Settings quality:

1. While scrolling, the page appears to break into moving pieces, deform and reassemble when the
   gesture stops. The page must remain visually complete **during** movement, not recover afterward.
2. Too many actionable icons/rows feel inert under the pointer. Feedback must begin on hover, then
   respond naturally to pressing, releasing, leaving, selecting and keyboard focus.
3. Some state changes jump directly to the destination. Visual transitions must be continuous,
   interruptible and quick, without delaying the real action or losing native input behavior.
4. The native title strip has a different apparent colour/material from the translucent content.
   The top frame, navigation, content and footer must read as one deliberate Aether window.
5. Shapes, glyphs, text and pages must appear quickly, remain crisp and avoid visible reconstruction.

The owner's reference is the **current Dock's quality of response**, particularly pointer-following
magnification and continuous expansion/contraction. Preserve that Dock. Settings does not need
Dock-sized zoom on every control: translate its continuity and responsiveness into suitable form
interactions. Apple is a quality reference for restraint, continuity and material hierarchy; Aether
remains an original native Windows product with its own neutral frosted-grain identity.

The private supplied image establishes the visible caption/content mismatch. A still cannot prove
or disprove the scrolling defect. Keep that distinction throughout your diagnosis and reporting.
Do not put the image, its desktop surroundings or its local filename in the public repository.

## 2. Recover the project before changing it

Use the **one shared official AetherShell checkout**. Verify `origin`, branch, HEAD and
staged/unstaged/untracked changes. Do not create a clone or worktree, reset existing work, clean
untracked assistant files, rewrite history or develop in a recovery archive. Use disjoint ownership
if delegating; do not run independent GUI/capture suites at the same time.

Read, in order:

1. `AGENTS.md`, `HANDOFF.md` including NEXT SESSION START HERE.
2. `PROJECT_SPEC.md` FR-004/FR-015/FR-022/FR-037 and NFR-001–NFR-011; `TASKS.md` T47;
   `DECISIONS.md` D26/D27/D38–D43; `docs/decisions/0011-native-form-presentation.md`.
3. This prompt, then `docs/CLAUDE_V3_VISUAL_UNIFICATION_PROMPT.md` for the broader owned-surface
   scope. This follow-up supersedes its historical Settings starting facts and weak motion proof;
   it does not cancel installer/maintenance/updater coherence or safety requirements.
4. `docs/specs/v3-visual-unification-inventory.md`, `docs/modules/ui_form.md`,
   `docs/modules/settings.md`, relevant graphics/UI module notes and TEST_STATUS S-049/S-050.

Historical starting evidence, **verify against the current tree**:

- Source commit `2e4ff0d` introduced/reviewed shared forms, glass menus, redesigned Settings,
  setup/maintenance, updater and confirmations. `5f07b6a` records its final handoff.
- Release `/W4 /WX`, 24 unit executables and final scoped runs of 11 affected native executables
  passed. The first native aggregate failed Dock fixture entrance timing; its corrected full suite
  passed separately. No uniformly green aggregate rerun was claimed.
- Sixteen Settings and 32 setup/updater stills were inspected. That did not establish continuous
  scrolling, every hover state, physical DPI, active Acrylic on inactive forms or owner acceptance.
- Installed Alpha 2 remains distinct from the private prototype. The prototype intentionally still
  reports `0.2.0-alpha.2`; no V3 version, installation, tag or release is authorized.

Read the latest ignored runtime receipt and verify actual executable identity only if needed. A
running process and a previous screenshot are not evidence that the latest build is on screen.
Preserve the useful current trial, its recovery host, five genuine pins and installed settings.

## 3. Design references and how to use them

These are principles to interpret for Aether, not API/dependency choices or permission to copy art:

- [Apple HIG — Motion](https://developer.apple.com/design/human-interface-guidelines/motion):
  use purposeful, brief feedback that follows the interaction; allow interruption and respect
  reduced motion. Additional decoration must not slow a frequent task.
- [Apple HIG — Materials](https://developer.apple.com/design/human-interface-guidelines/materials):
  material serves hierarchy and legibility. Keep navigation, controls and content distinguishable
  without stacking excessive glass effects. Retain Aether's existing material rather than copying
  Liquid Glass, SF Symbols, Apple assets or platform-specific effects.
- [Microsoft Fluent 2 — Motion](https://fluent2.microsoft.design/motion): consistent natural motion
  communicates relationships; larger transitions should remain quick and purposeful. A restrained
  top-level fade is a useful starting point; a form should not fly around on every navigation.
- [Microsoft — Title bar design](https://learn.microsoft.com/en-us/windows/apps/design/basics/titlebar-design):
  blend the title region with the window while preserving window identity, caption actions,
  dragging, system-menu and maximize/restore behavior.
- [Microsoft — Custom window frame using DWM](https://learn.microsoft.com/en-us/windows/win32/dwm/customframe):
  documented frame integration requires correct non-client sizing and hit testing. Retain DWM
  caption handling where feasible, plus complete resize and system-command behavior.
- [Microsoft — Mica in Win32](https://learn.microsoft.com/en-us/windows/apps/desktop/modernize/ui/apply-mica-win32):
  Mica is an opaque wallpaper-based material. Do not label it live transparency or introduce
  Windows App SDK/runtime dependencies merely because an example uses them.

The specific timing and measurement targets below are Aether engineering proposals for this batch,
not numbers prescribed by these sources and not newly approved universal release budgets.

## 4. Diagnose the actual rendering problem first

Inspect these integration points before choosing a solution:

- `src/ui/form/FormScrollView.*`, `FormSurface.*`, `FormPaint.*`, `FormInternal.h`.
- `src/settings/ui/SettingsWindow.*`, especially BuildPage, Layout, page drafts, scroll and focus.
- `src/graphics/Theme.*`, `Animation.*`, `Transition.*`, `FrameScheduler.*`, `TextCache.*`,
  `GlassPainter.*`; app theme/motion/settings routing in `src/app/ShellApplication.cpp`.
- `tests/integration/ui_form/FormIntegrationTests.cpp`, `FormTestSupport.h`,
  `tests/integration/settings_ui/SettingsDesignTests.cpp` and existing functional Settings tests.
- The actual Dock's motion code only as a reference; do not globally retune it.

Source facts inspected at preparation, not yet a proven runtime root cause:

- `FormScrollView::Apply` moves a native canvas HWND using `SetWindowPos(...SWP_NOCOPYBITS)` and
  queues `RedrawWindow(...RDW_INVALIDATE | RDW_ALLCHILDREN)` for its viewport.
- Form regions paint separately through buffered DIBs and a Direct2D DC target. Cards/dividers map
  canvas coordinates back to the form. Native fields/lists/documents can be independent opaque
  layered child HWNDs. Shared scene mathematics does **not** imply an atomic composed frame.
- The DC target currently tries software rendering first. Profile its actual cost; blindly forcing
  a hardware DC target can introduce readback and is not a demonstrated fix.
- Hover/press/switch tweens, a navigation indicator, page fade and scheduler routing already exist.
  Navigation/list row hover is passed to the painter as a discrete 0/1 value; field wells do not
  receive the same hover amount. Glyphs generally inherit colour and fixed geometry.
- BuildPage destroys the outgoing page's native controls, creates the incoming page and invokes
  FadeIn. That is not automatically a two-page crossfade or a coherent transition for every child.
- Existing tweens preserve current value on retargeting; that alone does not prove velocity
  continuity. Existing springs retain velocity. Choose appropriate behavior per interaction.
- Settings creates a normal `WS_OVERLAPPEDWINDOW`. DWM frame extension and caption dark mode are
  set, but the current form path has no complete custom caption/non-client integration.
- Current settled-position checks and resting screenshots do not verify intermediate scroll frames.
- The current theme route can run after unrelated settings changes; FormSurface::SetTheme clears
  text resources, recreates fonts/brushes and reapplies frames. Check redundant theme/resource
  rebuilding and asynchronous relayout while the user is interacting; do not assume every theme
  notification represents an actual material/DPI/text-scale change.

A mixed native-child/background presentation during scrolling is a **hypothesis**. Reproduce and
instrument it before asserting that it is the cause. Also investigate clipping, coordinate/DPI
rounding, invalidation ordering, full-scene traversal per child, layered-child repositioning,
background erase, page fade, nested native document scrolling and resource creation during paint.
Change the rendering strategy where evidence requires it; do not treat an implementation detail in
D42 as a guarantee that it already meets FR-037. Record a focused ADR amendment if that strategy
changes, preserving the native-input and accessibility objectives.

Do not hide the defect with more blur, darker tint, delayed repaint until release, a longer tween,
always-on reduced motion, repeated synchronous `UpdateWindow`/`DwmFlush` calls, blanket
`WS_EX_COMPOSITED`, a continuous busy render loop or disabling controls during animation.

## 5. Non-negotiable visual and interaction criteria

All criteria apply in the supported normal mode; document intentional accessibility/OS fallbacks.

| ID | Required result | Evidence that can establish it |
|---|---|---|
| SF-01 | During scroll, every card, text line, glyph, border and child control stays registered to its content; no strips, detached pieces, holes, stale pixels, stretch, duplicate text or release-time reconstruction. | Guarded moving-window sequence, reviewed while moving and frame by frame; injected known-pattern alignment checks. |
| SF-02 | Wheel bursts, reversals, keyboard scrolling and available scrollbar/precision-touchpad routes respond without fighting the gesture, accumulating a delayed tail or jumping back. | Continuous input traces plus visible frames and final-range checks for each genuinely supported route. |
| SF-03 | Every enabled actionable icon, row and control responds to hover and press/release; disabled states remain honest; keyboard focus is distinct from hover/selection. | Per-control state matrix and interruption sequences, including pointer entry/exit and capture loss. |
| SF-04 | State, selection and page transitions preserve continuous visible state, are retargetable and never require waiting before the next action. | Rapid A→B→C→A navigation, toggle reversal, enter/leave/re-enter and open/close/reopen at intermediate progress. |
| SF-05 | Caption/top frame, sidebar, content and footer share a coherent neutral glass system, including activation changes. No unrelated opaque caption strip in supported active glass mode. | Whole-window owned-backdrop captures including non-client pixels, active/inactive and dark/light states. |
| SF-06 | Text and native editors remain crisp, readable, correctly clipped and operable throughout; no fake live controls made from snapshots. | Native caret/selection/clipboard/keyboard/UIA checks, intermediate-frame review and actual IME walkthrough where available. |
| SF-07 | First show and page reuse draw a coherent complete first visible frame without progressive blank boxes, glyph pop-in or repeated geometry rebuilding. | Cold/warm timing and captured first-frame sequence with cache/resource diagnostics. |
| SF-08 | Input targets follow visible controls; decorative movement does not move neighboring layout or cause a target to dodge the pointer. | Native hit testing during hover/press/page motion and resize/DPI changes. |
| SF-09 | Reduced motion, high contrast, transparency off and unsupported OS paths remain readable, responsive and fully functional. | Policy switches during active feedback/scroll/page transitions; explicit native/opaque fallback checks. |
| SF-10 | Settled/hidden/minimized Settings requests no animation frames; caches and native resources are bounded and released. | Scheduler/resource lifecycle checks after settling, reopen and repeated navigation. |
| SF-11 | Native window commands, consent, drafts, persistence, real provider data and recovery remain correct. | Existing functional gates plus targeted frame/input regressions; separate actual runtime identity. |

Any reproducible SF-01 defect blocks this batch regardless of FPS averages or attractive stills.
Do not average away a broken state with a visual score or describe it as acceptable because a unit
test passed. Diagnose shared-form fixes against setup, updater and confirmations as well.

## 6. Scroll and rendering architecture expectations

Treat scroll as a coherent translation of content within a stable viewport. Keep window material,
grain origin, clipping boundaries, navigation and footer stable. The moving content's cards,
decorators and native controls must use the same effective offset and DPI rounding for each
presented state. No content is allowed to bleed into the caption or footer.

Choose and prove a compatible native strategy. It may coordinate native-child layout and repaint,
use a cached background scene, change invalidation partitioning or revise the shared presentation.
Do not assume that buffering each HWND separately solves cross-HWND presentation. Do not replace
native editors with painted text or copy the Dock's NOREDIRECTIONBITMAP host into Settings.

Avoid layout/control creation, font/geometry construction, repeated text measurement and unnecessary
whole-form traversal in the per-frame hot path. Reuse bounded shared resources keyed correctly for
device, theme, DPI and text scale. Invalidate/rebuild when those inputs change; never keep stale
resources merely to improve a benchmark. Handle device loss and a failed cached path visibly.

Keep real wheel delta semantics and rapid reversal. Direct scrollbar dragging should track the
pointer without decorative lag; keyboard and focus reveal must remain predictable. Handle the
About licence editor and other nested native scrollable controls: the control under the pointer
gets its expected native behavior; forwarding must not double-scroll parent and child. Do not claim
precision-touchpad coverage by sending only synthetic wheel messages.

Snapshots may assist a brief transition only if they remain private, derive solely from Aether's
own UI, preserve correct live input/accessibility, and do not hide real control state. A screenshot
that looks interactive while the genuine editor/caret is absent is not a valid rendering fix.

## 7. Build a complete, restrained interaction vocabulary

Audit actual controls on **all eight pages**: Dock, Top Bar, Weather, Updates, General, About,
Desktop and Appearance. Maintain a control/state checklist rather than assuming one styled button
proves every icon works. Include navigation glyph+label rows, switches/checks, push/icon buttons,
links, combos, sliders, numeric fields, pin-list actions, reset/apply, progress/retry and caption
buttons. Mark purely decorative logos and status glyphs as noninteractive; do not invent actions
or a misleading pointer cursor just to animate every graphic.

Use the whole actionable row as a coherent feedback target when appropriate. Give hover a modest
change in surface/contrast or glyph emphasis, press a brief compression/emphasis, release a smooth
return, selection its own persistent indication and keyboard focus an accessible visible ring.
Effects must be noticeable at normal speed without neon glow, moving glare or large hover tiles.
Keep form text sharp; use a small glyph transform only where it helps and never relayout neighbors.

Suggested starting motion ranges, to tune in actual playback:

| Interaction | Initial tuning proposal |
|---|---|
| Hover arrival/departure | 100–160 ms / 120–180 ms, immediate onset and smooth reversal |
| Press/release | 60–100 ms / 100–160 ms, click action admitted immediately |
| Switch/check visual state | 140–220 ms, state semantics update immediately, reverse from current appearance |
| Navigation selection | 180–260 ms, continuous travel without jumping to a reset origin |
| Page change | 160–240 ms, restrained coherent fade; optional tiny translation only if proven useful |
| Menu/confirmation entry/exit | 140–220 ms / 100–180 ms, no delayed input admission |

Avoid applying all these effects to every event. Use a shared token vocabulary and respect
MotionPolicy. No effect may be the sole way to communicate state. Reduced motion retains immediate
meaningful hover/focus/state contrast while removing unnecessary travel and bounce. Visible focus
must not wait for an animation to finish.

For page changes, coordinate the outgoing/incoming content and native controls as a whole. Do not
let cards fade separately while editors pop in at full opacity. Retarget to the latest destination;
do not queue obsolete pages. Restore each page's drafts/selection/scroll as required without
rebinding focus to a stale HWND or routing a click to a hidden outgoing page. Test navigation while
scrolling and while asynchronous status changes arrive.

## 8. Make the top frame part of the window

The owner explicitly includes the OS-looking strip containing the title and minimize/maximize/
close controls. Styling only the large in-client Settings heading misses the complaint.

On supported active glass systems, carry Aether's neutral tint, blur response and fine-grain
identity through that top region. Keep an appropriate subtle hierarchy between navigation and
content, without a conspicuous opaque cap or doubled tint seam. Busy wallpaper naturally produces
different colours at different locations: identical RGB everywhere is not the criterion. Test the
material response on controlled backgrounds and compare unobstructed patches across the boundary.

Prefer documented DWM/native frame integration retaining system caption buttons where feasible.
If a custom frame is needed, implement its entire contract, not three painted glyphs:

- Normal activation/Alt+Tab identity, accessible title and native minimize/maximize/restore/close.
- Caption dragging in empty regions, double-click maximize/restore, right-click system menu,
  Alt+Space, Alt+F4, keyboard move/size and proper interactive exclusions.
- All resize edges/corners, minimum size, maximize/work-area bounds, DPI changes, multi-monitor
  negative coordinates, restored/maximized layout and Windows 11 Snap behavior where supported.
- Correct DWM non-client processing/hit testing, per-monitor metrics, focus and accessibility.
- Native-compatible caption hover/press/active/inactive states, no overlap with content and no
  transparent input hole over a visible caption action.

Keep Settings an ordinary activatable resizable application window, not a topmost no-activate
tool window. Do not alter foreign windows or the existing reversible corner guard for this task.
Respect system inactive-Acrylic and transparency/accessibility policies; transition the whole
owned frame coherently to the intentional fallback. Do not bypass Windows policy to force glass
or use whole-window opacity, which also fades text. Document Windows 10/older-build limitations.
For custom maximize/restore regions, follow Microsoft's
[Snap hit-testing guidance](https://learn.microsoft.com/en-us/windows/apps/desktop/modernize/ui/apply-snap-layout-menu)
including `HTMAXBUTTON` where appropriate. Do not assume a caption colour value makes it transparent:
[`DWMWA_COLOR_NONE`](https://learn.microsoft.com/en-us/windows/win32/api/dwmapi/ne-dwmapi-dwmwindowattribute)
is documented for border suppression, not as a general transparent-caption switch.

## 9. Measure responsiveness, not only animation callbacks

Establish a baseline and compare the final candidate on the same stated OS build, display refresh,
resolution/DPI and hardware class. Do not record machine names, personal paths or process lists.
Keep raw timing/capture output local. Separate UI-thread work, animation updates, actual composed
frame cadence and recording cadence. A 60-FPS output file or a 16-ms timer proves neither a new
displayed frame nor the absence of torn native-child presentations.

Use these initial **diagnostic targets on a capable 60-Hz reference configuration**, then report
actual results and justified limits; do not silently turn them into universal release requirements:

- Aim at coherent 60-Hz presentation (16.67-ms budget), p95 presented-frame interval <=16.7 ms,
  p99 <=33.4 ms under sustained scroll, and no unexplained >50-ms visible stall in the controlled run.
- Aim at p95 Settings-owned UI work <=8 ms per active frame, separating DWM/wait/capture cost.
- First visible input feedback p95 <=50 ms; first coherent incoming page frame on warm navigation
  p95 <=100 ms. Do not count the chosen decorative settling duration as input latency.
- Measure cold first-show and warm reopen separately. Initial goals for Settings-owned preparation
  are <=500 ms cold and <=200 ms warm, excluding separately reported process startup/provider work.
- After settling, expect zero new Settings animation requests over a ten-second idle observation;
  do not promise zero whole-process CPU while real status providers are running.

Collect at least three comparable active runs of roughly ten seconds, including repeated wheel
bursts, reversal and navigation, plus cold/warm samples. Report sample counts, p50/p95/p99 and worst
case where meaningful; do not publish a percentile from one sample. Label synthetic scheduler
timing, unavailable compositor telemetry and untested high-refresh hardware honestly. If the frame
cadence target cannot be measured with available tools, do not substitute callback FPS as proof.
The mandatory no-tearing criterion remains regardless of whether these proposed budgets are met.

## 10. Prove the result while it is moving

Reproduce the original behavior first with an own-window fixture dense enough to expose it:
cards, multiline text, glyphs, switches, slider, combo, native text field and native scrolling
licence/list. Use known nonprivate text and alignment markers, plus a changing opaque private
test-pattern backdrop. Exercise the real shared form path rather than a new showcase renderer.

Add meaningful regressions that fail for the discovered defect. Retain existing strict geometry,
pixel, UIA, draft and idle assertions. Avoid tests that merely repeat the chosen implementation.

Required sequences:

1. Sustained slow and fast scroll, small wheel deltas/bursts, reversal mid-flight, top/bottom bounds,
   exposed thumb/keyboard routes and nested native document scrolling. Inspect intermediate frames
   before release, not only the final position after a sleep.
2. Hover every actionable control family; leave/re-enter halfway; press-drag-out-release; capture
   loss; keyboard focus; disabled, selected, busy and error states. Verify one action per gesture.
3. Rapid page switching during scroll/fade, returning to saved scroll, editing during theme/status
   updates, switch reversal and opening/closing a combo/confirmation during other feedback.
4. Resize while moving; small viewport; 100/150/200% DPI fixtures and 100/150/225% text scale;
   active/inactive dark/light; transparency off; high contrast; reduced motion switched mid-flight.
   Synthetic DPI tests do not establish a physical multi-monitor transfer.
5. First-show/reopen/page-cache sequences and resource cleanup. Exercise loss/recreation only
   through safe test seams; verify that failure still leaves readable usable controls.
6. Whole-window title-frame tests and native commands from section 8, including caption hit targets
   while hover feedback is active. Include window borders/title in capture coverage calculations.

Capture only the project's test windows fully covered by its opaque private backdrop, with
pre/post coverage checks. Keep sequences and timing raw data ignored/local. Do not capture the
owner's desktop or close/hide their applications to obtain prettier evidence. D36 requires a fresh
readiness question and explicit ready reply before **each** real-desktop capture attempt; earlier
launch authorization does not authorize recording. Continue isolated work without needing that.

Watch at normal speed first, then inspect frame by frame around movement, reversal, clipping edges
and caption seams. Review **every saved frame** for privacy and artifacts. Use a real playback/image
inspection tool; do not describe generated-but-unopened images as visually reviewed. If you cannot
play the sequence, say so and use sequential frame inspection without claiming normal-speed proof.
Recording that perturbs cadence is separate from performance measurement and must be disclosed.

## 11. Preserve the native product contracts

Do not change settings schema, values/ranges, page routes, pin identity/order, migrations, backups,
async durable-save ordering, startup/Task Manager semantics, taskbar replacement or permission
policies for animation. Ordinary live changes and explicit Apply consent remain different. Motion,
navigation and refresh must not apply consent drafts or overwrite input. Keep focus, caret,
selection, IME, clipboard, keyboard navigation, native combo Selection/ExpandCollapse and UIA Scroll.

The native combo dropdown exception remains until a truly equivalent provider/keyboard solution
is designed and proven. Do not replace a combo list with a generic MenuItem popup for appearance.
Maintain real status/update/progress/cancellation semantics and signed-manifest/hash checks.

Shared changes must preserve installer/repair/uninstall/updater/confirmation layouts and independent
operation before installation. Keep their COM lifetime, cancel/retry and progress contrast fixes.
Automated deployment tests use isolated temporary roots, never installed Alpha 2 or real startup
registration. No new third-party dependency, Electron, web view, downloaded fonts, hidden autorun,
elevation, undocumented global hook or Explorer replacement is part of this task.

## 12. Work order and required checks

Proceed in this order, keeping checkpoints in HANDOFF so another interruption loses no work:

1. Record exact starting source/runtime and reproduce the moving-frame defect. State observed
   facts, suspected mechanisms and the evidence that will distinguish them.
2. Prove a continuous-scroll fix on the real hybrid form fixture. Confirm native editors/UIA and
   repaint/latency/idle behavior before extending presentation changes.
3. Finish the control-state vocabulary and page interruption behavior on one dense Settings page,
   then all eight pages. Integrate and qualify the caption without breaking native window behavior.
4. Review dark/light moving sequences yourself and correct defects. Apply shared fixes to other
   consumers only where needed; rerun their regressions rather than assuming compatibility.
5. Run the focused and final integrated gates, audit public text/package freshness where applicable,
   make coherent verified local commits and prepare an identified private candidate.

Use `scripts/build.ps1`; inspect actual target/test names and dependency closure. Example focused
checks (adapt the module filter to changed files; do not omit a changed dependency):

```powershell
./scripts/build.ps1 -Config release -BuildDir build/wf-claude-settings-motion `
    -Modules 'graphics;ui_form;settings_ui' -CMakeArgs '-DAETHER_WARNINGS_AS_ERRORS=ON' `
    -Test -TestRegex '^aether_(graphics|ui_form|settings_ui)_(integration_)?tests$' -TestExclude ''

./scripts/build.ps1 -Config release -BuildDir build/wf-claude-settings-motion-all `
    -CMakeArgs '-DAETHER_WARNINGS_AS_ERRORS=ON' -Test -TestExclude 'network|integration'
```

Run affected native suites sequentially after building their targets, including setup/updater/
confirmation consumers when shared forms change. Check graphics/form/settings functional tests,
window-frame input/UIA, reduced motion, idle and new moving-frame regressions. Build the real app,
launcher and setup, not only a test renderer. Record exact commands, revision, duration, failures,
skips and missing hardware. A filtered pass does not erase an earlier aggregate failure.

Do not install over Alpha 2, push, tag or publish a release. For a final trial, follow the current
HANDOFF's explicit launch authorization and recovery procedure; announce any authorized bounded
private swap. Verify the candidate hash/process/adjacent helpers, private profile, genuine pins,
surfaces and Settings visibility, and unchanged installed settings/startup/binary. Preserve the
recovery host. If authorization for a specific remaining owner action is missing, finish all
independent work and present the concrete candidate before asking for only that action.

## 13. Deliver a reviewable result, with honest boundaries

Update the surface inventory with SF-01–SF-11 evidence, not just a general “tested” label. Update
HANDOFF, TASKS, TEST_STATUS, relevant module docs and the ADR if the rendering/frame strategy
changes. Keep requirement/decision IDs and distinguish: implemented, automated tests passed,
moving frames inspected, normal-speed playback inspected, measured on this configuration,
actual candidate running, owner accepted, unsupported and still unverified.

Your final report must state:

- The reproduced cause and evidence; the precise shared changes that removed it.
- Before/after moving-frame findings, tested control families/pages and caption behavior.
- Actual measured responsiveness/cadence, tools/sample counts and measurement limits.
- Exact build/test results, failure corrections and anything skipped or untested.
- Local commit/source identity and which candidate, if any, is actually running.
- Remaining owner acceptance, physical OS/DPI/IME/screen-reader/clean-machine qualifications and
  any native fallback. Do not convert this task or the old passing suites into full V3 acceptance.

Do not stop at a plan or a resting screenshot. The owner needs Settings to remain one intact,
quick, responsive surface while they use it. If your own playback still shows pieces rebuilding
after a gesture, return to diagnosis before presenting the batch as complete.

## 14. Owner follow-up — can Windows Quick Settings open beside the Top Bar click?

The owner confirms that clicking the Top Bar's grouped Wi-Fi/sound/battery area now opens genuine
Windows Quick Settings, but dislikes its bottom placement. Desired result: open immediately below
the clicked Top Bar group on the same monitor, near the activation point, with a modest gap and
work-area/DPI clamping. Keyboard activation should anchor to the focused group rather than an
unrelated current pointer position. This is a FR-035/T41 investigation in addition to Settings work;
it is not permission to substitute a fake Windows panel or change real radio/audio/battery settings.

Inspected source facts:

- `src/topbar/TopBarWindow.cpp` emits `QuickControlsRequested` with the screen rectangle of the
  grouped item. Network, volume and battery currently form one group, not separate anchor targets.
- `src/app/ShellApplication.cpp` receives that rectangle. Its native branch calls
  `apps::OpenWindowsQuickSettings()` without passing an anchor; only the Aether branch uses it.
- `src/applications/SystemShellActions.*` gates Windows 11 and submits one `Win+A` chord. This
  wrapper has no HWND/RECT/placement parameter and no shell-opening acknowledgement.
- `src/controlcenter/ControlCenterWindow.cpp::Toggle(anchor)` already positions Aether's own panel
  below the group, right-aligns it, selects the anchor monitor/DPI and clamps to the work area.

[Microsoft documents Win+A as opening Quick Settings](https://support.microsoft.com/en-us/accessibility/windows/narrator/chapter-2-narrator-basics).
The documentation consulted during preparation did not identify an official public anchor/position
API for that **Windows-owned** panel. This is a limited feasibility finding, not proof that no
technique can ever work. A generic HWND movement call is not a documented Quick Settings placement
contract, and successful key submission is not proof that the panel opened or moved.

Investigate whether a documented route exists for the supported Windows builds. Cite exact primary
API documentation, supported versions and lifecycle behavior. Do not confuse the following with
control over the Windows panel:

- [`CalculatePopupWindowPosition`](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-calculatepopupwindowposition)
  computes suitable placement for a popup that the caller owns/positions; it does not redirect Win+A.
- [`Shell_NotifyIconGetRect`](https://learn.microsoft.com/en-us/windows/win32/api/shellapi/nf-shellapi-shell_notifyicongetrect)
  reads a notification icon's rectangle; it does not change the native system flyout's anchor.
- XAML Flyout placement applies to that application's own Flyout, not an arbitrary shell panel.

If a supported native route is found, make a bounded prototype that opens directly at the desired
anchor, avoids a visible bottom-to-top jump and preserves Escape/outside-click/focus/keyboard,
system controls, work-area bounds, DPI and multiple-monitor behavior. Verify repeated opens,
subpages/back, content size changes and Explorer restart. Test only with appropriate isolated
seams first; keep any real panel interaction separate from automated no-system-mutation tests.

Do not repeatedly reposition shell windows with a polling timer, reparent/embed the shell panel,
inject code, hook Explorer, alter global taskbar/work-area settings to coerce placement, or select
foreign windows by localized title/guessed class. An OS-specific cross-process experiment, if
investigated, remains explicitly experimental/unsupported until qualified; never silently ship it
as a reliable Windows capability. No repeated Win+A retries: a second chord may close the panel.

If no reliable supported route is established, report that clearly, keep the native choice working
at its Windows-controlled position and present the existing anchored Aether controls as an honest
alternative. Do not silently toggle the saved preference or describe Aether controls as the actual
Windows interface. This feasibility limit must not be hidden by a screenshot/mockup or block the
independent Settings corrections. Record the exact native-versus-Aether outcome in T41, HANDOFF and
TEST_STATUS. The private supplied panel image and any network/device information remain outside Git.

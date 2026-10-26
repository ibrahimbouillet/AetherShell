<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Ibrahim Bouillet -->
# Claude implementation brief — notifications, hidden icons and rounded window frames

Owner direction, 2026-10-26: D45, FR-038, FR-011, FR-036, FR-037; T48, T44, T47.
Claude implements this next batch. Codex reviewed S-052 and prepared this assignment; it has not
implemented these follow-ups. Read AGENTS.md, the latest HANDOFF, applicable PROJECT_SPEC/TASKS/
DECISIONS, then this brief. The S-052 Settings/Quick Settings work is the baseline to preserve.

## 1. Deliver the owner's three requests

1. Clicking the Top Bar date/time opens the actual Windows notification center and calendar,
   as in the private reference. The clock action must reach notifications, not just a date view.
2. Add a small, polished Top Bar chevron that gives access to the actual hidden notification-area
   application icons. This overflow contains background application icons; it is separate from
   notification messages and from the Dock's running/pinned applications.
3. Give windows four rounded visible corners and a thin, continuous neutral perimeter matching
   Aether's glass style, including when genuinely maximized. The owner's latest close-ups require
   the visible edge around the complete window, not only clipping its corners. The rejected wide
   screenshot is an external application window: fixing only Aether Settings misses that case.

Implement these requests, correct the reviewed S-052 defects below, build and validate the result.
Do not stop at a proposal or substitute screenshots/mock notifications/fixed application lists.
Preserve the Dock's accepted behavior, native input/UIA, consent, genuine state and recovery.
No new dependency, installation, push, tag or V3 release is authorized by this brief.

## 2. Open the private reference images first

Five unchanged, hash-verified owner attachments are available in the Git-ignored
`.local/claude-references/` directory. Read its README and view:

- `notifications-reference.png`: notification center/calendar behavior and structure.
- `hidden-icons-reference.png`: actual notification-area overflow and chevron behavior.
- `maximized-frame-reference.png`: rejected square frame on a maximized external window.
- `frame-rim-closeup-reference.png`: restrained visible perimeter at a rounded corner.
- `glass-corner-closeup-reference.png`: the material/rim character to carry around the frame.

These contain private application, account and notification content. Never transcribe that content
into public files, fixtures, logs or commit messages; never publish the images or derive shipping
assets from their pixels. Use them only for interaction and visual intent. Automated visual
evidence must use Aether-owned test windows over the changing private test-pattern backdrop.

## 3. Independently reviewed baseline and corrections

Reviewed source: `3084a93937fe044b274e738894bf49172c4f9034`, then the hidden-first-Settings fix
`bccd5729ca7ffe59780eae77a0a2169fa53187e3`; handoff-only HEAD at review was `86e7196`.
Codex independently reran the existing `build/wf-claude-fluidity` unit binaries: 24/24 executable
suites passed, 21.61 s. This was not a fresh compilation, native integration rerun, real-device
test, live-candidate identity check or owner visual acceptance. S-052's native aggregate remains
8/11 with passing scoped reruns and documented baseline intermittents, not a uniformly green run.

Correct these source-established issues before accepting this next candidate:

- **Precision input and cancellation:** `src/ui/form/FormScrollView.cpp`, `ScrollImmediately`
  (review line 392), uses `Apply(position + delta)`. During Imaging/Holding, `Apply` queues a
  deferred target without advancing `position`, so subsequent fractional-wheel deltas overwrite
  rather than accumulate the pending displacement. Its early return when target equals visible
  position can also retain a stale pending move after Home/reset. Accumulate from the pending
  destination, preserve bounds, and replace/cancel pending movement on immediate resets. Add
  regressions asserting exact displacement for multiple events before a frame, both current and
  stale editor-image paths, plus Home/reset interruption. `after > before` alone misses this.
- **Detail-motion interruption:** `src/controlcenter/ControlCenterWindow.cpp`, `Impl::Open`
  (review lines 493–500), replaces the outgoing view and snaps transition progress to zero.
  Back/Escape during entry therefore starts from a fully visible detail rather than the current
  composed frame. Retarget current positions/opacities and verify immediate reversal without a
  visual jump, including rapid repeated navigation and reduced motion.
- **Media arrival and bounds:** `ControlCenterWindow::SetState` (review lines 1295–1307) grows
  the window downward using its existing scale/top when media appears. Initial fitting covers
  only the earlier height. Recompute placement/scale against the clicked anchor and current work
  area when media arrives/leaves; retain access to all controls on short/high-DPI work areas.
  Add a no-media-to-media regression at a constrained/bottom-clamped anchor.

The reviewers inspected these paths but did not reproduce these three defects in a live GUI.
Keep that distinction. A separate pre-existing panel input issue also deserves correction while
touching the panel: release currently admits the focused target rather than requiring the actual
pressed target (`OnPointerUp`, review line 1435). Whitespace-down followed by release over the
previously focused tile must not activate it; clear press ownership on cancellation/capture loss.
This issue exists in the parent of `3084a93`; do not label it a newly introduced regression.

## 4. Date/time must open genuine notifications

Current route: `TopBarWindow::Activate` emits `ClockRequested`; `src/app/ShellApplication.cpp`
maps it to Win+Alt+D. Microsoft documents Win+N as notification center/calendar on Windows 11;
Windows 10 uses its Action Center route (Win+A). Detect the supported Windows behavior rather
than routing every version identically. Preserve genuine calendar access on Windows 10 explicitly.

Pointer, keyboard Enter/Space and UIA Invoke must converge on the same guarded action. Suppressed,
disabled, destroyed or stale Top Bar requests must not dispatch. Close competing Aether transient
panels appropriately, preserve foreground/key-release/error handling and avoid dispatching a chord
through an open modal menu. A successful input submission alone does not prove the panel opened.

Use the real Windows notification center for this batch. Do not read notification content merely
to provide an entry point, request notification-listener permissions, clear messages, toggle Do
Not Disturb, start Focus or invent unread badges. Those actions in the picture are Windows-owned.
This is separate from D44's Aether Quick Settings panel; do not undo its explicit routing.
Native placement remains Windows-controlled unless a supported, verified placement contract is
identified; do not embed, reparent or reposition the native notification center by private tricks.

Verify with fake action routing first; native hidden-taskbar behavior is a separate manual gate.
Report Windows-version/monitor/focus limitations instead of claiming that sending keys proves UI.

## 5. A real hidden-icons chevron

`TopBarModel.cpp` currently omits `ItemKind::NotificationArea` unconditionally under D27.
`topBar.showNotificationAreaButton` is a legacy false field and tests assert the arrow is absent.
The surviving activation path restores the entire Windows taskbar for 60 seconds or resumes
replacement. That recovery behavior does not by itself fulfill this hidden-icons request.

D45 explicitly authorizes a new dedicated chevron for hidden tray icons, superseding only D27's
arrow prohibition for this purpose. Keep recovery in Settings/logo menu/CLI. Choose and document
an unambiguous setting/default/migration; a stale legacy false must not silently hide the owner's
new entry point. Place it in the Top Bar's status group, with compact spacing, a proper installed
system glyph, real hit bounds, keyboard focus and a clear accessible name such as "Show hidden
icons". It must remain distinct from Wi-Fi/audio/battery Quick Settings and date/time notifications.

Establish a supported path to Explorer's real overflow. Microsoft documents Win+B for focusing
the notification area and keyboard interaction with its controls. Qualify what that permits while
taskbar replacement is active. Do not assume it directly opens the overflow or use a fixed number
of arrows/Enter against an unknown focus target. `Shell_NotifyIcon` manages an application's own
icon; it is not an API for enumerating other apps' hidden icons. Never populate a copied-looking
grid from a process list, hardcoded logos or private Explorer memory.

Prefer documented Windows interaction and validated accessible control identity. Do not inject,
subclass foreign Explorer windows, use private tray interfaces/memory layouts, or permanently
restore/reconfigure the native taskbar. If a verified native route requires temporary restoration,
preserve D23/D40 suspension, cleanup and recovery; label the visible behavior honestly. Record the
specific unresolved limitation if the supplied one-click overflow experience cannot be supported.
A button that merely exposes the taskbar remains partial completion, not a completed overflow.

Native overflow geometry/material are Windows-owned. A compact below-chevron Aether presentation
is desirable only if genuine complete icons/actions can be obtained through supported means;
do not promise a new tray host or add a persistent desktop surface without a separate decision.
Preserve ordinary app left/right-click behavior, menus and privacy. Never auto-quit a tray app.
Test Explorer restart, absent overflow, replacement on/off/suspended, stale controls, dismissal,
keyboard/UIA, light/dark, high contrast, reduced motion and narrow/DPI layouts using fake targets.

## 6. Rounded corners AND a continuous fine frame, including maximized

Preserve real maximize state, work-area alignment, foreground and original placement. The owner
does not want normal-state windows silently resized to impersonate maximization. All four corners
must expose the actual changing scene beyond the frame; no opaque rectangle, black wedge, painted
wallpaper or detached halo. Draw one crisp restrained perimeter all around, following the same
radius/material in normal and maximized states. Use the private crops to judge rim width/opacity.

Diagnose the actual rejected external-window case before claiming its cause. The current child
guard `src/app/WindowCorners.cpp` clips eligible foreign windows and may lease `DWMSBT_NONE`; it
draws no perimeter. It excludes the parent Aether process and Aether window classes. The helper
can skip ineligible windows or abandon a lease after an app replaces its region/material. Its
process being ready does not prove the specific target has an active lease. A show-time/late-frame
replacement is a source-supported investigation path, not an established explanation of the still.
Keep target diagnosis private: no titles, account content, paths or process inventory in Git.

Implement and verify Aether-owned window frames separately. The owned maximized-corner prototype
is not wired as a production lease; only its geometry is reused by the external guard. Meanwhile
`src/ui/form/FormPaint.cpp` uses `cornerRadius=0` and `border=false` for the form background.
Preserved native side/bottom borders and the S-052 title integration do not prove this new full
rounded-perimeter requirement. Coordinate geometry, clipping, material, caption and client scene;
avoid independent borders with gaps, double edges or mismatched tint during resize/scroll.

Preserve native caption buttons, close/maximize/restore, Snap/HTMAXBUTTON, system menu, drag/restore,
keyboard commands, normal-state edge resizing, IME/UIA and minimum size. Preserve actual composed
application content and transparency while changing frame treatment. Use DPI-correct physical
bounds; do not clip caption controls or leave invisible clickable corner regions.

For foreign applications retain D40's identity/integrity/session/ownership and conditional-restore
rules. Do not blindly override app replacements, round unsupported protected/elevated/fullscreen
windows, inject, subclass, fight geometry, kill/restart apps or introduce an unapproved global
overlay. Investigate safe reacquisition on synthetic late-frame/region/material replacements before
changing abandonment policy. Establish a supported perimeter technique per rendering family; if
one is unsupported, state that precise gap. Clipping alone cannot be called a completed frame.

Verify actual pixels over a changing private backdrop: all four cutouts plus perimeter continuity,
radius, width and contrast; native-region membership alone is insufficient. Cover normal/maximize/
restore, start already maximized, minimize/resume, repeated activation, late app replacements,
disable/quit/parent exit, failure recovery and HWND reuse, themes/high contrast/transparency-off,
96/144/192 DPI and negative monitor coordinates. Preserve placements and application replacements.
Provide separate owned-window evidence, external compatibility observations and owner acceptance.

## 7. Delivery and proof

Keep using the single shared official checkout. Inspect live identity before touching the existing
private candidate/recovery host; preserve the installed Alpha 2 binary/settings/startup and pins.
Use your own `build/wf-<name>` directory. Run the warning-free Release build, affected units and
focused native fixtures. Add meaningful regressions for the reviewed defects and new action/frame
behavior. A passing old binary is not a freshly built source verification.

Update requirements/tasks/decisions/module docs/test status/HANDOFF with exact source identities,
commands, outcomes, remaining failures, compatibility and owner-acceptance status. Local verified
coherent commits only. No push, tag, release or repository-settings change. Real-device actions,
the pictured external application and physical multi-monitor acceptance are separate manual work.
Before each assistant-operated real-desktop capture, D36 requires the readiness question and an
explicit ready reply for that attempt. Continue code/build/own-window tests while awaiting it.

The shared Markdown brief and private image package are the handoff to Claude. Codex has no direct
messaging connector to the external Claude chat and has not claimed that Claude received/read it.

## 8. Official technical references checked by Codex

- [Windows keyboard shortcuts](https://support.microsoft.com/en-us/accessibility/windows/keyboard-shortcuts-in-windows)
  — notification center/calendar and Windows-version differences.
- [Navigate the Windows taskbar](https://support.microsoft.com/en-us/accessibility/windows/navigate-and-explore-the-windows-taskbar)
  — notification-area focus and keyboard navigation.
- [Shell_NotifyIconW](https://learn.microsoft.com/en-us/windows/win32/api/shellapi/nf-shellapi-shell_notifyiconw)
  — add/modify/delete/focus/version operations for application notification icons.
- [Windows 11 rounded-corner policy](https://learn.microsoft.com/en-us/windows/apps/desktop/modernize/ui/apply-rounded-corners)
  — automatic corners exclude maximized/snapped windows; preference alone is insufficient.
- [SetWindowRgn](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-setwindowrgn)
  — region ownership and coordinates; not a perimeter renderer.

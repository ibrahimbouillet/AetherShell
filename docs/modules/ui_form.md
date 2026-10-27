<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Ibrahim Bouillet -->
# ui_form — the Aether presentation for application windows

`src/ui/form`, target `aether_ui_form`, gives normal, activatable application windows with native
child controls (Settings, the setup wizard, the updater window and confirmations) the same neutral
frosted material, rhythm, type scale and motion as the Dock and Top Bar (FR-037 / D41 / T47). It
depends only on core, diagnostics, platform and [graphics](graphics.md); it never uses the
compositor or the shell surfaces, so setup can use it on a machine where Aether is not installed.

## Material

`FormSurface::Attach(window, tokens, motion, scheduler)` decides the material with the pure rule
`ResolveFormMaterial`:

| Material | When | How |
|---|---|---|
| Acrylic | Windows 11 22H2+ (build 22621) and transparency effects on | `DWMWA_SYSTEMBACKDROP_TYPE = DWMSBT_TRANSIENTWINDOW`, frame extended over the client; the client paints `form.windowTint` at `form.windowTintAlpha` plus grain in premultiplied alpha |
| Solid | no system backdrop (Windows 10, 21H2) or transparency off | opaque `form.windowSolid`; the same layout |
| Native | high contrast, or no renderer | no custom painting: registered controls keep stock system-colored rendering; owner-drawn labels and lists are drawn with GDI in system colors |

Windows draws an inactive window's Acrylic as its flat fallback; this is the documented system
behavior and is not overridden. `SetTheme` switches materials in place (including to and from
Native) without recreating controls, so text, selection, focus, drafts and scroll positions survive.

## Painting model

The form keeps `WS_CLIPCHILDREN`. Whichever window repaints a region — the form, a registered
control, or a container — renders that region of one scene (window background, `FormCard`s,
`FormDivider`s, the owner's decorator, control visuals, focus rings) through a buffered-paint DIB and
one Direct2D DC render target with premultiplied alpha. Drawing always happens in the form's client
DIPs, so the grain bitmap brush is anchored to the form origin and child windows never show seams.

Native input stays native: push buttons, check boxes (switch rows), combo boxes, track bars,
progress bars and list boxes keep their window classes, messages, keyboard behavior and system UI
Automation providers; only their painting is replaced. EDIT and HOTKEY controls, inset lists and
documents become opaque layered children (`WS_EX_LAYERED`, alpha 255) inside a drawn rounded field,
so caret, IME, selection and clipboard behave exactly as in any edit control. The HOTKEY control
ignores `WM_CTLCOLOR*`, so the surface paints its field colors and the same localized key names
(`GetKeyNameTextW`) while the control's own input and caret stay untouched. Unthemed progress bars
lose their classic static edge while styled (their range and position are carried across that style
change) and their now-invisible native animation timers are stopped.

Combo boxes retain the native dropdown for mouse, keyboard and programmatic/UIA expansion, with
shared field/list colors. A MenuItem popup cannot replace the combo's native Selection and
ExpandCollapse semantics without an equivalent provider; the incomplete interception was removed
during independent review. Native list/navigation rows grow with text scaling, and multiline native
buttons wrap in the shared painter as well as in the fallback path.
Containers forward `WM_COMMAND`, `WM_NOTIFY` and control-originated `WM_HSCROLL`/`WM_VSCROLL` to the
form window, so owners see notifications exactly as from direct children.

## Moving presentation (D43, SF-01, 2026-10-26)

The owner saw scrolled Settings content break apart and reassemble on release. Reproduced in an
own-window frame sequence (S-052): during wheel scrolling the scene was composed from children
painted at different positions, and the opaque layered editors (a separate DWM channel) could reach
the screen one frame apart from the form surface. Three shared changes remove it:

- **Atomic present.** `Present(container)` renders the container and every visible registered
  descendant in one pass and copies it with one blit through the container's window DC (child
  windows are not clipped out), then validates everything it drew. `PresentMove` renders the frame at
  the destination first, moves the canvas (`SWP_NOREDRAW | SWP_NOCOPYBITS`) and blits immediately
  behind the move. Scrolling, container fades and hover tweens of a scrolling page all present through
  the outermost registered container (`PresentRoot`), never by letting children repaint themselves.
- **Editor masking.** Each layered editor's private image (PrintWindow) is kept current underneath it
  at rest. While a container moves, the scene draws those images and the live editors stay in place
  at the smallest nonzero opacity: still focusable, hit-testable and accessible; any key, pointer or
  IME input reveals an editor at once. One identical frame is held at each transition
  (Imaging → Holding → Moving → Revealing), so whichever composition channel arrives first shows the
  same pixels. No live control is replaced by a snapshot at rest.
- **Retained layers (Acrylic).** A cached material layer, a content layer in plain premultiplied
  memory shifted by row moves, and a scratch target that renders only the newly exposed or dirty
  rectangles (plus the scroll indicator overlay) keep the per-frame UI-thread cost small. Control
  visuals are cached per control and re-rendered only after their own repaint, update region or tween.
  Layers are prepared during idle after a page is built. Solid and Native materials render the full
  scene each frame through the same atomic path. `Stats()` reports the UI-thread cost and cache use.

`ClientOriginPx(hwnd)` gives decorators the origin of a moving canvas inside the frame being
rendered. `SetTheme` rebuilds only when the appearance actually changed (`SameAppearance`), so a
repeated theme notification does not drop the retained layers.

## Motion

Hover, press, switch knob, navigation indicator and the page `FadeIn` use elapsed-time tweens with
`form.feedback`, `form.indicator` and `form.page`, scaled by `MotionPolicy` (reduced motion is
instant). Field wells animate a hover rim and navigation/list rows tween their hover fill; containers
themselves take no hover (a pointer resting over a scrolling page must not repaint it piecemeal).
The scheduler keeps a steady cadence while animating (each frame is due one interval after the
previous frame's start, not after its end). Frames come from the optional `FrameScheduler`, otherwise a 16 ms timer that is killed as
soon as everything settles; `Animating()` stays true until the final settled repaint. A state change
that does not move (reduced motion) repaints once without starting frames. Indeterminate progress
and the busy status ring animate only while visible and the window is not minimized.
Progress preserves the actual accent and adjusts only its opaque track when needed to retain at
least 3:1 segment/track contrast, including neutral Windows accents. Rendered dark/light regressions
cover determinate, moving activity and reduced-motion activity without changing progress geometry.

## FormScrollView

A viewport (fixed, clips) and a canvas (content height, moves) as registered containers, plus an
overlay indicator child at the trailing edge (`GutterPx()` must be kept free of controls). Scrolling
moves the canvas without copying bits; wheel, `WM_VSCROLL` codes (`HandleScrollCode`, including the
thumb position), `EnsureVisible`/`RevealFocus` and short animated scrolls are supported. The
viewport answers `WM_GETOBJECT` with a UI Automation provider exposing the Scroll pattern merged with
its native HWND provider. Wheel notches glide on a critically damped spring
(stiffness 720, damping ratio 1): a burst or reversal retargets it with continuous velocity and the
spring rests within 0.35 px / 6 px/s. Fractional precision-touchpad deltas move the view at once
(carrying a glide in flight along), thumb drags follow the pointer, and line steps, immediate
`ScrollTo` jumps and policy/layout clamps present at once. Reduced motion scrolls without animation.

## ConfirmDialog

`ShowConfirmation(request, tokens, motion, scheduler)` is the shared modal confirmation: status badge,
title, message, detail and up to three native buttons (Primary/Danger, Secondary, Cancel). The owner
is disabled while it is open and re-enabled before the dialog is destroyed. It activates only when
this process owns the foreground; an inactive dialog sets its initial focus when the user activates
it, so it never takes activation from another application. Enter/Escape/close box follow the
documented mapping and `WM_QUIT` is re-posted. If the dialog cannot be created, the same question is
asked with `MessageBoxW` (`FallbackMessageBoxFlags`/`ChoiceFromMessageBox`).

Long/enlarged confirmation content scrolls inside the monitor work area; the action footer remains
visible, and measured multiline buttons stack when needed. Reduced-motion or high-contrast policy
changes finish active feedback, page fades and scroll motion immediately. Explicit accessible names
and helper text can be assigned to native fields whose neighbouring labels are insufficient.

## Evidence and limits

Unit tests (`tests/unit/ui_form`) cover the material rule, the MessageBox fallback mapping and pure
painters. Integration tests (`tests/integration/ui_form`) use only their own windows over a private
two-phase backdrop: client translucency over the glass and opaque fields (offscreen premultiplied
render), field/HOTKEY interiors identical across backdrop phases, Solid identical across phases,
scene authority (screen equals offscreen render for every scene-painted control), notifications
through containers, dropdown handler, theme round trips preserving state, idle (no form timers after
settling, native progress timers never recur), scheduler-driven frames, scroll/UIA, and the
confirmation dialog's keyboard, buttons, owner, quit and no-activation behavior. Live see-through of
Acrylic is sampled only while the test form is active; the window-level material itself is DWM's.
Remaining: physical multi-monitor DPI transitions, screen-reader walkthroughs and owner visual
acceptance.

D45 frame (S-054): every non-high-contrast form asks DWM for rounded corners
(`DWMWA_WINDOW_CORNER_PREFERENCE = ROUND`) and a border in the new `windowRim` token
(`DWMWA_BORDER_COLOR`); detaching restores both defaults. `FormSurface::SetMaximizedFrame(true)`
tells the surface its window is genuinely maximized: the Acrylic material resolves to Solid, and the
background of every window of the form is painted as one rounded body over the whole client
(`paint::kWindowFrameRadius`, 8 DIPs) with a pixel-snapped one-DIP `windowRim` stroke inside its
edge. The owner shapes the window with the matching region (Settings, ADR 0012). High contrast keeps
the system frame. FormScrollView now accumulates deferred precision input (`Pending()`) and drops a
stale pending target when an immediate move lands on the visible position (S-053 finding).

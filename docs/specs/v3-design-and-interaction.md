# V3 design and interaction work order

Owner-approved direction, 2026-10-23, D38; requirements FR-031–FR-036. The owner accepted Alpha 2
and started a broader V3 design, motion and interaction overhaul. This first batch does **not**
complete V3. No V3 release number, tag or publication is authorized by this work order.

Keep Alpha 2 installed and usable. The supplied reference screenshot contains unrelated application
content: do not copy it into the repository, fixtures or public presentation. Examples below use
generic application/profile names. Repository documentation and the next-assistant prompt stay English.

## First: compact surfaces and usable surrounding space (FR-031/FR-032)

- Make the default Dock modestly shorter: the owner estimates about 4–5 screen pixels. Preserve its
  visible bottom gap and anchor; do not achieve compactness by moving it closer to the bottom.
  Choose a DPI-aware implementation, record DIP versus physical-pixel behavior, preserve explicit
  saved size choices and visually check before calling the requested size accepted.
- Only the visible panel and currently drawn animated icons may receive input. Space above, below,
  left and right must remain usable by another application's window. Hover, wheel, click and drag
  must not activate a Dock icon through invisible shadow/entrance padding. Apply this to Top Bar too.
- Preserve magnification, reordered/raised icons, accessibility targets, reduced motion and drag
  capture. Hidden animation headroom must not become an invisible wall. Decorative shadows must
  not require a larger interactive native window.
- Keep symmetric screen-edge margins: left/right application margins align with Top Bar side
  insets; the upper outer margin follows Top Bar's top inset; the lower outer margin follows the
  Dock's visible bottom gap. The owner still requests automatic placement changes for **maximized
  windows only**. Manually placed windows may occupy free side/bottom space without being moved.
- Distinguish input interception from work-area allocation. A normal Windows AppBar reserves an
  edge rectangle; it does not supply a shaped maximized work area around a centered floating Dock.
  Prototype the layout policy separately, preserving readable application controls and reversible
  work-area recovery. Do not claim that fixing hit regions fixes the full-width reserved band.

Source diagnosis at the Alpha 2 baseline: Dock expands a visual region for shadow/entrance; Top Bar
uses its full outer window rectangle. `SurfaceWindow::ApplyInputRegion` gives that visual region to
`SetWindowRgn`, then relies on `HTTRANSPARENT` outside the intended input region. Windows only
guarantees that forwarding within the same thread. The initial V3 patch removes these two expanded
region overrides. Its explicit tradeoff is clipping external shadow/entrance decoration. A later
polished shadow solution must preserve cross-process input. Same-thread hit-test assertions alone
are not proof that another process receives clicks.

## Living running indicators (FR-033)

Owner clarification, 2026-10-25: the next batch is assigned to Claude for implementation and Codex
for independent review. Follow [the focused prompt](../CLAUDE_V3_HOVER_PROMPT.md). Marker motion
must be calmer and perceptibly slower at normal speed. A background hover pill matches the active
pill's width without changing foreground identity; actual activation retargets old/new markers
together. If hover already expanded the destination, keep it expanded rather than restarting it.
The prompt's suggested timing is an initial tuning proposal, not a fixed owner-approved duration.

Use the current running dot and active wider pill as the starting visual language:

| State/action | Desired behavior |
| --- | --- |
| Pinned application with no running window, hover | Existing icon rise/magnification; no invented running indicator. |
| Running background application | Small running dot. |
| Hover a running background application | Dot smoothly expands into a pill like the active application's marker; stays expanded while hovered. |
| Actual foreground application | Wider active pill remains tied to genuine foreground state. |
| Launch/activate by clicking an icon | Small marker grows smoothly as the application becomes running/active; do not report success before Windows observes it. |
| Leave, reorder, resize or change foreground | Position and width retarget continuously without snapping; return to the true resting state. |

Hover emphasis must not change the recorded foreground application or activate a window by itself.
Preserve the existing icon-rise behavior. Use elapsed-time/native animation primitives, not a new
JavaScript UI framework. Handle quick reversals, interruption, reduced motion and idle frame shutdown.

## Real hover previews with window/media actions (FR-034)

After a deliberate dwell over a running/background application, replace the text-only tooltip with
a rounded preview card using the same neutral translucent material. Show the actual window content,
not a fabricated image. Moving into the card must keep it usable; leaving/dismissing it must not
leave an invisible input window behind. Supply keyboard/UIA access and Escape dismissal.
Keep the hovered application's marker expanded while the pointer is inside its preview or on X.
Preserve source aspect ratio, use unambiguous selected-window content, and permit safe travel across
the icon/card gap. Leaving both returns a background marker to its running dot; the actual active
application stays wide. Media controls remain a later T43 follow-up after this focused preview/X lot.

The top-right X requests normal closure of the selected real window only on explicit click. For
multiple windows, identify/select each safely; never close an arbitrary sibling or terminate an app
because its preview was hovered. An application may refuse closure or ask to save work.

When an actual media session can be reliably attributed to the previewed application/window, show
supported play/pause controls and observed playback state. Do not bind a global/other app's session
using titles or guesses. Hide or explain unsupported controls. Preview pixels, titles and media
metadata remain transient local data; do not log, persist or publish them by default.

Investigate documented DWM thumbnails and existing media-controls identity/capabilities. Minimized,
protected, elevated, unavailable or ambiguous windows require an honest fallback. This feature is
not implemented by the current tooltip or the already available global media panel.

## Genuine connectivity and actionable system controls (FR-035)

- Wi-Fi strength must reflect current Windows observations. Active waves are bright; inactive waves
  are gray; unknown strength is distinct from zero. No fixed subtraction or decorative signal state.
  Signal strength, link quality, frequency and throughput are different quantities.
- Alpha 2 already reads `ConnectionProfile.GetSignalBars()` (0–5). Its glyph compresses that reading
  into three waves: 0→0, 1→1, 2/3→2, 4/5→3. This can visually differ from the stock Windows icon
  without being fabricated. V3 must review mapping, rounded geometry, freshness and unknown cases;
  continuous strength-only refresh is not yet qualified. Keep provider work off the UI thread.
- Every presented tile must perform its stated action: Accessibility, Live captions, Energy saver,
  airplane mode, radios, audio, brightness and the remaining controls. Distinguish real toggles
  from clearly labeled Windows Settings routes; show unavailable/error states rather than a dead tile.
  Existing source routes are not proof that the owner's clicks succeed; reproduce the failure.
- Prefer opening the genuine Windows Quick Settings surface from the grouped Wi-Fi/audio/battery
  action if supported, as requested by the owner. Investigate the documented Windows shortcut,
  close Aether's own popup first, honor OS differences and verify while native taskbar replacement
  is active. A keyboard shortcut is not an embedding API or an acknowledgement that a panel opened.
  Retain an honest custom/Settings fallback; never silently open two panels.
- Redesign the icon family with rounded, readable, proportional forms, including a recognizable
  airplane and Wi-Fi waves. A single coherent original vector system is preferred; imported assets
  require provenance/license review, and a new runtime dependency still requires a recorded decision.
  Preserve contrast, DPI, accessibility labels and actual state. No unrelated online images or logos.

## Window choreography and corners (FR-036)

The desired experience is a window revealing/expanding from the clicked Dock icon, a corresponding
return-to-icon minimize transition, and fluid activation/switching rather than an abrupt jump.
Synchronize this with icon rise and indicator growth. Record source icon and target-window geometry;
handle delayed launches, app refusal, interruption, multiple windows and reduced motion honestly.

The owner also wants all application corners to match the shell's rounded design, including the
inset maximized presentation. This is a **desired outcome with unresolved external-window feasibility**.
Windows normally excludes maximized/snapped windows from automatic rounding. Its corner preference
is a supported hint in applicable cases, not a universal radius/animation override.

First qualify Aether-owned windows and prototype external-window behavior using documented APIs
and synthetic windows only. Do not inject into other processes, replace their window procedures,
change Explorer/Winlogon, fight applications that own their geometry, or hide private content behind
a fake screenshot animation. Track unsupported window/OS states explicitly and preserve original
attributes on cleanup. Do not promise an arbitrary third-party application can be universally animated.

## Delivery sequence and acceptance

1. Fix empty-margin input interception; separate source tests from cross-process/visual acceptance.
2. Implement and inspect the smaller default; prototype the work-area/margin policy separately.
3. Qualify native Quick Settings routing and each existing system tile; fix genuine signal freshness
   and mapping; revise the rounded icon family.
4. Implement interruptible dot/pill motion, then real hover cards and supported per-window actions.
5. Prototype window choreography/corners, document OS limits, then obtain actual owner visual review.

Do focused Release `/W4 /WX` checks for each coherent change. Tests use fakes/own windows and never
alter real devices, owner windows or startup. Actual recordings require a fresh D36 ready reply
before every attempt and all-frame privacy review. T38's existing capture failure remains open;
it is not proof that product controls or a V3 showcase work. V3 remains open after this first batch.

## Primary technical references

- [WM_NCHITTEST and HTTRANSPARENT](https://learn.microsoft.com/en-us/windows/win32/inputdev/wm-nchittest)
- [Windows AppBar allocation](https://learn.microsoft.com/en-us/windows/win32/shell/application-desktop-toolbars)
- [Windows signal-bar observation](https://learn.microsoft.com/en-us/uwp/api/windows.networking.connectivity.connectionprofile.getsignalbars)
- [Windows 11 rounding policies](https://learn.microsoft.com/en-us/windows/apps/desktop/modernize/ui/apply-rounded-corners)

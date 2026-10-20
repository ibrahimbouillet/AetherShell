# Application Search window

`search::ui::SearchWindow` is the keyboard-focusable, transient application search surface opened
from the Dock or shortcut. It runs beside Explorer. Its constructor takes the graphics device,
frame scheduler, `SearchService`, application launcher and icon provider; those dependencies must
outlive the window. Initialize, Open/Close/Toggle, theme changes and all model access run on the
window's COM STA/UI thread. Icon and launch completion callbacks follow their services' UI-thread
contracts, with weak lifetime checks before accessing the window.

The window queries only indexed `ApplicationInfo` entries; it never executes arbitrary typed text.
Typing updates the displayed input immediately and debounces the in-memory search by 70 ms.
Navigation and Enter flush a pending query first so they cannot activate a result for older text.
The query timer and caret timer stop when the window hides or is destroyed. A repeated Open focuses
the existing window and preserves the original foreground window for Close.

## Input and results

A standard Win32 EDIT owns the actual text, selection, clipboard and IME behavior. Its alpha-zero
layered child avoids duplicate GDI text over the composition surface; DirectWrite draws the mirrored
text, selection and caret using the current design tokens. Because alpha-zero layered pixels are
hit-transparent, presses over the field reach the Search window, which places the caret (or extends
the selection with Shift or a drag) by hit-testing the drawn DirectWrite layout and sending
`EM_SETSEL`, so the caret lands where the glyphs are drawn. The EDIT's font follows the token family
(falling back to Segoe UI when GDI does not know it) and is recreated only when the size or DPI
changes. The native caret blink interval is respected while open. The edit's accessible name is
annotated as "Search applications" through the documented
[IAccPropServices API](https://learn.microsoft.com/en-us/windows/win32/api/oleacc/nf-oleacc-iaccpropservices-sethwndpropstr).
The SDK annotation GUID definitions live in `NativeEditGuids.cpp`; no third-party library is added.

Up/Down, Home/End, Page Up/Down and Tab select results, Enter launches through the application
launcher, Escape clears the query and a second Escape closes. A failed launch retains the results
and shows an inline error footer. The height accounts for that footer. A short work area displays
only whole rows; keyboard, wheel and UIA focus scroll the selected result into view. Hidden rows
report UIA `IsOffscreen` and empty bounds instead of fabricated on-screen positions. Application
IDs determine stable result IDs across filtering, and selection changes raise UIA focus events.

The normal placement follows the cursor's monitor and DPI. `SetPlacementArea` changes only this
window's placement for deterministic embedded hosts/tests; it never changes the system work area.

## Layout and motion (design language)

The window keeps the height of its placement area (from 18% below the top to 16 px above the bottom,
plus shadow and entrance guard margins)
and never resizes per query. Inside it, a glass panel (popup radius, Popup elevation, neutral
material) is 680 DIPs wide at most and contains: a 48-DIP search field with a Search glyph and an
underline that becomes a 2-DIP accent bar while focused, an "Applications" caption, 52-DIP result rows
(32-DIP icons drawn with `DrawIconBitmap`, the name with the matched ranges in the accent text color,
the kind as secondary text), and a footer hint ("Navigate, Open, Close"). The selected row gets a
selection pill and a small accent indicator whose position is tweened (fast, standard curve). A failed
launch shows an inline error with an Error badge above the footer. The panel height follows the
result count with the slide spring; the frosted backdrop and input rectangle follow the current
panel. A separate drawing region preserves exterior shadows and entrance movement, while excluding
the unused fixed-window tail. Narrow visual-only margins return `HTTRANSPARENT`; Windows guarantees
forwarding only to windows on the same thread. Text honors the Windows text scale up to 1.4x.
High contrast uses `selectedFill`/`selectedText` for result selection. Pointer hit testing and UIA
visibility use the currently displayed whole rows, rather than the target height of a growing panel.
Pointer actions wait until the compositor entrance finishes because the drawing moves independently
of resting hit geometry; keyboard input remains available during that brief transition.

Open is a compositor transition (fade in, from 12 DIPs lower and 97% scale, normal duration,
decelerate curve); close fades out to 98% (fast, accelerate) and hides when the compositor reports
completion, which replaced the close timer. Reopening during a close cancels it. Reduced motion and
high contrast are instant, including when enabled during an ongoing transition.
`RenderedFrames()` counts rendered frames: an idle open window renders only
for caret blinks. `PanelDip()` and `InputDip()` expose the current panel and field rectangles.

## Verification and remaining qualification

Integration uses thirty synthetic applications, a fake launcher and fake icons. It checks ordering,
stable IDs, keyboard launch/escape, debounced burst edits, navigation flushing, visible launch-error
state, short-area scrolling, the preserved focus-restore target and native edit name/value from a separate MTA client
while the UI thread pumps messages. Dark/light captures use an opaque private backdrop with a blur
guard band; foreign-window intersection guards run before and after copying pixels. Files are
written only to `%TEMP%\AetherShellTests\search-*.png`; no test launches a real application or changes
real settings/work area.

Executed on 2026-10-18: the private `build/wf-search-ui2` build passed with warnings as errors;
all four integration cases passed (1/1 CTest executable). Both final dark/light PNGs were opened
and visually checked: one themed input text copy, visible text selection, three whole synthetic
result rows in the constrained viewport and a readable inline launch error. Only the private
synthetic backdrop and Search window are present. The MTA test confirmed the native edit's
accessible name, Value pattern and keyboard-focusable property while the UI thread pumped.

Windows refused a foreground activation requested by the first focus fixture; that precondition
was removed. The final regression checks the remembered restore target without activating a real
foreign application. Actual foreground restoration remains subject to Windows focus policy.
Real IME composition, end-to-end screen-reader workflows, Windows 10 and monitor/DPI transitions require product
qualification. Finite compositor-owned opacity/8 DIP entrance and exit transitions were added afterward, with
instant reduced-motion/high-contrast behavior and a bounded close timer. All five integration cases
passed in the settled Release build, including finite hide, reopen cancellation and closed-action
rejection. A later capture fixture lost activation on the real desktop and failed before capture;
the isolated five-case rerun passed. No UI frame loop runs for those transitions. These checks do not establish complete
manual Search UI qualification.

Design-language restyle (0.2.0-alpha.1): `SearchVisualTests` adds two own-window cases: the panel
animates its height while the window keeps its bounds and rendering stops when idle, and clicks in the
field place the caret at the drawn glyph boundary (index 0, 2, 4 and the end). The existing cases keep
their assertions; the finite-exit case now waits for the compositor completion instead of a timer.

2026-10-20 focused verification: Release with warnings as errors builds and all ten Search UI native
cases pass. New checks cover preserved exterior drawing, the excluded fixed-window tail, whole-row
hit testing/UIA during expansion, entrance pointer suppression and immediate policy changes. The
capture fixture now refuses a copy unless its crop plus a 128-pixel blur guard is inside its opaque
backdrop, with foreign-window guards before and after. Both final dark/light PNGs were inspected:
only synthetic applications, readable query selection/error/footer and the private pattern appear.

The initial native executables crashed in shared compositor batch registration; the common graphics
fix is described in [graphics](graphics.md). A later Search run passed nine cases but failed its idle
assertion because a fixed 700 ms delay left a small spring velocity tail. The fixture now requires
explicit layout settlement within two seconds of the resize request, then retains the original
at-most-two-frames assertion during its 300 ms idle sample. The final ten-case rerun passes; the
earlier crash and premature-idle failure remain part of the session evidence. Physical DPI/monitor
changes, Windows 10, actual IME/screen-reader use and cross-process margin behavior remain open.

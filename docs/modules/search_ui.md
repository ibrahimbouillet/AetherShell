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
text, selection and caret using the current design tokens. Mouse presses over the input forward to
the real control because alpha-zero layered pixels are hit-transparent. The native caret blink
interval is respected while open. The edit's accessible name is annotated as "Search applications"
through the documented
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

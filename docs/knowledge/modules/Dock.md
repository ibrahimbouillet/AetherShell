# Dock — implemented, scoped native checks passed

Requirements: [FR-001–FR-004](../../../PROJECT_SPEC.md). Task T6;
[executed scope S-008/S-011](../TEST_STATUS.md), [implementation reference](../../modules/dock.md).

`src/dock` implements the search entry, ordered pins, optional unpinned running applications,
identity deduplication, real running indicators and persistent pin/unpin/reorder. Shrink/overflow
keeps hidden entries reachable, including excess pins. Click, middle-click, context menu, keyboard,
tooltips and drag springs use the current drawn geometry. The Dock contains no system status.

The current Release /WX gate passed 14 unit + 2 native cases in scoped runs after cross-review fixes.
Private dark/light rest/hover captures were opened. Fakes route launches/activation; tests inspect
idle frames, unchanged work area, drag/hit behavior and separate-MTA UIA navigation/Invoke/disconnect.
UIA Invoke posts an HWND message before opening an overflow menu. Windows refused background focus;
the test asserts that explicit error and restored no-activate style, not successful foreground focus.

Application services, asynchronous settings, accessibility and graphics supply its dependencies.
Optional reservation and shared full-screen suppression follow
[window behavior](../../decisions/0003-window-behavior.md). Real launch/activation, successful
foreground focus, AppBar/full-screen/Explorer behavior, physical monitor changes, Windows 10,
high contrast and screen-reader workflows remain separate qualification. The current Release unit
and isolated app lifecycle gates pass. Aggregate GUI attempts safely refused foreign overlap; the
final isolated Dock run passed after fixture-only 128-pixel guard-band and own-window z-order fixes.
There is no uniformly green combined 31/31 run, and no refused capture is accepted as a product pass.

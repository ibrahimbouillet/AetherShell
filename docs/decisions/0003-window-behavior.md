# ADR 0003 — Desktop window behaviour, AppBars and full-screen policy

- Status: accepted (2026-10-17)

> Historical Version 1 baseline. On 2026-10-18 the owner approved D19 / [ADR 0009](0009-reversible-taskbar-replacement.md).
> It supersedes the blanket no-hide policy and non-AppBar Dock rule only for the optional,
> explicitly consented V2 replacement mode. The remaining Explorer, full-screen and input policies
> remain applicable. The text below preserves the original decision rather than rewriting history.

## Context

Both bars float over the desktop beside the regular Windows taskbar. An AppBar
(`SHAppBarMessage`) reserves part of the monitor work area; that is right for some surfaces and
wasteful for others. Always-on-top windows must not cover full-screen games, videos or
presentations.

## Decisions

### Top Status Bar
- Registered as an **AppBar on the top edge** by default (`TopBarSettings::reserveSpace = true`),
  reserving only its height plus its top margin, so maximized windows sit below it (similar to a
  menu bar) instead of having their title bars covered. Users can turn reservation off; the bar then
  floats over windows.
- Positioned inside the monitor work area, horizontally inset by side margins, with rounded corners.
- Handles `ABN_POSCHANGED` (re-query position), `ABN_FULLSCREENAPP` (hide while a full-screen app is
  in front on that monitor), `TaskbarCreated` (Explorer restart → re-register), DPI and display
  changes. `ABM_REMOVE` is always sent on shutdown; if the process crashes, Explorer drops the
  registration when the window no longer exists.

### Bottom Dock
- **Not** an AppBar by default (`DockSettings::reserveSpace = false`): reserving ~80 DIPs for a
  floating dock would waste space on small screens. Optional reservation registers a bottom AppBar.
- `HWND_TOPMOST`, `WS_EX_NOACTIVATE` and `WS_EX_TOOLWINDOW`: clicks never steal focus, it has no
  taskbar button and is not in Alt+Tab.
- Sits above the bottom of the work area, so it never overlaps the Windows taskbar.
- The window is taller than the visible panel to leave room for hover magnification; a window
  region restricts input to the panel (plus the magnified icons while hovering).

### Full-screen interaction policy
- Both bars hide while a full-screen application is in the foreground on their monitor (AppBar
  `ABN_FULLSCREENAPP` notifications, cross-checked with `IsFullscreenWindow`) and reappear when it
  leaves. Topmost is otherwise kept so the bars stay reachable over normal windows.

### Keyboard access
- Global hotkeys (`RegisterHotKey`, configurable/disable-able): **Ctrl+Alt+Space** opens Search,
  **Ctrl+Alt+D** moves keyboard focus into the Dock (arrows, Enter, Shift+F10/Menu key, Escape).
  Failure to register (already taken) is logged and shown in Settings, never fatal.

### Monitors
- Version 1 targets the **primary monitor**. Window ownership and configuration are per monitor so
  more monitors can be added later. Monitor removal, resolution and DPI changes trigger a relayout.

### Shell integrity
- Aether Shell never replaces `explorer.exe`, never touches the Winlogon `Shell` value and never hides
  or disables the Windows taskbar.

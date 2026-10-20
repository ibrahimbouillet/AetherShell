<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Ibrahim Bouillet -->
# Windows platform layer

`src/platform/windows`, target `aether_platform`, supplies Win32 error conversion, RAII handles,
COM apartments, paths, DPI/monitor helpers, system events, message-loop/dispatcher infrastructure,
single-instance channels and hotkey/window utilities. It depends on core and diagnostics; product
windows/rendering remain above this layer.

## V2 taskbar compatibility seam

`TaskbarReplacement.h/.cpp` implements D19 / [ADR 0009](../decisions/0009-reversible-taskbar-replacement.md)
and FR-024. `ITaskbarBackend` makes state and visibility operations injectable. `TaskbarVisibilityLease`
captures baseline state/windows, temporarily adds auto-hide and hides only validated Explorer-owned
taskbar windows. It tracks explicit native state changes for normal restoration and recognizes new
taskbar windows after an Explorer change. `End` attempts restoration and keeps failure explicit.

`ABM_GETSTATE` / [ABM_SETSTATE](https://learn.microsoft.com/en-us/windows/win32/shell/abm-setstate)
are documented. `Shell_TrayWnd` / `Shell_SecondaryTrayWnd` identity and third-party visibility control
are a Windows compatibility detail. The backend checks class and shell process ownership before
changing a window, and ignores stale/recycled non-shell HWNDs. It never reads foreign window titles
or content. It does not replace Explorer, change Winlogon or register Aether as the native Windows
Settings taskbar implementation.

`TaskbarGuardSession` starts the existing versioned launcher in internal guard mode on a worker.
Only parent/ready/stop/restored handles are inherited via an explicit attribute list. The helper
checks parent image directory and session, then acknowledges readiness. It watches the parent/stop
handles and shell show events; ordinary shutdown or parent death restores the lease. The parent
observes helper exit and can restore an independently captured initial baseline. Public operations
use Result failures and generic diagnostics; no machine or private path details are emitted.

These operations block and belong on a worker; application UI access/status is marshalled through
its dispatcher. An initial snapshot cannot preserve native Settings changes known only to a helper
that was killed.

**Recovery without Aether Shell running (D21).**
- **Before the helper starts:** `TaskbarGuardSession::Start` calls `PrepareTaskbarBaseline`. It first
  restores any baseline left by an earlier crash, then captures the current state and writes
  `<config>\taskbar-baseline.json` atomically. The file uses schema 1 and holds the auto-hide state,
  window handles (as hints only) and visibility.
- **Baseline file lifetime:** Start fails closed if the file cannot be written, and removes it if the
  helper never starts. The session removes it once restoration is confirmed.
- **Restoring:** `RecoverTaskbars` restores from that file with no instance running and removes it
  only after success.
  - Without a file, nothing changes unless an explicit command asks to show the taskbar.
  - An unreadable file only makes the taskbar windows visible and leaves auto-hide unchanged.
- **Restoration always shows** every Explorer taskbar window and never hides one (D22). A capture at
  start can record the taskbar as not visible during Explorer's re-layout (S-016).
- **Known edge:** when only the parent dies and its helper restores the taskbar, nobody removes the
  file. The next `PrepareTaskbarBaseline` or `--restore-taskbar` re-applies it once, which can revert
  a native auto-hide change made in between.

Actual auto-hide, Explorer restart, multi-monitor and Windows-version compatibility require separately
authorized desktop qualification. Native Windows taskbar settings continue to configure Windows.

Pure taskbar tests use an in-memory backend. Rendering/app fixtures prohibit real taskbar or work-area
changes. Exact executed platform/controller counts and remaining V2 gates belong in TEST_STATUS;
source presence and a fake lease do not establish successful real taskbar replacement.

## Input injection (F0)

`InputInjection.h/.cpp` sends user-triggered key chords only (for example Win+Tab after a click), with
key-up recovery when Windows inserts only part of a chord. `MakeSendInputInjector()` returns
`Result<std::unique_ptr<IKeyboardInjector>>`; `SystemKeyboardInjector()` and the `SendKeyChord`
overloads that use it are the simplest production route. UIPI silently drops input while an elevated
window is in the foreground, and this cannot be detected. Tests use only a fake injector.

## 0.2 replacement lifecycle notes

The guard protocol (four inherited handles, `launcher/main.cpp`) and the helper are unchanged in
0.2.0-alpha.1. Explorer-restart, poll-timer and temporary-reveal changes live in the app controller
(see app.md); the platform seam still restores through `TaskbarVisibilityLease::End` and the D21
baseline.
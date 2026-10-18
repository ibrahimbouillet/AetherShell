# Native application lifecycle

`src/app`, target `AetherShellApp`, library `aether_app`. Main initializes common controls and COM,
and performs paths/logging bootstrap on a worker before creating UI. `ShellApplication` owns the
dispatcher, graphics/frame scheduler, events, single-instance channel, async settings, application
catalogue, tracker/launcher/icons, search, status/audio/weather, update coordinator and windows.

Settings load asynchronously before surfaces start. The Dock and Top Bar are the only persistent
desktop surfaces; Search and Settings are transient windows. Service changes dispatch to UI. Global
shortcut conflicts are shown in General. Second instances forward settings/search requests; actions
arriving during startup are retained until surfaces are ready.

The health signal is queued immediately after surfaces become ready, before potentially slow
application discovery. Quit settles editor/startup work, disables input and freezes new model mutations before the final
FIFO flush. Declining a failed-save quit restores mutation/input and shortcuts. Shutdown disconnects callbacks, cancels/joins workers, stops providers/windows, then
releases graphics/status WinRT runtime/dispatcher before the single-instance lock is released.

Read-only independent review found health-task ordering and early mutex release issues; fixed.
The complete Release /WX build and the isolated native app lifecycle case passed. The latter
checks both bar HWNDs (visible, or correctly suppressed when the real foreground is full-screen),
health, Settings/Search channel requests, shutdown, unchanged work area and channel cleanup.
Health-before-discovery ordering is source-reviewed; the fixture does not deliberately block discovery.
The updater handoff is guarded until child exit, with an observer established before process launch
and shared completion surviving failed dispatcher delivery. This path is source-reviewed only;
the live official signed-update cycle remains unqualified. See TEST_STATUS. No real external application launch, real
audio/startup change or real work-area mutation is claimed by the isolated module tests.

## V2 desktop ownership (D19 / ADR 0009)

`general.replaceWindowsTaskbar` is false by default. The Settings Desktop page requires explicit
Apply before changing it. The app coordinates a worker-owned `TaskbarGuardSession` using the existing
versioned launcher; guarded startup, disable/restart and process observation do not run shell waits
on the live UI thread. A replacement-only observer checks helper state and dispatches status to
Desktop. Dock replacement mode forces its AppBar reservation and uses the corrected visible bottom
margin plus its full animated footprint. Explorer remains running.

The helper restores the captured native taskbar state after parent death or a normal stop; the
parent attempts a baseline fallback if the helper dies. After a double failure, the baseline saved on disk restores the taskbar (D21; see below).
The helper can retain later native Settings state for normal restoration, but a killed helper cannot
transfer that state to the parent's initial snapshot. This is a disclosed shell-class compatibility
feature, not official native taskbar registration or Windows Settings takeover.

After catalog discovery, an empty Dock is eligible for an initial read-only pin import when replacement
was selected and `taskbarPinsImported` is false. The Desktop button can request an explicit import.
The STA worker reads supported shortcuts, and UI delivery merges missing entries into the latest
settings, preserving existing pins and arguments. No native taskbar pins are changed. Windows 11
pin/order coverage remains best effort.

`--quit` forwards shutdown to an existing instance and is a successful no-op without one.
`--restore-taskbar` asks a running instance to disable replacement. Without one (D21), the process
does the following:
- It keeps the single-instance lock and runs `RecoverTaskbarWithoutInstance` on a worker.
- It restores from `<config>\taskbar-baseline.json`, or shows the taskbar windows when no baseline
  exists.
- It turns `general.replaceWindowsTaskbar` off, but only in an existing settings file with the current
  schema.
- It reports the result in a message box and exits with code 0 on success or 1 on failure.

Recovery actions received while an instance is still starting are retained until settings load
settles, before the desktop surfaces and the guard start.

The earlier lifecycle result above is the 0.1 baseline. V2 controller/app, full Release, native
replacement and real Explorer restart qualification remain pending until executed evidence is
recorded in TEST_STATUS. Automated fixtures keep replacement disabled or use fakes; they never
hide the real taskbar or change the work area.

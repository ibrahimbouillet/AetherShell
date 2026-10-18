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

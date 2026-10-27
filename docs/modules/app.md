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

## V2 desktop ownership (D19 / ADR 0009, D23)

**Default on (D23).** `general.replaceWindowsTaskbar` defaults to true: while Aether Shell runs, its
Dock replaces the Windows taskbar. A settings file without the key, or with a non-boolean value
(warning), means true. Every save writes the key, so an explicit off (Settings > Desktop,
`--restore-taskbar`) persists. `ResetToDefaults` keeps the current value and resets everything
else. The settings/portable default remains `startWithWindows=false`. The historical D23 installer
kept its startup box unchecked; D34 supersedes that default for fresh interactive setup, which
visibly selects sign-in startup and offers an opt-out before Install. Accepting setup saves the
choice. Existing saved choices and Task Manager disables remain respected.

**Native access guard.** `ShellApplication::Run(command, paths, instanceName, nativeTaskbarAccess)`
may reach the real taskbar only when `nativeTaskbarAccess` is true and the instance name is the
installed `AetherShell` (`DesktopPolicy::MayAccessNativeTaskbar`). Any other session creates no
`TaskbarController`, never puts the Dock in replacement mode, shows "Windows taskbar replacement is
unavailable in this session" on the Desktop page, and refuses `--restore-taskbar` recovery without
an instance (`Unavailable`). Own-window tests run under their own instance names and also pass
false, so default settings in a fixture can never hide the real taskbar.

**Controller.** The app coordinates a worker-owned `TaskbarGuardSession` through the existing
versioned launcher; guarded startup, disable/restart and process observation never run shell waits
on the UI thread. Every status report is a `TaskbarUpdate`:
- `active`: a guard session runs and the Windows taskbar is hidden;
- `scheduled`: the controller acts again by itself (a request in progress, a pending activation or
  restoration retry, or an observed session);
- `suspended`: replacement is wanted but temporarily suspended;
- `status`: plain-language text for the Desktop page.

Retries are timed by the controller worker (`wait_until` on the retry time), not by a UI timer. The
1 s UI poll timer only asks the worker to observe a running helper. It stops as soon as an update
reports neither `active` nor `scheduled` (for example after the activation attempts are exhausted),
so nothing wakes periodically afterwards. Turning the setting on again (`EnsureTaskbarReplacement`),
an Explorer restart or `Resume` starts a fresh request, and the next active update arms the timer.
This closes the T16-gate poll-timer finding.

**Dock reservation.** `DesktopPolicy::ShouldForceDockReservation` keeps the replacement-mode AppBar
during activation, its retries, an active session and its restoration. It releases it when the
controller gave up (the Windows taskbar is back and reserves its own space), while suspended, and
never re-enables it after the Dock reservation itself failed. The app only switches the mode; the
reserved height always comes from the Dock's own layout (`PlaceDock`), so a compact Dock reserves
a compact band and size changes re-reserve on the next relayout. The app assumes no height.

**Explorer restart (TaskbarCreated).** The app re-registers the Top Bar and Dock AppBars, reserves
the Dock replacement space, then replaces the session through the existing `Restart` path (helper
protocol unchanged). A restart series retries sooner: the first retry after a failure waits
`restartRetryDelay` (500 ms) and later ones double up to the ordinary 2 s, with two extra attempts,
so the overall retry window is not shorter than an ordinary one. A Dock reservation that failed
earlier (for example while Explorer was not ready) is retried once per TaskbarCreated with a fresh
request. While suspended, TaskbarCreated changes nothing.

**Temporary reveal (runtime only).** `TaskbarController::Suspend()` shows the Windows taskbar
through the normal restoration path while replacement stays requested; activation, retries and
`Restart` are skipped. `Resume()` activates again with a fresh retry budget; `Request(false)` also
ends a suspension, and re-requesting true keeps it. Nothing is written to settings.
`ShellApplication::Impl::SetTaskbarReplacementSuspended(bool)` reserves the Dock space and arms
recovery observation before resuming. Top Bar notification-area access requests a temporary reveal;
the Dock stays visible until restoration completes, then hides so only the native bottom bar is
shown. A monotonic 60 s timer starts after restoration. Stale queued timer messages cannot shorten
a newer reveal. This is a fixed delay, not idle/activity detection. Explicit Resume, disabling
replacement or Quit cancels it; declining a failed-save quit leaves manual Resume available.
Full-screen suppression remains combined with the restoration state.

The helper restores the captured native taskbar state after parent death or a normal stop; the
parent attempts a baseline fallback if the helper dies. After a double failure, the baseline saved
on disk restores the taskbar (D21; see below). The helper can retain later native Settings state
for normal restoration, but a killed helper cannot transfer that state to the parent's initial
snapshot. This is a disclosed shell-class compatibility feature, not official native taskbar
registration or Windows Settings takeover.

After catalog discovery, replacement mode imports supported Windows pins and supplies File Explorer
once. Automatic sync runs at startup, when enabled and after Explorer restarts; it is not a live
registry watcher. It respects explicit dismissals and user order. Disabling sync stops subsequent
automatic imports after initial import/Explorer initialization; the Desktop/Dock manual action can
explicitly reinstate dismissed entries. The STA worker performs discovery and `PlanTaskbarPinSync`
(including possible 8.3 path IO). UI delivery accepts only the settings revision used for planning;
edits during IO cause a bounded fresh replan, never a stale overwrite. Automatic eligibility is
rechecked after IO and before retries. Manual requests coalesce while an import runs. Feedback
reports counts and persistence failure without logging local app identities. No native taskbar pins
are changed. Windows 11 pin/order coverage remains best effort; profile routes never silently fall
back to the default browser.

Appearance preferences produce shared design tokens for Dock, Top Bar and Search; high contrast,
system/reduced motion and the chosen speed are applied together. The native Appearance settings
page exposes these values plus Start/Task View tiles, animation reservation, weather side, keyboard
format and pin sync. Status actions request documented Windows settings or shared key-chord helpers
only after user interaction. Isolated module tests use fakes and never invoke those system actions.
Current integration source and policy regressions await the fresh whole-candidate gates (S-022).

Those combined gates now pass (2026-10-20, S-022): Release /W4 /WX, **34 app unit cases and two
isolated app lifecycle cases**. Default-on settings under a non-production instance cannot change
the native taskbar/work area. Tests cover policy with fakes; the new real-desktop/voice/recovery
workflow remains unqualified. D24's microphone menu requests Win+H only after checked focus admission.

`--quit` forwards shutdown to an existing instance and is a successful no-op without one.
`--restore-taskbar` asks a running instance to turn replacement off (persisted). Without one (D21):
- the process keeps the single-instance lock and runs `RecoverTaskbarWithoutInstance` on a worker;
- it restores from `<config>\taskbar-baseline.json`, or shows the taskbar windows when no baseline
  exists;
- it turns `general.replaceWindowsTaskbar` off. Because the default is on (D23), a missing settings
  file is created and a corrupt one (already preserved as `settings.json.corrupt-*`) is rewritten
  with replacement off; a newer-schema file is never rewritten, and the message then names
  Settings > Desktop;
- it reports the result in a message box and exits with code 0 on success or 1 on failure.

Setup adds a per-user Start menu shortcut "Restore Windows taskbar" that runs the launcher with
`--restore-taskbar` (see installer.md).

Recovery actions received while an instance is still starting are retained until settings load
settles, before the desktop surfaces and the guard start.

The earlier lifecycle result above is the 0.1 baseline. Unit suites cover the controller (fake
sessions), the desktop policy tables, recovery with a simulated taskbar and the D23 settings
semantics. Real replacement, Explorer restart and sign-out qualification remain real-desktop checks
recorded in TEST_STATUS. Automated fixtures keep replacement off or use fakes and the native access
guard; they never hide the real taskbar or change the work area.

## D26 controls and side work-area alignment (in progress)
The app owns a transient [quick controls panel](controlcenter.md), a separate serial MTA worker for
[system](system_controls.md)/[media and brightness](media_controls.md), and guarded explicit intents.
Isolated native-test instances cannot execute those system routes. Shutdown disables the radio
completion bridge, closes the panel, cancels/joins workers and releases infrastructure reservations.

Two alpha-zero click-through side AppBars reserve twelve DIP for maximized-window alignment while
replacement is active. Manually placed windows are never moved/resized. Disable/reveal/quit releases
both; Explorer/display notifications refresh them. Top Bar preserves Shell-approved horizontal exclusions and its existing visible inset;
side leases honor the approved vertical extents between top/bottom allocations. Native qualification remains
pending; geometry tests alone are not evidence that Explorer honors/restores these reservations.

## D45 additions — notifications, hidden icons and maximized perimeter (S-054)

- **Notifications/calendar:** `ClockRequested` and `CalendarRequested` close Aether's own panels and
  send the version-specific shortcut from `system_controls` (see that module). A failure is shown as
  the clock item's tooltip.
- **Hidden icons (`HiddenIcons.h/.cpp`):** `OpenTrayOverflow` drives an injected `ITrayOverflow`
  (invoke Explorer's overflow button once, wait for Explorer's overflow window, bounded retries for
  not-yet-populated content, about 0.3 s). `MakeSystemTrayOverflow` reads the Windows 11 taskbar's
  XAML content (`Windows.UI.Composition.DesktopWindowContentBridge` under `Shell_TrayWnd`) through UI
  Automation: the system tray buttons share automation id `SystemTrayIcon`, and the overflow button is
  the one whose text element shows a chevron glyph (`IsOverflowGlyph`, language-independent; U+E971
  observed on build 26300). The Windows 10 fallback is the `TrayNotifyWnd` button. The button is
  invoked through Invoke/Toggle/ExpandCollapse; overflow windows count only from Explorer's process,
  when visible, uncloaked and non-empty. The work runs on a dedicated MTA worker. The taskbar is not
  revealed: its content stays reachable while Aether hides it, and Explorer shows the overflow as a
  window of its own (S-055 live check). No icon is enumerated, copied or drawn by Aether.
- **Maximized perimeter (`WindowCorners`, ADR 0012):** the guard clips eligible maximized windows
  inside their rounded outline by `CornerRimWidthPx` and keeps one click-through ring window
  (`AetherShell.WindowCornerGuard.Rim`) directly beneath each clipped window, shaped to the band and
  filled with a neutral system-theme color. Captioned, resizable `WS_EX_LAYERED` windows are now
  eligible; an abandoned window gets one fresh attempt after it leaves the maximized presentation.
  Native tests verify cutouts, a continuous ring and rim lifecycle over a private two-phase backdrop.

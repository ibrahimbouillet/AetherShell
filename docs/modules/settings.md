<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Ibrahim Bouillet -->
# Settings

Implements FR-014 and the settings model/startup component of FR-016. NFR-002 keeps all file and
registry operations on workers; NFR-005 requires explicit failures and bounded callback lifetime.

## Persistence design (GAP-007)

The UI thread owns the model and `Changed()` signal. `Load` posts reading, migration and corruption
recovery to one private FIFO `WorkerPool`; its completion applies the result on `IUiDispatcher`.
Updates are accepted only after loading finishes. An accepted update validates and changes the
model immediately, emits `Changed`, then queues its snapshot. Serialization, backup and atomic
replacement happen on the worker. The completion reports durability on the UI thread. A failed
write retains the in-memory value and dirty state; the next Update retries the latest snapshot.
Revision numbers prevent an older save completion from clearing a newer pending revision.
No automatic retry loop or UI-thread file access is permitted.

`FlushAsync` is a FIFO barrier for app shutdown. The app waits asynchronously for its completion
before stopping the message loop and destroying the service. Destruction suppresses callbacks and
joins/discards the worker; it is not a substitute for flushing accepted changes. Consumers must
keep the dispatcher alive until the service has been destroyed.

## Configurable shortcuts (GAP-008)

Schema-v1 receives additive optional `general.searchHotkey` and `general.focusDockHotkey` objects,
each with integer `modifiers` (Alt=1, Control=2, Shift=4, Win=8) and `virtualKey` fields. Defaults are
Ctrl+Alt+Space and Ctrl+Alt+D. The existing `hotkeysEnabled` flag and every existing default remain.
Invalid/unsupported combinations fall back to defaults with warnings; collisions are corrected.
The application registers the validated chords and reports registration conflicts. The native
Settings UI exposes the main key and all fifteen nonzero modifier masks for both chords.

## Storage and recovery

`SettingsSerialization` covers every model field with stable camelCase member names and lowercase
enum values (`weekday-day-month` for the long weekday form). Unknown keys and wrong types warn and
retain defaults; numeric ranges, finite values, coordinate pairs, pins and key chords validate.
Pins keep the first 64 distinct nonempty identities and limit each string to 1024 UTF-16 units.
The serialized file also has a one MiB ceiling; larger output fails before replacing the working
file, with the in-memory state retained for correction.

Missing schema means version 0, migrated to version 1 in memory without an automatic write. Future
schema data is read without being overwritten. Before the first explicit change, the exact input
bytes are atomically preserved as `settings.v<N>.backup.json`. A different existing backup is first
preserved under a timestamped sibling. Unsupported huge numeric versions fail closed. Binary
rollback retains user-data backups; it does not automatically roll user settings back.

Unreadable/corrupt or oversized regular files are renamed to `settings.json.corrupt-<UTC suffix>`
before defaults are used; at most three are retained. A failed preservation blocks saving and leaves
Load retryable. Directories/symlinks at the settings path are not treated as corruption backups.
Reset preserves the current source bytes as `settings.json.bak` before its atomic save. No operation
logs setting values, unknown member names, paths or exception text.

## Startup registration

`StartupRegistration` is blocking and must run on a worker after a direct consent action. It validates
an absolute existing `.exe`, quotes its path, appends `--autostart` and writes only the per-user Run
value `AetherShell`. An injected registry adapter and key paths permit isolated tests. No constructor
accesses the registry. Disable removes only this Run value.

The `StartupApproved` binary format is not a documented stable Windows contract: known leading
bytes 2 (enabled) and 3 (disabled by user) are recognized, and unknown formats report
`UnknownApproval`/`Unsupported`. The approval value is never written, and a recognized Task Manager
disable is never bypassed. Actual Windows registration/Task Manager behavior remains unverified.

## Executed checks

On 2026-10-18 a new private `build/wf-settings2` directory built with `/WX` and **32 unit cases passed**:

```powershell
./scripts/build.ps1 -BuildDir build/wf-settings2 -Modules settings -Jobs 2 -CMakeArgs '-DAETHER_WARNINGS_AS_ERRORS=ON' -Test -TestRegex settings
```

The initial 28 cases passed, followed by three safety regressions and one large-integer/UTF-8
boundary regression; the cache
retained `AETHER_WARNINGS_AS_ERRORS:BOOL=ON`. Tests cover all model defaults/enums, malformed values,
pin/chord validation, migration and exact-byte backups, corruption/size recovery, save/reset/temp-file
behavior, worker/UI delivery, revision ordering, failed-write retry/flush, failed-load retry, mutation
exceptions, callback suppression and an entirely in-memory startup adapter. No test touches real
Run/Uninstall keys, changes startup settings or launches a process. Native UI validation is recorded
separately below when its integration checks complete.

## Native editor

`settings_ui` provides a native navigation window for Dock, Top Bar, Weather, Updates, General,
About and Desktop. Ordinary non-consent fields save after a 350 ms debounce, and pending edits save before navigation or
hiding. Field mutations merge into the latest model rather than restoring a captured whole-model
copy. Apply also retries a failed save. Draft numeric values must be complete before they can apply.
Weather searches run on a worker only after Search; selecting a returned candidate saves it.
About includes the complete MIT license and public attribution.

The startup checkbox is excluded from automatic saves. Apply is the explicit consent action, with
controls disabled before calling the asynchronous registry seam. Repeated Apply cannot enqueue
duplicate startup changes. Reset confirmation explicitly includes disabling startup, and the
registry removal must succeed before defaults replace the model. A missing startup seam cannot
reset an enabled-startup model. The app supplies worker execution and UI-marshalled completion.
`PrepareToClose(ready)` drains editor drafts and waits for a pending startup transaction to settle
before calling `ready` on the UI thread; the application queues its settings flush barrier there.
This preserves ordering when quitting immediately after editing or consenting to startup.

DPI messages recreate fonts, reposition permanent controls, rebuild page controls and restore
draft fields. Initial and DPI-changed bounds stay within the monitor work area. Resizable native
horizontal/vertical scrollbars and wheel input move every child, including wholly offscreen ones;
keyboard navigation reveals the focused control. Update progress refreshes preserve editable update
fields. Retained verified offers remain downloadable after an error or postponement; checking and
downloading disable that action. Inline update failures show a generic error code and retry help,
never raw exception/error text. Progress displays received bytes, with a percentage only when a
nonzero total is actually reported.

On 2026-10-18 the private `build/wf-settings-ui-audio` build passed `/WX` and **5 native integration
cases** after the shared release privacy compile definition triggered a full dependency rebuild:

```powershell
./scripts/build.ps1 -BuildDir build/wf-settings-ui-audio -Modules settings_ui -Jobs 2 -Target aether_settings_ui_integration_tests -Test -TestRegex '^aether_settings_ui'
```

The build cache retains `AETHER_WARNINGS_AS_ERRORS:BOOL=ON`. Cases cover actual debounce autosaving,
all pages, update draft retention across progress snapshots, generic failure/progress presentation,
shortcut editing and preservation of every modifier mask, startup exclusion from live changes,
one pending consent operation, concurrent Dock pin preservation, startup-removal failure before
reset, close readiness after consent persistence, DPI/font scaling, full MIT text and keyboard
reveal of Apply in a reduced viewport. An initial viewport assertion failed; the implementation now
moves every child with DeferWindowPos, and keyboard focus reveals the whole button even when
the scrollbar's far-right position shows only part of it. Tests use temporary files and injected
startup callbacks, with no actual startup or registry changes, network requests or app launches.

Independent static review identified and cleared consent, model merge and viewport issues.
Physical monitor transitions, screen-reader interaction, real Windows startup/Task Manager,
live geocoding and real update/download/install actions remain separate manual qualification.

The settled Release gate passed **33 service unit cases**, including a blocked-save shutdown
regression. `SetChangesEnabled(false)` rejects new Update/Reset mutations without cancelling
accepted saves or Flush; the app calls it after editor/consent settlement and before its final FIFO
barrier. Reenabling restores writes when quitting is cancelled. Five native editor cases also pass.

## V2 Desktop consent and pin import

Schema-v1 gains additive `general.replaceWindowsTaskbar` and `general.taskbarPinsImported` Boolean
fields, both default false. Existing files therefore retain coexistence until explicit consent.
The Desktop page is appended at index 6, preserving older page indexes. Its replacement checkbox
is `consentOnly`: enabling and disabling both require Apply; debounce, navigation, close and app
quit settlement cannot apply an unchecked consent draft. A saved true value can resume on later
normal launches. This is separate from start-with-Windows registration.

The page explains recovery and the unsupported native Windows Settings takeover. Its status label
refreshes in place through `RefreshDesktopStatus()`, preserving a pending checkbox draft. The
Import Windows taskbar pins button calls an injected callback and is disabled if unavailable. Import
reads supported shortcuts without altering Windows pins, merges missing entries and preserves launch
arguments. Original Windows 11 pin coverage and order are not guaranteed. About and all previous
pages remain accessible.

On 2026-10-18 the fresh Debug `build/wf-v2-settings` `/W4 /WX` build passed the settings unit
executable (8.65 seconds) and all **seven native editor cases** in a separate serial run (15.14
seconds). New cases establish durable Apply-only enable/disable, draft-preserving status refresh,
fake concurrent import with no implicit consent, action-result retention on reopen, unavailable
import disabled, pin arguments saved/reloaded and unchanged real work area. The five-case result
above is the earlier 0.1 baseline. The V2 suite uses temporary settings and fake callbacks, not
actual taskbar, startup registry, audio or external application changes. Wider product qualification
and the full Release gate remain separate.

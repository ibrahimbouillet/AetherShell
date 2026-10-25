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

The Weather page offers manual search or **Locate automatically with Windows**. A separate
**Use Windows location** action requests the OS permission in the foreground; fresh defaults alone
never request or infer consent. The page discloses that coarse coordinates go to Open-Meteo and a
rounded location key is cached locally. A denied or unavailable reading leaves weather unavailable.
Existing settings with manual coordinates and saved weather opt-outs remain manual/off during
migration. Real Windows permission and location behavior still require owner-driven qualification.

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

## 0.2 schema additions (schema version stays 1)

All new members are additive; 0.1 ignores them with a warning, so rolling back is safe.
- `dock`: `reserveAnimationSpace` (false), `metricsVersion` (internal, 1..2), `dismissedPins` (at most
  128 ids, each at most 1024 UTF-16 units, unique ignoring case), `explorerDefaultApplied` (false),
  `showStartButton` and `showTaskViewButton` (true).
- `topBar`: `height` (32 DIP, 24..48), `openWindowsFlyouts` (true), `weatherPlacement`
  (`left`/`right`), `keyboardIndicator` (`two-line`/`compact`, default two-line; two-line needs at least
  30 DIP and the Top Bar falls back to compact below that), `showNotificationAreaButton` and
  `showBatteryPercent` (true).
- `general`: `syncTaskbarPins` (true), `themeMode` (`system`/`light`/`dark`), `accentSource`
  (`windows`/`aether`), `animationSpeed` (1.0, 0.5..1.5).
- `weather`: `useWindowsLocation` (true as the first-use offer, with no implicit OS request),
  `windowsLocationConsent` (false until an allowed Windows permission result). An older file with
  saved coordinates and no location-mode key migrates to manual mode; automatic coordinates are
  held in memory and are never written to these settings fields.
- Dock size defaults are 40/4. `dock.metricsVersion` is an internal marker: a file whose dock group
  has no marker was written by 0.1 (version 1), and there an absent `iconSize` or `bottomMargin`
  means 48 or 10. If the effective size is exactly 48/10 it moves once to 40/4 with a migration
  warning; any other size is kept, and the marker is then written as 2. After that an explicit 48/10
  is the user's choice and stays.
- Pinned ids deduplicate ignoring case.

## Taskbar replacement default (D23)

- `general.replaceWindowsTaskbar` defaults to **true**: while Aether Shell runs, its Dock replaces the
  Windows taskbar. No schema bump: an older file without the key reads as true through
  the struct default, and a non-boolean value falls back to true with one warning.
- Every save writes the key, so an explicit false (Settings > Desktop, `--restore-taskbar`) persists.
- `ResetToDefaults` applies `Settings{}` but keeps the current `replaceWindowsTaskbar`: a reset changes
  customization, never desktop ownership. Everything else resets, including `taskbarPinsImported`,
  `syncTaskbarPins`, `dismissedPins` and `explorerDefaultApplied`, so an emptied Dock imports the
  Windows taskbar pins once more.
- `--restore-taskbar` without a running instance writes false even when the settings file is missing
  (created) or corrupt (rewritten after the store preserved it as `settings.json.corrupt-*`); a
  newer-schema file is never rewritten (see app.md).
- A corrupt file falls back to defaults, which now means replacement on; the earlier explicit off is
  lost, although the corrupt file is preserved. Downgrading to 0.1 drops the key, so re-upgrading
  turns the mode on again.
- The settings/portable default stays `startWithWindows=false`. D34 supersedes the earlier installer
  default: fresh interactive setup visibly selects sign-in startup with an opt-out, then saves the
  accepted choice. Existing saved choices and Task Manager disables are preserved; a settings
  default does not itself register startup.

Unit coverage (`TaskbarSettingsTests`, `SettingsSchemaTests`): legacy files without the key read
true; Settings{} has replacement on and startup off and writes the key; an explicit false survives
JSON; three non-boolean values fall back to true with exactly one warning; a service reset keeps
both true and false and resets the other fields through an in-memory store.

## V2 Desktop consent and pin import

Schema-v1 gains additive `general.replaceWindowsTaskbar` and `general.taskbarPinsImported` Boolean
fields. In 0.1.x both defaulted to false; since D23 (above) replacement defaults to true.
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

Integration checkpoint (2026-10-20): the Apply fixture now starts from a persisted opt-out; its
false-to-true and true-to-false transitions no longer depend on the old default. The pin-argument
fixture verifies that adding a pin preserves its original replacement choice. A new default-on
fixture checks that closing discards an un-applied opt-out and Apply durably saves false. All use
temporary stores and own editor windows with no taskbar controller. These edits await execution
against the combined candidate; the earlier seven-case result does not verify them.

The integration adds an eighth navigation page, **Appearance**, without changing previous page
indices. It edits theme/accent, animation speed, icon presets (preserving a custom size until a
preset is selected), Start/Task View buttons, animation-space reservation, weather side, keyboard
format and automatic pin sync. Two new own-window cases cover persistence/explicit taskbar opt-out
and pin remove/re-add metadata. The latter fills the 128-entry dismissal list: removing a pin must
retain the newest dismissal by evicting the oldest, and manual Add clears only its own dismissal.
Pin mutations report asynchronous saving failures. Ten native editor cases are expected; execution
of this candidate remains pending in TEST_STATUS S-022.

Combined candidate verification (2026-10-20, S-022): **52 settings unit and 10 native editor cases
pass**, Release /W4 /WX. Appearance persistence/custom size, explicit opt-out, bounded newest dismissal
and profile route preservation are now executed evidence. No real startup/taskbar setting changed.

## Visual redesign on the shared form presentation (T47 / FR-037 / D42)

`SettingsWindow` is rebuilt on [ui_form](ui_form.md): a normal resizable window (default about 960 x 700
DIPs, minimum 380 x 340) with the Acrylic/opaque/native material, a glyph navigation pane that becomes
an icon rail below `form.narrowWidth`, a scrolling content region (`FormScrollView`) with a page title,
section titles and cards, and a fixed footer with the toned status, "Reset defaults..." and Apply.
Rows are layout elements over existing controls, so `SetTheme`, resize and DPI changes relayout without
recreating anything: drafts, staged consent switches, focus, selection and scroll position survive.

Contracts are unchanged: page order and the navigation list (100), Apply/Reset/status (101-103), binding
ids `200 + index` in the same order and kinds, the action/list/status ids 270-272 and 301-321 (plus 322
progress and 323 headline on Updates), the 350 ms live apply (timer 19), consent-only startup and
taskbar replacement, validation/correction messages, asynchronous saves, `PrepareToClose`, pins,
geocoding and the D23 reset rule. Numbers are formatted for people (`SettingsFormat`): an opacity of
0.80 shows "80" with "%", factors such as 1.45 show with "x", whole pixels without decimals, while
parsing returns the exact stored value. Each number has an auxiliary slider (id `400 + index`) kept in
sync with its value field; invalid text marks the field and explains the range. A staged consent
switch says "Press Apply to confirm this change." until Apply. Updates are presented in place from
the coordinator's snapshot (headline, toned summary, determinate progress only for a known total,
actions enabled per state, Later/Skip only for an offered release, the verified state worded from the
signed manifest) without rebuilding the page. Reset asks through `SettingsCallbacks::confirm` or the
shared glass confirmation; `scheduler` paces motion. Combo boxes retain their native dropdown and
Selection/ExpandCollapse accessibility semantics, using the shared field/list colors. This is a
documented native popup exception until a custom glass list has equivalent accessibility patterns.
Ctrl+Tab / Ctrl+Shift+Tab / Ctrl+PageDown / Ctrl+PageUp switch pages. Before the application's first
`SetTheme` the window uses deterministic tokens for the Windows light/dark mode (no WinRT palette read,
because standalone callers may tear down COM apartments around each window).

Compact layouts stack fields, pin actions and long buttons; measured multiline button/control/footer
heights grow with text scaling. Layout tests preserve horizontal bounds before checking containment
and inspect scrolled positions, so viewport clipping cannot hide a horizontally overflowing control.
Navigation retains page-local invalid/unfinished input, text selection, consent drafts, focus and
scroll, restoring them over the current settings model. Ordinary live edits drain through the
existing validation path before navigation; consent remains Apply-only on its owning page. Reopening
the active page does not rebuild it. Closing discards unapplied drafts, preserving the existing
close/consent contract. Native numeric, combo and hotkey controls publish their setting labels and
helper text explicitly for assistive technology.
The application's development/test `--data-dir` profile rejects startup registration before any
registry access, so a private prototype cannot replace the installed sign-in entry through Settings.

Evidence: `tests/integration/settings_ui` (12 original behavioral
cases adapted only in mechanics: `ScrollMetrics` instead of the window scroll bar, a descendant search
for the Quick Settings switch, a 2-pixel DPI tolerance at fractional scales), redesigned page/layout/
input/state checks and
`tests/unit/settings_ui` (formatting and slider mapping). See TEST_STATUS S-049.

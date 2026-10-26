<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Ibrahim Bouillet -->
# topbar — native floating system status surface

**Next assignment, D45/FR-038/T48 (2026-10-26):** implement genuine notifications/calendar from
date/time and a dedicated real hidden-icons chevron following
[the Claude brief](../CLAUDE_V3_NOTIFICATIONS_TRAY_FRAME_PROMPT.md). Current clock routing remains
Win+Alt+D; the model omits the old D27 arrow even when its legacy field is true. The historical
recovery route described below is still in code but is not a visible overflow implementation.
D45 narrows D27's arrow ban for the new hidden-icons purpose only. Source implementation and native
hidden-taskbar/overflow verification are pending; S-053 records the source review/existing units.

V3 source follow-up (D38/T39, S-035): native input/window region is the visible panel rectangle,
with no expanded outer shadow region. This avoids relying on same-thread-only HTTRANSPARENT
outside the panel; outer shadow decoration is clipped. Work-area allocation, physical visual
acceptance and exact rounded-corner input remain separate. Installed Alpha 2 is unchanged.

`src/topbar`, target `aether_topbar`, implements the Top Status Bar described by FR-005–FR-011,
NFR-002/NFR-007 and ADR 0003. It is a separate custom-drawn Win32/Composition surface alongside the
Dock. System status never moves into the Dock.

## Integration

`TopBarWindow.h` exposes `aether::topbar::TopBarContext` and `TopBarWindow`. Context supplies the shared
`GraphicsDevice`, `FrameScheduler`, `IUiDispatcher`, optional `SettingsService`, nullable clock,
battery, keyboard, network, audio, microphone and weather providers. Providers are unstarted and
must outlive the window. The window starts successful subscriptions and stops them on destruction.
The dispatcher and graphics resources also outlive the window. Model, window and settings access
remain on the UI thread; provider callbacks copy snapshots into lifetime-guarded dispatcher work.

Construct `TopBarWindow(context)`, call `Create()`, connect the request signals and `Show(false)`.
The signals are `SearchRequested`, `SettingsRequested`, `CheckForUpdatesRequested`, `AboutRequested`,
`QuitRequested`, `KeyboardRequested`, `NetworkRequested`, `WeatherRequested`,
`MicrophoneSettingsRequested`, `VoiceTypingRequested`, `ClockRequested`, `PowerSettingsRequested`, `SoundSettingsRequested`,
`RevealWindowsTaskbarRequested`, `ResumeTaskbarReplacementRequested`, and `FullscreenChanged(bool)`
for forwarding to the Dock. `Relayout`,
`OnExplorerRestarted`, `SetTheme(tokens, motion)` and `SetFullscreenHidden` support app/system wiring.
`SetSnapshots`, `SetSettings`, `Items` and `RenderCount` support deterministic own-window tests.
`AudioObservedChanged` reports an actual observed output-snapshot change on the UI thread, after
the Top Bar update. The application uses it to republish the provider's current state to an open
Quick Controls panel without polling or issuing a device intent. Duplicate audio observations are
deduplicated; injected `SetSnapshots` values share this path for isolated regression tests. Outbound
listeners may destroy the surface synchronously, so emission is the final member operation.
`Create(testBoundsPx)` requires `context.allowAppBar = false`.
`SetTaskbarReplacementState(available, active, suspended)` supplies runtime state without writing
preferences. The optional externally owned `context.menu` presenter lets isolated tests return a
selection without displaying a popup; otherwise the shared `ThemedMenuPresenter` is used, with its
native high-contrast fallback. The optional `context.menuWindowOps` injects foreground-window
operations for deterministic voice-typing tests; production uses the shared system implementation.
The final optional `context.fullscreenProbe` supplies a deterministic classifier for isolated tests;
otherwise runtime observation uses `platform::IsFullscreenWindow`, and `allowAppBar=false` disables
native observation. Existing context aggregate fields retain their positions.

The optional loaded settings service supplies initial visibility/clock/weather values and live
updates. A concrete ClockProvider switches second/minute resolution when `showSeconds` changes.
Weather settings are forwarded to the injected provider; with no weather provider there is no
weather transport. The application remains responsible for system time/resume notifications and
overall provider/service lifetime ordering.

## Model, layout and honest states

`TopBarModel.h/.cpp` separates snapshot formatting and layout from the HWND. Display IDs are stable
per kind: logo, weather, keyboard, network, volume, microphone, battery, clock. Every item has text,
tooltip, accessible name and explicit unknown/mute/cache/glyph hints. No SSID or profile name is read
or displayed by this module. Audio device names appear only in the relevant tooltip, never logs.

Clock/date use `status::ClockFormatter`, with system/12/24-hour format, optional seconds and date
style. Unknown time/temperature/percent/input language uses a neutral dash and explanatory name,
never a fabricated zero. Initializing, unavailable, unsupported and error states remain explicit.
Confirmed absent batteries hide when configured; failed battery readings remain unknown. When shown,
real external power without battery information uses a plug glyph. Microphone mute reflects the
endpoint only; recording detection is labelled unsupported.

Weather is visible only when both weather and its Top Bar item are enabled. Fresh/cached/failure
states remain distinct; cached values have a marker and labelled tooltip, with observation time and
Open-Meteo attribution. Temperature units are shown explicitly. Glyphs are original vector geometry,
including network/signal, speaker/mute, microphone/mute, battery/charge/plug and weather categories.
Wi-Fi uses three curved waves and a dot, with the actual Windows `signalBars` (0–5) mapped to
0–3 active waves. Missing strength keeps the waves outlined and shows an unavailable mark; it
never becomes a full-strength icon. Cellular retains five vertical bars. Ethernet uses a separate
monitor/stand glyph, and unknown/offline states remain distinct in their accessible descriptions.

The bar follows the configured 24–48 DIP height with 12-DIP side margins and configurable top margin.
Large accessibility text can raise the effective height, up to 48 DIPs. Logo stays left;
status/date/time align right. Weather follows its Left/Right preference. The keyboard supports
Compact and TwoLine, with its real layout name on the second line, clipped with an ellipsis when
needed (the full name stays in the tooltip); small heights/large text fall back to Compact.
The battery-percentage preference hides only the visual number, retaining its real value in the
tooltip. Width measurement uses DirectWrite. Weather collapses first, then
language. At extreme widths, network, microphone, volume and battery collapse in that order before
the clock is clipped. Hit rectangles are deterministic, half-open and never overlap.

## Window, interaction and accessibility

The inherited SurfaceWindow is topmost, a tool window and non-activating for pointer input. Glass,
typography, focus and high-contrast colors use the shared design tokens. Small value-change fades
finish immediately with reduced motion. Frames are requested for changes/finite animations only.

The shared glass material now keeps the backdrop visible without host blur as well: at the default
0.8 preference the tint alpha is 0.66 dark / 0.72 light, rather than the former 0.86 / 0.90. The host
backdrop brush mixes at 0.72 opacity so original desktop detail remains visible through its frost.
The drop shadow is masked outside the rounded panel; its silhouette no longer adds a second dark
fill behind the tint. Text and icons retain their own opacity. Maximum material opacity, Windows
Transparency effects off, and high contrast each produce an opaque panel. Windows 10 keeps the
documented per-pixel translucent fallback; it does not claim host-backdrop blur support.

Hover shows details; clock click requests calendar/notifications and battery click requests power
settings. Volume click/microphone click submit real
asynchronous ToggleMute requests, and volume wheel applies a bounded two-percent increment per
notch. Unknown volume is never used as a starting value. Failures show an explanatory tooltip.
The logo and every status item use shared themed context-menu models and emit requests; this
module does not launch commands. Arrow/Home/End/Tab/Shift+Tab navigation, Enter/Space activation,
Escape and Menu/Shift+F10 context menus are supported for action items. Context-menu selections
recheck current item/runtime availability after modal message pumping, rejecting stale actions.

The urgent T17 interaction correction makes the keyboard, network and visible weather item buttons.
Pointer, keyboard and UIA Invoke share the same guarded request path. Tooltips announce the action:
input-language switch (Win+Space), Windows network settings, or Aether's existing Weather settings.
The application owns these routes; this module never starts a shell process, switches the actual
layout or performs network/recording operations itself. Microphone primary activation retains mute;
right-click or Shift+F10/Menu on the focused microphone opens mute, Windows voice typing (Win+H),
and microphone privacy commands. `VoiceTypingRequested` is emitted only after verifying that the
application which was foreground before the menu has regained focus. A missing, stale or Top Bar
target, a third application taking foreground, or rejected/unverified focus restoration suppresses
the request; choose the intended text field again before retrying. The app owns the Windows key
chord. Aether adds no recording or speech-recognition engine. Hidden, disabled or full-screen-suppressed
surfaces reject actions; queued requests recheck current availability. Provider refreshes and delayed
control errors also cannot reopen a tooltip while the bar is suppressed or disabled.

While replacement is active, the optional notification-area button and logo menu request that the
app restore the Windows taskbar for 60 seconds. During runtime suspension they request Resume.
The app owns restoration confirmation, the timer and controller calls; the Top Bar neither changes
the actual taskbar nor persists a temporary suspension.

`accessibility::AccessibleSurface` exposes a toolbar with Button elements for the logo, input/network/
weather actions and audio/mic, and
Text elements for status. Bounds are physical screen pixels. Names/status, structure and focus events
follow model changes. UIA actions post to the window before opening a modal menu so automation calls
can return. WM_GETOBJECT is forwarded; WM_DESTROY disconnects providers before destroying the model.

## AppBar and full-screen policy

`SetUpdateAvailable(bool)` marks the existing logo with a visible up-arrow badge and "Update
available" tooltip/UIA status. Its menu offers "View available update…", emitting `UpdatesRequested`
so the application can show the existing offer without another check. Clearing the flag updates
the model immediately; repeated identical flags request no frames. This adds no desktop surface.
The own-window fixture now checks stable logo identity, immediate name/tooltip clearing and no
extra frames for a repeated flag. These assertions pass in the settled Release scoped run;
bounded render quiescence establishes the baseline. Aggregate GUI interference is recorded in S-011.

Runtime reservation follows `TopBarSettings::reserveSpace`: ABM_NEW, top-edge QUERYPOS/SETPOS,
POSCHANGED relayout and REMOVE on destruction. Explorer restart removes/re-registers safely, even
when both direct window broadcasts and application events forward the notification. Full-screen
AppBar notifications and an out-of-context foreground hook queue a current-foreground classification
before hiding/showing the surface and emitting the shared full-screen state. The raw AppBar opening/
closing boolean cannot override that result; it can be stale or describe a maximized framed window
when the native taskbar auto-hides. Full-screen state changes log only the new boolean, without
window identity, title or path. Positioning uses the primary monitor; DPI/display/work-area changes
relayout the window. The protocol follows Microsoft's
[documented AppBar lifecycle](https://learn.microsoft.com/en-us/windows/win32/shell/application-desktop-toolbars).
The pure `ComputeTopBarPlacement` includes the shared Bar elevation's complete shadow footprint in
the reservation. The HWND retains exterior drawing space; item/input rectangles remain inside the
panel. Glass uses explicit `Elevation::Bar` and `tokens.glass.backdropOpacity`, and the shared brand,
arrow/chevron geometry and round stroke tokens. High contrast suppresses decoration/fades; identical
display models and repeated runtime state request no new frames.

Tests set `allowAppBar=false`, which disables registration and the global foreground hook entirely;
they never change the real work area. Live reservation/full-screen/Explorer-restart behavior still
requires authorized desktop qualification, including Windows 10 and monitor/DPI changes.

## Verification

Unit tests cover unknown/initializing values, confirmed absence, cached/offline weather and
attribution, real signal/mute/percent values, visibility settings, localized seconds/date formatting,
narrow layout and hit testing. Integration creates the bar over its own opaque pattern backdrop,
including a blur guard band. Dark/light PNGs are written to `%TEMP%\AetherShellTests\topbar-*.png`;
pixel assertions compare actual composed RGB inside/outside the panel. Work area is read before and
after and must be unchanged; unchanged fake snapshots request no idle frames. A separate three-second
integration starts real providers read-only and prints only item text, without names/private detail.
No test performs an audio control or weather network request.

V2 regression additions compare the same panel over two different private opaque test patterns.
Ordinary glass must respond to that background change; high contrast must remain unchanged. These
checks distinguish real compositing from variance caused only by text/icons. Wi-Fi strength mapping
is covered by pure tests, and own-window variants render Wi-Fi with unknown strength, Ethernet and
cellular separately. On 2026-10-18, the Release `/WX` build in `build/wf-v2-visuals` passed six
graphics and eight Top Bar unit cases, then three glass and two Top Bar integration cases in a
serial desktop slot (11.70 seconds). Background-change mean RGB deltas were 26.827 for dark host
blur, 18.968 for light without blur, and 0.000 for the opaque high-contrast material. Work area and
idle checks passed. All eight generated private-window PNGs were opened and inspected: the glass
reveals the synthetic stripes, text stays legible, and Wi-Fi/unknown-Wi-Fi/Ethernet/cellular geometry
is visibly distinct. This does not claim an actual Windows high-contrast user workflow or physical
multi-monitor/DPI qualification. A separate agent reviewed the material/shadow changes read-only
and reported no actionable defect.

Executed on 2026-10-18: the private `build/wf-topbar2` build with warnings as errors passed seven
unit cases and two integration cases (2/2 CTest executables). Dark/light captures were opened and
visually checked: the complete date/time fits, the original two-ribbon brand mark matches the
branding geometry, and every captured pixel belongs to the private test windows. Capture guards
check foreign-window intersections before and after copying pixels. The final run confirmed
unchanged work area, read-only provider publication and steady idle frame counts.

Independent review identified lost rapid wheel increments before provider echo; the pending target
now accumulates those increments, with a fake-value burst/clamp regression. Screen reader
navigation, settings/menu/product workflows and real audio controls require later full-app
qualification; own-window render tests alone do not establish those behaviors.

A second agent reviewed final layout/semibold measurement, UIA action deferral and teardown,
provider ownership/dispatch, reservation opt-out, full-screen hooks and pending-volume echo handling.
No further actionable finding was reported in that read-only review; the reviewer did not rerun tests.

The T17 source added a ninth unit case for action metadata with unknown readings and a third native
case for pointer, keyboard and separate-MTA UIA signal delivery. Callbacks are synthetic and the
real work area must remain unchanged. The earlier eight-unit/two-native Top Bar result predates
these interaction changes; their later combined execution is recorded below.

## Integrated V2 behavior and validation

The integration worktree now contains the configured height/weather/keyboard/battery preferences,
shared glyph/material/elevation/menu integration, Clock/Power/Sound request signals, runtime
notification-area reveal/resume state and actions, Windows voice typing, and stale-menu/hidden-wheel guards. The app owns
all system actions. Added unit cases cover menu availability, updates, height/DPI/shadow reservation,
weather placement, keyboard fallback, percentage metadata, runtime notification actions, the Win+H
menu, and rejected/unverified/third-window focus handoffs. A fourth
own-window integration case injects a fake menu and foreground operations to verify the new signals,
focus restoration before the voice request, failed handoff suppression, stale-command rejection,
keyboard context-menu access, tooltip suppression after a provider refresh, repeated-state idle
behavior and unchanged real work area. `allowAppBar=false`; no real taskbar/audio/network/browser
action or input injection is called. Executed scopes are recorded below.

Follow-up review also guards asynchronous control-error tooltips after fullscreen suppression or
shutdown admission closes. The concrete audio providers have no injectable control backend in this
module; this small guard was inspected, and no real audio action was performed to exercise it.

Historical combined candidate verification (2026-10-20, TEST_STATUS S-022): Release /W4 /WX passes **20/20 unit
and 4/4 native cases**, including fake voice-target admission and request signals with no real keys
or audio change. Fresh dark/light/Wi-Fi/Ethernet synthetic captures were opened and inspected. Actual
Windows dictation in an editable field, permissions/UIPI and physical keyboard behavior remain manual.
Real Windows voice typing, text-field focus preservation, microphone consent and supported language
behavior still require an authorized desktop workflow. Fake foreground assertions establish request
ordering and rejection only; they do not establish successful dictation into a real application.

Follow-up correction (2026-10-21): raw `ABN_FULLSCREENAPP` now queues the same
canonical foreground check as WinEvent notifications. A fifth native case injects synthetic fullscreen
state while AppBar access is disabled: stale opening cannot hide a normal window, stale closing
cannot reveal over current fullscreen, and repeated messages emit no extra state changes. It checks
only its own HWND and unchanged work area; no external foreground window or actual taskbar is changed.
The S-022 20-unit/4-native result predates this addition.

Fullscreen-correction verification (2026-10-21, [TEST_STATUS S-023](../knowledge/TEST_STATUS.md)):
the complete candidate built in Release `/W4 /WX` with zero warnings and passed **18/18 unit
executables, 672 cases in 30.16 seconds**, then **14/14 isolated native executables, 123 cases in
127.00 seconds**. Top Bar contributes **20 unit and 5 native cases**, including the new stale-AppBar
notification regression. Native suites ran serially with isolated windows and unchanged-work-area
assertions. These receipts supersede the pending execution status of the implementation checkpoint;
they remain separate from actual desktop qualification.

The subsequent owner-authorized bounded S-023 desktop retest launched freshly copied corrected
binaries normally with no earlier Aether instance (candidate SHA-256
`524afc253c84cb28e1523e15b531850f2a80ebf6defa5166748bf2ece221d0bb`). Both Aether HWNDs remained
visible while the native taskbar was hidden. The work area changed from `[0,0,1920,1080]` to
`[0,93,1920,969]`; an already-maximized owned synthetic framed window reflowed its visible DWM frame
to those exact bounds after two `WM_SETTINGCHANGE` messages and reverted on exit. Normal CLI quit
stopped candidate/helper, removed the baseline and restored the initial visible native taskbar,
enabled auto-hide and original work area exactly.

The same bounded session confirmed enabled pin import/Explorer default and five pins including File
Explorer through the project's own Settings accessibility and configuration flags. It did not verify
running-icon display or temporary reveal/resume. No screenshots, owner-application control,
Explorer restart, audio/dictation or startup mutation occurred. Real Chrome, framed-fullscreen
variants, exclusive-mode games, Windows 10 and physical monitor/DPI workflows remain unqualified,
as do actual voice typing and audio controls.

## Glass logo menu (T47 / FR-037 / D42)

The logo menu and the item menus use one persistent `GlassMenuPresenter` per Top Bar (an injected
`TopBarContext::menu` still wins, as in the isolated tests). The menu model keeps every command id,
label, order and availability rule, including "Show Windows notification area for 60 seconds"; the
logo entries gain icons (Search, Settings, Taskbar, Download, Updates, About, Power). Menus opened from
the keyboard (Applications key, Shift+F10, Enter/Space on the logo) start with their first entry
selected; an open menu is cancelled when the bar hides for full screen or is destroyed. The stale
command revalidation after the modal loop is unchanged.

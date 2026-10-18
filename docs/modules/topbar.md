<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Ibrahim Bouillet -->
# topbar — native floating system status surface

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
`QuitRequested`, and `FullscreenChanged(bool)` for forwarding to the Dock. `Relayout`,
`OnExplorerRestarted`, `SetTheme(tokens, motion)` and `SetFullscreenHidden` support app/system wiring.
`SetSnapshots`, `SetSettings`, `Items` and `RenderCount` support deterministic own-window tests.
`Create(testBoundsPx)` requires `context.allowAppBar = false`.

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
Wireless bars are filled only from a real `signalBars`; missing strength has an unavailable mark.

The bar is 32 DIPs high with 12-DIP side margins and configurable top margin. Logo stays left;
status/date/time align right. Width measurement uses DirectWrite. Weather collapses first, then
language. At extreme widths, network, microphone, volume and battery collapse in that order before
the clock is clipped. Hit rectangles are deterministic, half-open and never overlap.

## Window, interaction and accessibility

The inherited SurfaceWindow is topmost, a tool window and non-activating for pointer input. Glass,
typography, focus and high-contrast colors use the shared design tokens. Small value-change fades
finish immediately with reduced motion. Frames are requested for changes/finite animations only.

Hover shows details; clock click shows the long date. Volume click/microphone click submit real
asynchronous ToggleMute requests, and volume wheel applies a bounded two-percent increment per
notch. Unknown volume is never used as a starting value. Failures show an explanatory tooltip.
The logo opens a native popup menu and emits requests; it does not launch arbitrary commands.
Arrow/Home/End/Tab navigation, Enter/Space activation and Escape are supported for action items.

`accessibility::AccessibleSurface` exposes a toolbar with Button elements for the logo/audio/mic and
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
AppBar notifications and an out-of-context foreground hook hide/show the surface and emit the
shared full-screen state. Positioning uses the primary monitor; DPI/display/work-area changes
relayout the window. The protocol follows Microsoft's
[documented AppBar lifecycle](https://learn.microsoft.com/en-us/windows/win32/shell/application-desktop-toolbars).

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

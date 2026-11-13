# Changelog

All notable changes to Aether Shell are documented here. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/) and the project uses
[Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

## [0.4.0-alpha.4] — in progress, not published

Alpha 4 (D62–D73). Its test builds keep this number until the owner calls Alpha 4 complete.

### Added

- **The Top Bar as text on the desktop** (D71): Settings → Top Bar → Presentation → *Text on the desktop* removes the bar
  and writes its items at the very top of the screen, straight over the desktop picture, in light or dark ink chosen from
  the picture under each side, with a soft halo. The strip stays free of windows. With Auto-Hide the text comes back on a
  strip of glass while a window is under it. *Floating bar* stays the default.

- **Five languages** (D70): English, 中文 (Chinese, simplified), हिन्दी (Hindi), Español and Français, everywhere: bars,
  menus, tooltips, Search, Quick Settings, Settings, setup, the updater, messages. Settings → Language (a new page) follows
  Windows' display language by default or keeps the one you choose; the change applies at once, and a chosen language also
  writes the Top Bar's date. Windows' tools and Settings pages are named in it and found by their name in any of the five.
  Names of applications, files and networks are never translated. The welcome can greet in Spanish ("Hola").
- **The documentation in the five languages:** README, user guide and installation guide, with a language bar.
- **Ten languages** (D74): Portuguese, Russian, Arabic, Bengali and Urdu join the five. "Follow Windows" now goes through
  every display language the person ranked in Windows, not only the first; the language chosen in Settings is also read by
  setup, the updater and the launcher, which read the settings file every version shares. Arabic and Urdu are shaped and
  aligned right to left. Only the interface is translated into the new five; their documentation is still English.

- **Two styles, Liquid and Grain** (Settings → Appearance → Style). Liquid draws the Dock and the Top Bar as glass over the
  page behind them, live: its colours and shapes show through, softened and a little livelier, kept in a band of brightness
  where the bars' text reads over white, black or anything between, with a bright rim, light gathered in its edges and a fine
  outline; icons cast their shadow on it and an application that asks for attention casts a red light. Its blur layer can be
  shaped in Settings → Appearance: how much it covers, and its color. Grain is the frosted glass as before, and stays the
  default.
- **The arrival** after the greeting: a drop with the Aether mark floats, divides in two, and its halves strike the top and the
  bottom of the screen and spread into the Top Bar and the Dock, in the chosen style. It plays again when another style is
  chosen and when Aether Shell starts without a greeting.
- **Two previews** of the styles in Settings → Appearance: on screen at once, they play the arrival, then rest on the Dock and
  the Top Bar.
- **Search finds everything by name, as Start does:** Windows' tools in either language and by their command (Device
  Manager by "devmgmt"), Windows' Settings pages, and the files and folders in your own folders, from Windows' own index
  (names only, never contents), each with what it is and where.

### Changed

- **Rounded corners for every maximized window, without touching the application** (D72, ADR 0014): Aether no longer
  gives other applications' windows a region or changes their material. Small windows of its own, directly above the
  maximized window, show the live desktop in the four corners and draw the thin rim. Browsers, File Explorer, packaged
  and elevated applications are all rounded, and keep their own caption buttons. If Aether stops, nothing is left on any
  window; a clip left by an earlier version is removed at start.
- **Lighter at rest and when switching style** (D69), with the same pixels and motion: the Top Bar no longer redraws its
  whole content about ten times a second with a seconds clock (only the digits that change are drawn, once a second);
  unchanged backdrops are not sent to the compositor again; repeated labels keep their text layout; Auto-Hide reads the
  windows once for both bars; the Settings previews wait for the full-screen arrival instead of rendering under it; the
  arrival draws each frame in one full-screen pass instead of three; the desktop picture and the arrival's full-screen
  frame are given back after use instead of staying in memory.

### Fixed

- **The weather loaded nothing** (D75): Open-Meteo's compressed answers made WinHTTP's decoder abort mid-body ("operation
  aborted"), so every request failed as a network error although the service was reachable. Requests to Open-Meteo now ask for
  the plain answer (under 1 KB), the log names the real failure, and a Network test covers both Open-Meteo requests.
- **Rounded corners no longer lag when a window comes back** (D76): a monitor whose front window stopped being maximized used to
  lose its corner windows and rebuild them (a window, two shapes and about forty DWM thumbnails: 40 to 100 ms) when a maximized
  window came back. The frames are now kept hidden and shown again (about 9 ms), shapes are built in one call, the two windows
  are stacked in one operation, and the look at the windows stops once every monitor has its front window.
- **A dragged Dock icon no longer leaves the Dock** (D73): pulled past an end, the Dock stretches after it like a rubber
  band, by a little over twenty pixels at most, and springs back with a bounce when the icon is let go.
- With Auto-Hide on, the Dock and the Top Bar left at once after the greeting at an unlock, because the page left open covered
  them; they now stay out a few seconds, as when they are used.
- In Search, Enter opened the row under the mouse pointer instead of the one chosen; the pointer now only lights a row, and
  opens it when clicked.

## [0.3.5-alpha.3](https://github.com/ibrahimbouillet/AetherShell/releases/tag/v0.3.5-alpha.3) — 2026-11-05

Alpha 3, version 3.5, validated by the owner and published as the Latest release (D61). It contains everything prepared as 3.4 (D60),
which was built and installed as a test build on 2026-11-01 but never published. Release notes:
[docs/releases/0.3.5-alpha.3.md](docs/releases/0.3.5-alpha.3.md).

### Added

- **Setup asks for the welcome's name** (D61): an optional field on the Options page, saved in the user's own settings. Installing over
  an existing version shows the name already saved; an unchanged name writes nothing.
- **The welcome notes in the log how smoothly it was written** (D61): frames, frames per second, longest gap, and whether the background was
  the picture or a plain colour. Never the words or the name.

### Changed

- **The welcome's pen costs a twentieth of what it did per frame** (D61): the soft dark edge under the ink is kept, and blurred again only
  around the newest ink, instead of the whole greeting again for every frame. Compared pixel by pixel, the result is the same to 1/255.
- **The README presents five real desktops** in a slideshow, with a strip of pictures that open at full size.
- **The Top Bar answers only to the very top of the screen** (D60): with Auto-Hide on, the pointer must be on the top row of pixels, with no
  margin, for a quarter of a second. One pixel lower belongs to the application: its tabs and close button stay free.
- **The welcome's writing flows** (D60): 2.2 s instead of 1.5 s, with a speed that changes slowly, a gentle start and landing, and ink that
  settles to its width just behind the pen. The letters are unchanged.
- **Search shows every match** instead of the best eight, and answers sooner; only rows on screen load an icon.

### Fixed

- **The welcome showed a black screen when the wallpaper's file was gone** (D61): Windows names no picture for the monitor once its file is
  moved or deleted, and Aether took that for a plain colour. It now falls back to the copy Windows keeps of the picture on screen.
- **Every panel closes when you use anything else, and on Escape** (D60): Quick Settings could stay open over the page you were using until a
  second click. Escape now closes at once, also from a detail view or with text typed in Search.
- **Search finds Windows' own tools** (D60): Device Manager, Services, Event Viewer, Computer Management, Task Scheduler, Disk Management and
  the Control Panel's items.
- **Search forgets what you typed when it closes** (D60): every opening starts empty.
- **Settings no longer shows a page twice over** (D60): after a check for updates, or anything else that moved a page's rows, scrolling could show them
  doubled, with old words in their place.
- **Updates says what is true** (D60): a newer release without a signed manifest used to read "The update could not be completed. Failure:
  Validation". It now names the version and how to install it. Other failures are said in words.
- **The README's pictures show on GitHub**: they had never been committed.

## [0.3.3-alpha.3](https://github.com/ibrahimbouillet/AetherShell/releases/tag/v0.3.3-alpha.3) — 2026-11-01

Version 3.3, published as the Latest release (D59). Release notes:
[docs/releases/0.3.3-alpha.3.md](docs/releases/0.3.3-alpha.3.md).

### Added

- **Handwritten welcome** (FR-041, D53, D54): an optional greeting after sign-in and unlock, filling the screen
  with original strokes and a calligrapher's pen over your blurred desktop picture, with the bars held back until it ends, in French or English, with an optional local
  name. It takes about two seconds. On by default, skippable. After a lock it is already there when you unlock, before the desktop.
- **Wi-Fi networks in Quick Settings** (D56): see the networks around you and their signal, and join one, with its
  password if it needs one. Windows keeps the password. Names and passwords are never logged or sent.
- **Complete touch** (FR-040): direct pan, fling and elastic return across Settings, menus and Search,
  tap, hold and page gestures, and a swipe from the screen edge for hidden bars. Not yet tried on a
  physical touchscreen.

### Fixed

- A window that stops responding can no longer hold up rounded corners on other windows, and is not
  written to while unresponsive (AS4-03). A maximized window filling the monitor keeps its corners when
  both bars auto-hide (AS4-06). Far less work on window events and work-area changes (AS4-07, AS4-08).
- A release acts only on what was pressed in the Top Bar and Search; wheels and touchpads travel equally
  far whatever their packet size (AS4-11–AS4-15).
- Startup: Settings says what Windows will really do at sign-in and Apply repairs a lost registration; a failed save
  puts it back (AS4-18, AS4-21). Installed sessions can change sign-in startup after setup, over-long startup commands and
  custom data folders are refused visibly, the launcher can no longer wait forever for a child, and session
  notifications recover from an early failure (AS4-16, AS4-17, AS4-19, AS4-20, AS4-22).
- Graphics: reattached surfaces are sized correctly and idle surfaces recover from a lost device with a
  bounded retry (AS4-24, AS4-25).

The reported blank application windows and unreliable sign-in startup are **not** confirmed fixed: their
causes have not been reproduced.

### Changed

- **Sliders go where the hand goes** (D55): a press anywhere on a Settings slider moves it there and keeps
  dragging, a finger moves it sideways, and a pan up or down over it scrolls the page. A held slider applies
  when you let go, so a slow setting no longer interrupts the movement. Brightness follows the thumb, and a
  value you asked for no longer returns to the old one while Windows catches up.
- **Quick Settings scrolls under the finger** (D55): the pages follow a pan up and down, and settle on release.
- **Rounded corners only between the bars** (D56): with both bars hidden, a maximized window takes the whole screen,
  square. Browsers keep their rounded corners between the bars instead of losing them for good.
- **Back on the desktop, both bars return** (D59), even with windows still behind it.
- **Top Bar Auto-Hide comes out after half a second** (D52, D54, D56, D58, D59): the pointer must rest for half a second
  where the bar appears, at the top of the screen, before the bar returns, and moving away sooner cancels it, so a quick visit to
  a button near the top no longer puts the bar over it. A small movement of a resting hand does not restart
  the wait. A monitor above the primary one no longer counts as that edge. The Dock's reveal and the Top
  Bar's other reasons to show are unchanged.

## [0.3.0-alpha.3](https://github.com/ibrahimbouillet/AetherShell/releases/tag/v0.3.0-alpha.3) — 2026-10-27

Third alpha, Alpha 3 (owner decisions D48 and D49): the V3 design and interaction work since Alpha 2.
Release notes: [docs/releases/0.3.0-alpha.3.md](docs/releases/0.3.0-alpha.3.md). The owner accepted
this version as Alpha 3 and published it as the Latest release (D50).

### Added

- **Auto-Hide for the Dock and the Top Bar** (D47): two independent, persisted settings, off by
  default (Settings → Dock → *Automatically hide the Dock*, Settings → Top Bar → *Automatically hide
  the Top Bar*). An enabled bar slides away while an application window covers it and slides back
  when the pointer rests on its screen edge (bottom for the Dock, top for the Top Bar). It stays while
  in use, while one of its menus or panels is open, and on the uncovered desktop. Auto-hidden bars
  reserve no screen space, so maximized windows use the full screen; full-screen suppression keeps
  priority.
- **Notifications and calendar** (D45): the Top Bar date and time opens the Windows notification
  center (Win+N on Windows 11, Win+A on Windows 10); the clock menu adds the calendar (Win+Alt+D).
  Aether never reads, clears or badges notifications.
- **Hidden icons** (D45): a Top Bar chevron (Settings → Top Bar → *Show hidden icons button*, on by
  default) opens Windows' own notification-area overflow directly below it, in front of other
  windows. It is a toggle: the arrow points up while the overflow is open, and a second click closes it.
- **Aether Quick Settings** (D44): the grouped network, audio and battery action opens a rebuilt
  quick-controls panel with split tiles, two pages, sliders, conditional media controls and detail
  views for Wi-Fi, Bluetooth, accessibility and sound output. Switches that Windows does not expose
  to applications open the matching Windows Settings page. *Use Windows Quick Settings (Windows 11)*
  routes the action to Windows' own panel instead.
- **Dock window previews** (T42/T43): hovering a running app widens its marker and, after a short
  dwell, shows a glass card with genuine previews of its windows. Each preview has a close button for
  that window only; pointer travel into the card keeps it open.
- **Rounded maximized windows** (D40/D46, ADR 0012): while Aether replaces the taskbar, eligible
  maximized windows are clipped to rounded corners, with a thin continuous rim drawn by a
  click-through window beneath them. Placement and maximize state are preserved and every change is
  undone on exit. A maximized Settings window draws its own rounded frame and caption buttons with
  native Snap layouts, system commands and accessibility.

### Changed

- **One glass design across Aether** (D41/D42): Settings, the logo and context menus, setup,
  maintenance, the updater and confirmations share the neutral frosted-glass material, controls,
  typography, glyphs and motion. Native text editing, keyboard input and accessibility are kept.
- **Fluid Settings** (D43): Settings stays coherent while it moves and scrolls (spring scrolling,
  retained layers, animated hover), and its title strip shares the window material while keeping the
  native caption buttons, Snap, system menu and resizing.
- A more compact Dock (3 DIPs less top padding, same icon sizes and bottom gap). Running markers
  animate from dot to pill on hover and activation, and hover cards fit their thumbnails.
- The Top Bar's focus ring appears only after keyboard navigation, as Windows shows focus cues; a
  click no longer outlines the chevron.
- Dock and Top Bar input regions are narrowed to their visible panels.

### Fixed

- Grey rectangular shadow fragments around raised Dock icons on light windows.
- Rectangular system backdrops behind rounded maximized-window corners. Chromium windows that
  re-apply Mica are clipped again, so they no longer stay square behind other windows.
- Settings now appears even when it is the first window of a process that was started hidden.
- Executable-only desktop windows stay on their unique pinned Dock icon.
- Preview close gestures cancel when capture is lost or the window list changes; keyboard selection
  follows the chosen window.
- Interrupted Quick Settings detail transitions continue from where they are; precision scrolling,
  media arrival and press-and-release targets behave correctly.

### Known limits

Edge reveal and Auto-Hide with real applications, the hidden-icons toggle on every Windows build,
multi-monitor and DPI changes, Windows 10 and some window-rendering families are not yet qualified
on real hardware. See [KNOWN_LIMITATIONS](KNOWN_LIMITATIONS.md).

## [0.2.0-alpha.2](https://github.com/ibrahimbouillet/AetherShell/releases/tag/v0.2.0-alpha.2) — 2026-10-23

Second public alpha, including the previously local V2 candidate below.

- Fresh setup discloses sign-in startup with an opt-out; existing choices and Task Manager disables
  remain respected. The stable installed launcher retries at most three abnormal exits.
- Observed audio changes reach an open Quick Controls slider immediately, without waiting for the
  periodic media refresh or issuing new audio commands.
- Native reusable capture/review tools retain strict local privacy boundaries. Fresh presentation
  media remains pending and is not bundled with the installer.
- Exact-revision Windows CI, Release `/W4 /WX`, package audit and scoped actual installed
  startup/recovery checks are recorded in HANDOFF and TEST_STATUS S-034.

### V2 changes

- Reversible visual taskbar replacement enabled by default (D23), preserving explicit opt-out, with Desktop Apply controls,
  a separate recovery helper, status/errors, and existing-instance quit/restore commands.
- Dock bottom-margin correction and mandatory animated-footprint AppBar reservation in replacement
  mode, keeping maximized application content above the Dock.
- Balanced Start, Search and Task View icon sizes with application icons by removing duplicate
  padding from their Dock drawing boxes; shared status-bar glyph sizes are unchanged.
- Framed maximized windows no longer suppress both Aether bars as fullscreen. Raw AppBar hints
  recheck current foreground policy, and registered Dock activation/position notifications reach
  the shell without overriding the host's temporary-reveal policy.
- Read-only best-effort Windows pinned-shortcut import with retained launch arguments and deduplication.
- More visible glass translucency, shadow restricted to the outside, opaque Windows-transparency-off
  and high-contrast fallback, and distinct Wi-Fi waves/Ethernet/cellular glyphs.
- Taskbar recovery without Aether Shell running (D21):
  - the taskbar state is saved before anything is hidden;
  - `--restore-taskbar` restores it with no instance running, and setup repair/uninstall restore it
    too;
  - restoration always shows the taskbar (D22).
- Fixed: replacement mode never activated on a real desktop because the Dock's first AppBar
  reservation was rejected.
- Owner-approved D19 / ADR 0009 records the narrow taskbar-hiding exception. Explorer remains intact;
  native Windows Settings takeover is unsupported. Full V2 delivery qualification is pending.
- Interrupted batch recovery adds shared theme/glyph/menu/transition primitives, compact Dock tiles,
  pin and running-item reordering, profile-aware presentation, one-time Explorer pin initialization,
  additive pin sync with dismissal/revision protection, and an Appearance settings page.
- Top Bar actions include keyboard/network/weather/clock/power/sound access and temporary native
  taskbar reveal with manual or 60-second resume. The Dock hides only after native restoration.
- The microphone menu requests Windows voice typing (Win+H) in the previous application; endpoint
  mute and microphone privacy settings remain separate actions. Aether does not record speech.
- Review fixes reject unknown profile relaunches, validate icon-file ancestors, reject foreign menu
  item pointers and stale modal actions, and prevent hidden animated Search rows from activating.
  Fresh combined native/desktop qualification is in progress; see TEST_STATUS S-022.

## [0.1.0-alpha.1](https://github.com/ibrahimbouillet/AetherShell/releases/tag/v0.1.0-alpha.1) — 2026-10-18

First manual-install native development alpha.

### Added
- Modular CMake build with presets, a Visual Studio environment build script and CI.
- Core library (results, strict JSON, cancellation, worker pool, dispatcher, signals, atomic files)
  and rotating diagnostic log.
- Win32 platform layer (DPI, monitors, system events, single instance, instance channel, message loop).
- Composition graphics layer: Direct2D on Windows.UI.Composition, glass material with Windows 11
  backdrop blur and a translucent fallback, design tokens, interruptible animations, tooltips.
- Real clock/battery/input/network/audio providers and opt-in Open-Meteo weather with bounded cache.
- Async settings persistence, migrations, startup consent and configurable global shortcuts.
- Immutable application search index, native search/settings windows and UI Automation surfaces.
- Native animated Dock and separate Top Status Bar with original branding and synthetic-window captures.
- Application lifecycle, versioned install/repair/uninstall, health-checked update/rollback and setup.
- Public specification, traceability, handoff, end-user guides and clean-public-export policy.

### Qualification
- Complete Release /WX build and public Windows CI pass. Local evidence: 426 unit cases,
  91 integration cases across scoped serial runs and 32 release-tool cases. Aggregate desktop
  interference/refusals remain recorded in TEST_STATUS; full Version 1 acceptance is open.
- Setup and checksums are published. Automatic updates are unavailable for this alpha; the unsigned
  manifest remains local. See KNOWN_LIMITATIONS.

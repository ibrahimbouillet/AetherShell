# Known limitations

This is a development alpha, not a fully qualified Version 1 release. The requirements in
PROJECT_SPEC remain the intended acceptance criteria; this file does not waive them.
The current release is Alpha 3, `0.3.0-alpha.3` (D48–D50, published 2026-10-27); `0.2.0-alpha.2`
and `0.1.0-alpha.1` remain available. Earlier deliveries do not qualify later versions. Build, package and CI
receipts are recorded only when executed in HANDOFF and `docs/knowledge/TEST_STATUS.md`.

- The corrected Dock light native fixture intermittently exposes its own HWND as UIA Pane rather
  than the required Toolbar (S-024); root-type/invoke assertions remain strict. Synchronous provider
  teardown is under investigation and its relationship to this failure is unproven. Passing earlier
  runs and visual captures do not close this accessibility qualification gap.

- V3 Dock window previews (T43, since Alpha 3) show what DWM composes for each
  window. Protected/DRM content may appear black, and some minimized windows offer only their
  caption, shown as an explicit placeholder. An elevated window can refuse the close request (UIPI);
  the card reports the refusal and keeps the window. DWM's above-content thumbnail stacking was
  verified on Windows 11 only. Media play/pause on the card is not implemented yet, and the owner's
  real-desktop acceptance of the preview card and the calmer marker motion is pending.
- Windows 10 and ARM64 have not been runtime-tested in this continuation.
- Multi-monitor DPI/removal, Explorer restart, full-screen suppression and AppBar interactions need
  broader real-desktop qualification. Automated window tests never alter the real work area.
- Real external application launches/UWP frame attribution, audio changes and startup registration
  are not exercised by automation; tests use fakes and read-only real status sampling.
- The first live official signed-update cycle requires an owner-controlled release signing key.
  Unsigned manifests must stay local. Authenticode signing is not configured.
- The initial visual identity is an original proposal for alpha, pending owner direction.
- Product-wide startup/memory/idle CPU release budgets remain open (Q-001). Search latency has a
  separate measured engineering target.
- D34 adds disclosed installed sign-in startup and bounded abnormal Windows-exception relaunch.
  Windows controls startup scheduling; Aether cannot guarantee being first, hiding the native bar
  before its own surfaces are ready, or surviving every failure. Intentional Quit, ordinary errors,
  Task Manager disable, exhausted retry budgets and uninstall remain stop paths. Native recovery
  may briefly restore the Windows taskbar during a crash/restart. No realtime/OS-critical priority
  or memory pinning is used. Actual reboot timing requires a separate observed sign-in test.
  A child stuck before its IPC listener is ready can make maintenance/stop fail with a bounded
  timeout; maintenance preserves the installation rather than deleting files under that live child.
  Advanced custom `--data-dir` sign-in startup is not qualified: the Run entry launches with the
  normal data directory. Use the standard data directory for installed sign-in startup. Multiple
  installations also share the current per-user application IPC namespace; cross-install shutdown
  isolation is not qualified.
- Temporary-root installer cancellation, repair and maintenance self-uninstall tests pass. Broader
  real-user upgrade/recovery and interrupted-install qualification remain open.
- Full-screen activity and foreign topmost overlays can interrupt interactive fixtures. Capture guards
  refuse unsafe crops; tests must run on a quiet desktop and never disable those guards.
- The app-to-updater process handoff has source review, not a live signed-release execution.
- V2 visual taskbar replacement defaults on (D23), preserves explicit opt-out, and can be changed
  with Apply in Aether Shell Settings → Desktop. It uses documented auto-hide plus a compatibility seam for shell-owned
  window visibility; it is not official taskbar registration or native Windows Settings integration.
  The Windows taskbar settings page continues to control Windows.
- A recovery helper restores native taskbar state after parent exit/failure, and the app observes
  helper exit with an initial-baseline fallback. The baseline is saved to disk before anything is
  hidden. If both fail, `--restore-taskbar` (without a running instance), setup repair and uninstall
  restore it (D21).
  - Native Settings changes remembered only inside a killed helper are not in that saved baseline
    and can be lost.
  - Old-revision Windows 11 checks cover auto-hide off and Explorer restart (S-021); this combined
    candidate, monitor changes and interrupted recovery still require qualification.
  - If only the app crashes and its helper restores the taskbar, the saved baseline remains until the
    next start or `--restore-taskbar`. A native auto-hide change made in between is reverted once.
  - The D21 path is unit-tested with a simulated taskbar and was checked once on a Windows 11 desktop
    whose taskbar already used auto-hide (TEST_STATUS S-016). Restoration always shows the Explorer
    taskbar windows (D22).
- Replacement mode reserves Dock space for maximized applications; manually positioned windows can
  still occupy that area. Other-AppBar coexistence and full-screen recovery need desktop checks.
- Windows pin import is read-only and best effort. It reads supported shortcut files, preserves
  Aether pin metadata/arguments and existing pins, but may miss Windows 11 pins or their native order.
  It does not reproduce the Windows notification area, system menus or every native taskbar feature.
- `--restore-taskbar` and `--quit` forward to an existing Aether instance and never start one. With
  none running, `--restore-taskbar` restores the saved taskbar state itself. Without a saved state it
  only makes the taskbar visible and leaves auto-hide unchanged. If no recovery path works, end Aether
  Shell's processes in Task Manager, then restart Windows Explorer. See the user guide before
  enabling this alpha mode.
- The candidate now implements distinct pinned/running drag order, late profile icon metadata and
  one-time Explorer pin initialization. T17 remains open until fresh combined interaction and owner
  desktop checks; source changes and synthetic unit tests do not resolve the reported real-use issue.
  Profile avatars require supported window icon metadata; no browser profile database is read.
- Urgent Top Bar source changes add keyboard/input-language, network-settings and weather-settings
  request actions plus a microphone privacy menu; their new test/product routes are still being
  qualified. Audio output mute/volume and microphone endpoint mute remain separate from recording
  and microphone-in-use detection. The owner clarified right-click applies to Dock icons, avatars to
  Chrome profiles, and requested Windows voice typing in addition to mute/settings.
- `topBar.openWindowsFlyouts` is stored for compatibility but is not a routing switch. In V3 source
  (D44, local only, not in Alpha 2) the grouped network/volume/battery action opens Aether's own Quick
  Settings panel unless `topBar.windowsQuickSettings` is explicitly chosen; other actions keep fixed
  targets (network/sound/power/privacy Settings and the calendar key chord). The microphone menu
  requests Windows voice typing after restoring application focus. Actual dictation availability/
  focus/permission remains a manual check; automated tests never open it.
- The V3 Aether Quick Settings panel (D44, since Alpha 3) has no documented global switch for
  airplane mode, energy saver, night light, mobile hotspot or nearby sharing: those tiles open the
  matching Windows Settings page and say so. Direct output-device switching is not implemented (the
  output view offers mute, the volume mixer and Sound settings). Wi-Fi shows connection state, never a
  network name (FR-008). Narrator/Magnifier/On-screen keyboard/Live captions are started with their
  documented Windows shortcuts, whose effect is not read back. Real radio, audio, brightness,
  accessibility and media actions from this panel need owner checks; automated tests use synthetic
  state and never perform them. Owner visual/motion acceptance is pending.
- V3 Settings (D43, since Alpha 3) integrates its title strip with the Acrylic material on
  Windows 11 22H2+; Windows 10, transparency off and high contrast keep a standard caption. The
  inactive window shows Windows' flat Acrylic fallback. Scrolling coherence was measured on one
  Windows 11 configuration at 150 % scaling; physical multi-monitor DPI, IME composition and
  screen-reader walkthroughs remain open.
- V3 notifications and hidden icons (D45, since Alpha 3): the Top Bar date/time sends the
  documented Windows shortcut (Win+N on Windows 11, Win+A on Windows 10, whose calendar stays on
  Win+Alt+D in the clock menu). Aether reads no notifications and shows no badges. The hidden-icons
  chevron opens Windows' own notification-area overflow through UI Automation on Explorer's overflow
  button (recognized by its chevron glyph); a replaced taskbar stays hidden. Explorer first shows the
  overflow above its hidden taskbar; Aether then moves that window (position and z-order only) centred
  below its chevron, about one frame later. Checked live on one Windows 11 build (26300); Windows 10,
  other builds and other monitors are unverified. If Explorer shows no such button (for example when no icon is hidden), the chevron
  reports that it is unavailable and nothing else happens.
- V3 rounded maximized windows (D45/D46, since Alpha 3): an eligible maximized application is
  clipped about one DIP inside its rounded outline and a click-through Aether ring beneath it draws the
  thin perimeter. Applications whose caption buttons are drawn by Windows lose them while clipped,
  and for standard captions Windows may draw its own caption over the top of the outline (square
  top corners, no top rim) while the window is active; applications drawing their own caption are
  not affected. Their own region/material choices still win; protected, elevated, full-screen and
  unsupported windows stay square. The ring and corners are not antialiased and follow the Windows app theme.
  Aether Settings draws its own caption buttons and turns opaque while maximized. Broad application
  compatibility, physical multi-monitor and 192 DPI behavior still need checks.

- V3 Auto-Hide (D47, since Alpha 3, off by default): the Dock and the Top Bar hide while an
  application window overlaps them and return at the bottom or top edge of the primary monitor. An
  auto-hidden bar reserves no screen space, so a revealed bar floats over maximized windows rather
  than pushing them aside (as Windows' auto-hide taskbar does). With a monitor below or above the
  primary one, that edge is not a hard stop: the pointer has to rest on it briefly. The pointer and
  window bounds are sampled every 50 ms only while Auto-Hide is on. Edge reveal, animation and real
  applications still need the owner's check; multi-monitor setups are unverified.

Record new reproducible issues in the official repository without personal paths, account details,
credentials, unrelated application data or whole-desktop screenshots.

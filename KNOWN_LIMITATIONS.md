# Known limitations

This is a development alpha, not a fully qualified Version 1 release. The requirements in
PROJECT_SPEC remain the intended acceptance criteria; this file does not waive them.
The current V2 candidate is `0.2.0-alpha.1`; earlier `0.1.0-alpha.1` delivery receipts do not qualify
this candidate. Its full build, package, CI and publication gates are recorded only when executed.

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
- Temporary-root installer cancellation, repair and maintenance self-uninstall tests pass. Broader
  real-user upgrade/recovery and interrupted-install qualification remain open.
- Full-screen activity and foreign topmost overlays can interrupt interactive fixtures. Capture guards
  refuse unsafe crops; tests must run on a quiet desktop and never disable those guards.
- The app-to-updater process handoff has source review, not a live signed-release execution.
- V2 visual taskbar replacement is optional and default-off, with explicit Apply consent in Aether
  Shell Settings → Desktop. It uses documented auto-hide plus a compatibility seam for shell-owned
  window visibility; it is not official taskbar registration or native Windows Settings integration.
  The Windows taskbar settings page continues to control Windows.
- A recovery helper restores native taskbar state after parent exit/failure, and the app observes
  helper exit with an initial-baseline fallback. The baseline is saved to disk before anything is
  hidden. If both fail, `--restore-taskbar` (without a running instance), setup repair and uninstall
  restore it (D21).
  - Native Settings changes remembered only inside a killed helper are not in that saved baseline
    and can be lost.
  - Real Explorer restart, auto-hide, monitor changes and interrupted recovery remain unqualified.
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
- The owner reports that hold/drag Dock reordering fails in actual use despite existing pointer
  handling and scoped model tests. T17 requires reproduction and a persistence/interaction fix;
  it is not marked resolved. Chrome profile avatars and a guaranteed pinned File Explorer entry
  are also requested follow-up work, not established current features.
- Urgent Top Bar source changes add keyboard/input-language, network-settings and weather-settings
  request actions plus a microphone privacy menu; their new test/product routes are still being
  qualified. Audio output mute/volume and microphone endpoint mute remain separate from recording,
  dictation or microphone-in-use detection, which are not implemented. The owner clarified right-click
  applies to Dock icons and avatars to Chrome profiles; exact extra menu/voice actions remain open.

Record new reproducible issues in the official repository without personal paths, account details,
credentials, unrelated application data or whole-desktop screenshots.

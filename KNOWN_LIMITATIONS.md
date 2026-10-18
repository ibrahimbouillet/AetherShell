# Known limitations

This is a development alpha, not a fully qualified Version 1 release. The requirements in
PROJECT_SPEC remain the intended acceptance criteria; this file does not waive them.

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

Record new reproducible issues in the official repository without personal paths, account details,
credentials, unrelated application data or whole-desktop screenshots.

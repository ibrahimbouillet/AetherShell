# Known issues and limitations

## Platform limitations (by design, documented APIs only)
- Backdrop blur needs Windows 11; Windows 10 gets a more opaque tint (ADR 0001/0002).
- "Microphone in use" has no documented cross-version API → reported as unsupported.
- Input language of other processes has no system-wide change notification → foreground-change
  hook plus a 1 s HKL comparison.
- Wi-Fi SSID is not shown (location-privacy restrictions on Windows 11 24H2+).

## Current qualification gaps
Status lifecycle GAP-003, weather expiry GAP-006 and async settings GAP-007 are resolved in source
with scoped tests and review. Configurable shortcut model/editor and shutdown quiescence regressions
pass. The Dock/Top Bar/search/settings/app/install delivery is implemented; full Release /WX and
scoped lifecycle/setup gates pass. Interactive capture refusals and remaining wider qualification
are recorded in TEST_STATUS; an alpha does not establish complete Version 1 acceptance.

Public-facing current limits are maintained in [KNOWN_LIMITATIONS](../../KNOWN_LIMITATIONS.md):
Windows 10/ARM64, real monitor/DPI/AppBar/Explorer/full-screen/application/audio/startup checks,
actual IME composition/screen-reader testing, live owner-signed updates, global performance budgets
and final owner visual direction. No Authenticode certificate is configured. None of these gaps
silently changes PROJECT_SPEC acceptance requirements.

## Tooling notes
- Graphify's C++ extractor yields no symbols for files built from macros (tests) and for some
  namespace-only headers; use `grep` for those.
- Current 2026-10-18 check: Graphify launcher resolves but exits 1 with
  `uv trampoline failed to canonicalize script path`; `uv tool list` reports no tools, and active
  Python has no Graphify package/module. No installation/repair was attempted. The old graph
  (2026-10-17 21:29:52 CEST) remains stale; source and Markdown are the working fallback.
- Measured 2026-10-17 (2,497-node graph): `graphify update .` 72 s, `export obsidian` 26 s
  (2,652 notes, 2 MB). A query for "backdrop blur" returned ~6.6 k characters (70 nodes, some noise);
  grep for the known identifier returned 23 exact hits in < 1 k characters. Use grep when the
  identifier is known; use Graphify for dependency/impact questions and unfamiliar areas.
- Obsidian CLI (`Obsidian.com`) only works while the Obsidian app runs and after it is enabled in
  Settings → General; plain file reads are the fallback.
- Obsidian 1.14.4 was independently rechecked running this session: vault-qualified read/search,
  HOME links and unresolved-link query succeeded for the repository's `docs/` vault. These Markdown
  edits update the same files; no exported graph or Obsidian configuration was modified.

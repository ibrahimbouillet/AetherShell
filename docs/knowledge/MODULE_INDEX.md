# Module index

Status records scoped implementation and executed evidence, not complete product acceptance.
Requirements: [PROJECT_SPEC](../../PROJECT_SPEC.md). Ordered work: [TASKS](../../TASKS.md).
Commands, counts, historical scope and pending gates: [TEST_STATUS](TEST_STATUS.md), especially
S-005–S-011. The complete Release /WX build and 426 unit cases (17 executables) pass. All 91 native
cases (14 executables) have passing scoped final-binary runs, but no uniformly green combined 31/31
run: aggregate GUI attempts encountered foreign overlap/full-screen or focus-dependent fixture
closure. Guards remain intact; isolated passes and failed attempts are retained in TEST_STATUS.
The alpha is installed; the official repository exists without a source push/CI receipt. Final
packaging/export privacy and approved public release evidence remain pending.
Module references in `docs/modules/` describe the actual APIs and limitations.

| Module | Responsibility | Source / reference | Tests | Current scope/evidence |
|---|---|---|---|---|
| core | Result, JSON, cancellation, workers, dispatch, signals, files, HTTP contract | `src/core` | `tests/unit/core` | Implemented; current Release unit dependency gate passes, including generic exception-log handling. |
| diagnostics | Rotating, sanitized logs | `src/diagnostics` | `tests/unit/diagnostics` | Implemented; historical scoped checks; no whole-product privacy certification. |
| platform | DPI, monitors, dispatcher, events, instance channel, paths, message loop | `src/platform/windows` | `tests/unit/platform` | Implemented; current Release unit gate passes, including dispatcher privacy follow-ups. Real desktop qualification remains. |
| graphics | Composition, glass, text, bitmaps, frames and animation | `src/graphics` | `tests/unit/graphics` | Implemented; current Release unit gate and Search GPU transition regression pass; complete visual/platform qualification remains. |
| ui | SurfaceWindow and tooltip | `src/ui` | `tests/integration/ui` | Hardened opaque-backdrop/guard-band probe passed 2 isolated native cases /WX; both captures inspected. Foreign overlap correctly refused before rerun. |
| net | HTTPS WinHTTP, bounded redirects/body size, cancellation | [reference](../modules/net.md), `src/platform/net` | `tests/{unit,integration,network}/net` | Current Release 24 unit + 28 read-only integration cases pass (52); three live-network cases excluded. Historical total 55 includes those network cases. |
| status | Clock, battery, keyboard, network | [reference](../modules/status.md), `src/status` | `tests/{unit,integration}/status` | S-005: fresh /WX 49 unit + 10 read-only integration + 1 isolated shutdown cases pass; GAP-003 resolved in scope. |
| status_audio | Core Audio volume/microphone | [reference](../modules/status_audio.md), `src/status/audio` | `tests/{unit,integration}/status_audio` | S-002: /WX 23 unit + 5 read-only integration cases pass; real controls/device failures unqualified. |
| weather | Optional Open-Meteo, cache, geocoding | [reference](../modules/weather.md), `src/weather` | `tests/unit/weather` | S-006: /WX 56 fake-based cases pass; GAP-006 resolved. No live forecast/geocoding test. |
| applications | Discovery, launcher, tracker, icons | [reference](../modules/applications.md), `src/applications` | `tests/{unit,integration}/applications` | Current Release 57 unit + 15 read-only integration cases pass (72). Real external launch/UWP-frame qualification remains pending. |
| search | Normalize, rank, highlight, immutable index, async service | [reference](../modules/search.md), `src/search` | `tests/unit/search` | S-007: current Release /WX 7 cases pass; p95 1.962 ms/5000 apps, 0.601 ms/2000 apps; cancellation/closed-dispatcher covered. Q-001 global budgets remain open. |
| settings | Versioned model, worker persistence, migration, consent-only startup | [reference](../modules/settings.md), `src/settings` | `tests/unit/settings` | S-007: current Release /WX 33 unit cases pass, including shutdown quiescence. GAP-007 resolved; GAP-008 model/editor implemented and scoped tests pass. |
| updates | SemVer, official Releases, signature/hash verification, coordinator | [reference](../modules/updates.md), `src/updates`, `scripts/release` | `tests/unit/updates`, Python release tests | Current Release 101 service unit cases and 32 Python release/package checks pass. Historical 117 is a prior recorded scope. Live signed official release unqualified. |
| accessibility | STA-owned UIA providers for custom surfaces | [reference](../modules/accessibility.md), `src/ui/accessibility` | `tests/{unit,integration}/accessibility` | S-007: /WX 3 unit + 3 own-window native cases pass; separate-MTA actions/focus/disconnect. Real screen-reader qualification pending. |
| branding | Original logo, raster/ICO/wordmarks and installer art | [reference](../BRANDING.md), `resources/branding` | Reproducible renderer checks | 12 raster sizes/8 ICO entries checked; contact sheet/banner inspected twice. Alpha proposal; Q-002 owner selection pending. |
| dock | Bottom application Dock, pinning, launch/input and UIA | [reference](../modules/dock.md), `src/dock` | `tests/{unit,integration}/dock` | Current Release /WX 14 unit + 2 native cases pass in scoped runs, including review fixes/UIA; captures inspected. Final fixture adds 128-pixel guard band and own-window z-order restoration; aggregate refusals retained. |
| topbar | Separate provider-driven Top Status Bar | [reference](../modules/topbar.md), `src/topbar` | `tests/{unit,integration}/topbar` | S-008: current Release /WX 7 unit + 2 integration cases pass, including update-badge identity/clearing/idle assertions; captures inspected. Real AppBar qualification pending. |
| search_ui | Transient native search/EDIT/results | [reference](../modules/search_ui.md), `src/search/ui` | `tests/integration/search_ui` | S-008: current Release /WX 5 cases pass, including finite GPU transitions; guarded captures inspected and native EDIT properties checked. Actual IME/screen-reader workflow unqualified. |
| settings_ui | Native settings, update controls, About | [reference](../modules/settings.md), `src/settings/ui` | `tests/integration/settings_ui` | Final /WX 5 native cases pass after consent/merge/viewport fixes; real startup/update/geocoding/screen-reader workflows unqualified. |
| install / updater / launcher / installer | Verified payloads, journal/rollback, stable launch and per-user setup | [install](../modules/install.md), [setup](../modules/installer.md); `src/install`, `launcher/`, `updater/`, `installer/` | `tests/{unit,integration}/install`, `tests/integration/installer` | S-009: Release /WX 10 unit + 7 install integration and 5 setup cases pass, including maintenance/viewport. Alpha installed separately; signed production recovery remains unqualified. |
| system_controls | Bounded real radio state and pure Windows routes | [reference](../modules/system_controls.md), `src/system_controls` | `tests/unit/system_controls` | S-026: Release /WX10 safe pure/admission cases pass; real controls unverified. |
| media_controls | SMTC sessions/playback and internal-display brightness | [reference](../modules/media_controls.md), `src/media_controls` | `tests/unit/media_controls` | S-026: Release /WX5 safe admission cases pass; no actual media/brightness discovery or mutation test. |
| controlcenter | Transient grouped media/system panel and accessible sliders | [reference](../modules/controlcenter.md), `src/controlcenter` | `tests/integration/controlcenter` | S-026 implementation in progress; native fake tests being compiled; prior UIA discovery gap unresolved. |
| app | Native lifecycle, service/window wiring and channel/health handling | [reference](../modules/app.md), `src/app` | `tests/unit/app`, `tests/integration/app` | Current Release app/CLI unit gate and 1 isolated lifecycle/health/channel/shutdown case pass with private options. Wider desktop/update handoff qualification remains. |

Navigation notes: [Graphics](modules/Graphics.md), [Dock](modules/Dock.md),
[Top Bar](modules/TopBar.md), [Search](modules/Search.md),
[System providers](modules/SystemProviders.md), [Settings](modules/Settings.md),
[Updates and installation](modules/Updater.md).

# Project context

Technical reference. **[V]** = verified by builds/tests or direct observation; **[A]** = assumption or
design intent not yet verified. Product requirements: [PROJECT_SPEC.md](PROJECT_SPEC.md).
Status details: `TASKS.md`; executed evidence: `docs/knowledge/TEST_STATUS.md`; latest session: `HANDOFF.md`.

## Objective and final outcome
A reliable, beautiful, modular native Windows application ("Aether Shell") with two desktop surfaces
(Dock, Top Status Bar), supporting search/settings/update UI, a per-user installer, signed GitHub
Releases updates with rollback, reproducible builds and honest documentation. Current product version
string: `0.2.0-alpha.2` (second public alpha; exact build, package and publication receipts are in
HANDOFF and `docs/knowledge/TEST_STATUS.md`). The prior `0.1.0-alpha.1` manual-install alpha remains
available in Releases.

## Architecture
```
app (lifecycle, wiring)                         src/app          [implemented] lifecycle smoke pending
 ├─ dock, topbar, search/ui, settings/ui        UI modules        [implemented] module gates below
 ├─ applications, search, status, status/audio,
 │  weather, settings, updates, install         services/providers
 ├─ ui → graphics → platform/windows → diagnostics → core   [V]
 └─ platform/net (WinHTTP for core/Http.h)      [V]
launcher/, updater/, installer/                 separate executables [implemented] module gates below
```
- **Threads [implemented, scoped tests]:** UI thread = COM STA, all windows, composition,
  Direct2D, WinEvent hooks, settings; `platform::MessageLoop` waits on messages + handles (no polling).
  Workers (`core/WorkerPool`) do network, discovery, icons (STA pool), downloads; results return via
  `IUiDispatcher`. Provider listeners never fire after `Stop()`.
- **Rendering [V]:** per window `WS_EX_NOREDIRECTIONBITMAP` popup → `DesktopWindowTarget` → backdrop
  sprite (HostBackdropBrush, Windows 11) + content sprite (`CompositionDrawingSurface`, Direct2D in DIPs).
  Frames only on change/animation (`graphics::FrameScheduler`, high-resolution waitable timer).
  Device loss → `GraphicsDevice::HandleDeviceLost()` → `DeviceReplaced`.
- **Windows integration [A, ADR 0003]:** top bar = top AppBar reserving its height (optional); Dock
  floating, topmost, no-activate, not an AppBar by default; both hide for full-screen apps; Explorer
  restart handled via `TaskbarCreated`; hotkeys Ctrl+Alt+Space (search), Ctrl+Alt+D (Dock focus).
- **V2 taskbar mode [implemented, D19/ADR 0009; live qualification pending]:** enabled by default
  unless the user saved an explicit opt-out. A launcher recovery process temporarily auto-hides/hides the
  native taskbar, watches the parent and restores state on exit; the parent observes helper failure.
  The Dock reserves its full animated footprint in this mode. Explorer and native Windows Settings
  remain separate system components; no Winlogon or administrator change occurs.
- **Install/update [A, ADR 0004/0005]:** per-user install root with launcher + `versions\<semver>`,
  journaled transaction (Prepared → … → Committed / RolledBack) with a health check.
- **Data locations [V]:** `%LOCALAPPDATA%\AetherShell\{config,logs,cache,updates}` (`platform/windows/AppPaths.h`).

## Technology stack
C++20 (MSVC, static CRT), CMake ≥ 3.25 + Ninja, Windows SDK 10.0.26100 (C++/WinRT), Win32,
Direct3D 11, Direct2D 1.1, DirectWrite, WIC, Windows.UI.Composition, WinHTTP, CNG (bcrypt), Core Audio,
WinRT (PowerManager, Networking.Connectivity), UI Automation. In-house test framework + CTest.
No third-party C++ dependencies. Python stdlib scripts for releases. GitHub Actions CI.

## Key locations
| Path | Content |
|---|---|
| `cmake/AetherModules.cmake` | module registry, directories, dependencies |
| `scripts/build.ps1` | build/test entry point |
| `src/core`, `src/diagnostics`, `src/platform/windows` | foundation |
| `src/graphics`, `src/ui` | composition graphics, glass, animation, `SurfaceWindow`, tooltip |
| `src/status/StatusTypes.h`, `src/applications/ApplicationTypes.h`, `src/settings/Settings.h` | shared contracts |
| `src/platform/net`, `src/updates`, `src/applications`, `src/status`, `src/weather` | services (see `TASKS.md` for status) |
| `docs/decisions/` | ADRs 0001–0009 |
| `docs/modules/` | per-module reference docs |
| `docs/knowledge/` | Obsidian vault notes: module index, test status, known issues, log (same files, no second copy) |
| `tests/{unit,integration,network}/<module>` | tests |
| `.github/workflows/ci.yml` | CI (Release, /WX, unit tests) |

## Implementation status (2026-10-18)
The following table preserves the `0.1.0-alpha.1` baseline. Current V2 evidence is in module references
and the latest TEST_STATUS. Alpha 2 publication is authorized and in progress; exact delivery
receipts belong in HANDOFF and TEST_STATUS. Full Version 1 platform/manual acceptance remains open.
An alpha is implemented and installed; full Version 1 platform/manual acceptance remains open.

| Area | Current executed scope |
|---|---|
| core, diagnostics, platform, net, graphics, accessibility | Release unit/read-only/native fixtures pass; see exact case table |
| updates | 101 unit cases pass; live official signed-release cycle remains unqualified |
| applications | 57 unit + 15 read-only/fake desktop cases pass; real external launch/UWP attribution remains unqualified |
| status / audio / weather | 60 / 28 / 56 scoped cases pass; no real audio mutation or live forecast request |
| settings / search | 33 / 7 unit cases pass; persistence admission freezes before app shutdown flush |
| Dock / Top Bar / Search / Settings UI | implemented; guarded own-window tests and original assets, with per-run results in TEST_STATUS |
| install / launcher / updater / setup | implemented; 17 install-library + 5 native setup cases pass |
| app | native Release executable and isolated full lifecycle/health/channel/shutdown case pass |
| CI/public delivery | clean history pushed; Windows Release CI green; manual alpha published; exact receipts in HANDOFF / TEST_STATUS S-012 |

## Current local development state (2026-10-22)
D27/D28 source work adds animated Dock reordering and attention cues, proportional status glyphs,
low-battery coloring, and consent-gated Windows location for automatic weather. S-027 passed a full
Release `/W4 /WX` build, all 21 unit CTest executables, and six focused own-window integration
executables (Dock, Top Bar, settings, application discovery, app attention and Control Center).
Earlier Search microbenchmark runs were intermittent; the final complete unit run passed. These
automated results do not replace owner review of real Windows location permission/forecast, shell
attention, reduced motion, physical DPI or Explorer/taskbar recovery. See S-027 in
`docs/knowledge/TEST_STATUS.md` and the current `HANDOFF.md`. The worktree-built Release candidate is
currently visible in a separate local preview profile with weather disabled; the installed Alpha 1
was not overwritten. D29 authorizes the second-release publication. See the latest handoff for the
exact published commit and remaining owner/device qualification; automated gates do not imply full
Version 1 acceptance.

## Known issues, limitations, risks (summary — details in `docs/knowledge/KNOWN_ISSUES.md`)
- Blur only on Windows 11; Windows 10 untested. Multi-monitor, DPI change and Explorer restart untested.
- No code-signing certificate: Authenticode cannot be enforced; SmartScreen will warn.
- WinHTTP synchronous cancellation keeps a documented theoretical handle-reuse window.
- Earlier usage limits left uncommitted module work; preserve it. Status concurrent lifecycle and weather expiry findings are resolved by scoped regressions;
  remaining product/desktop qualification is in TASKS / module docs.
- "Microphone in use" unsupported; Wi-Fi SSID intentionally not shown.
- V2 taskbar mode is a compatibility feature, not official native-taskbar or Windows Settings
  registration. Helper/app double failure, Windows changes, and actual Explorer recovery still need
  qualification. Windows pin import reads available shortcuts only and may miss pins/order.
- Graphify launcher currently fails; the stale graph is not current evidence. Obsidian CLI works
  while its app runs; root docs remain outside the `docs/` vault.

## Environment and configuration (no secrets here)
- Build machine: Windows 11 x64, VS Build Tools with C++ workload, Windows SDK 10.0.26100.
- Update source is compiled in: `AETHER_UPDATE_REPO_OWNER=ibrahimbouillet`, `AETHER_UPDATE_REPO_NAME=AetherShell`.
- Release signing: ECDSA P-256 public key compiled into `src/updates/TrustedKeys.h`; the private key is
  owner-controlled. Packaging uses a privately configured signing environment when available;
  unsigned manifests stay local. No signing secret is included in the public CI workflow.

## Constraints that must not be broken
See `AGENTS.md` "Non-negotiable constraints". In addition: keep module boundaries (UI depends on
interfaces; updates never depend on UI rendering; weather never depends on search; settings hold data,
not logic), and keep generated/local tool output out of Git.

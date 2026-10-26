# Aether Shell — project specification

This is the authoritative reference for **what the product must achieve**. The repository and
executed checks establish **what it currently does**; implementation does not redefine requirements.
Owner/author: Ibrahim Bouillet. License: MIT. Baseline: 2026-10-18, existing project decisions and
module work orders, plus the owner's request for one traceable specification. The first
`0.1.0-alpha.1` manual-install alpha is public. The owner-approved V2 prerelease is
`0.2.0-alpha.2`; its exact build/delivery evidence is recorded separately when executed.

**Sources of requirements, in order of authority.** (1) Explicit owner instructions recorded as
owner-approved decisions in [DECISIONS.md](DECISIONS.md) (including D15, D18, D19, D21 and D23). (2) The owner's original
master brief and its additional mandatory instructions (2026-10-17). The brief itself is not stored in
the repository; its binding content is captured in this file and cited as "brief §n". (3) Engineering
decisions taken by assistants under the authority the brief delegates ("choose … and document the
decision"); they may be revised with evidence, never silently. (4) Module work orders (`docs/specs/`)
and module references (`docs/modules/`), which add detail but no authority. Terminology: **Version 1**
is the product scope and definition of done of brief §17 (section G). `0.1.0-alpha.1` and
`0.2.0-alpha.2` (the latter called "V2" in D19/ADR 0009) are prereleases of that work, not
product versions 1 and 2.

## A. Identity and mission

Aether Shell is a modular native Windows desktop interface for people who want a floating
application Dock and a separate Top Status Bar while continuing to use Windows Explorer. The
native taskbar is visually replaced during V2 use by default, with a persistent opt-out and recovery
safeguards (D19/D21/D23). It makes applications and real system information accessible through a coherent,
lightweight glass interface. The intended audience is desktop users; no narrower user research or
enterprise deployment commitment is established.

**Problem addressed (brief §1, §22).** Windows users who want a premium, coherent and customizable
desktop (a fluid application Dock and a clean status bar with an original glass look) otherwise have
to choose between the stock taskbar and heavyweight or risky shell replacements. Aether Shell provides
that experience natively and lightly, beside Explorer, with real system data, safe updates and no
administrator rights. Use cases: launching and switching applications from the Dock, finding
applications by search, reading time, battery, language, network, audio and weather at a glance,
customizing both surfaces, and installing, updating, repairing or removing the product safely.

Success means a real native application that starts both bars, launches or activates applications,
reports real status, supports keyboard and assistive technology, shuts down cleanly, and can be
installed, repaired, updated and rolled back without administrator rights or loss of a working
installation. A rendering probe, service library or passing unit suite alone is not that outcome.
Version 1 is complete only when every item of the owner's Version 1 definition of done (brief §17,
mapped in section G) is verified; until then builds carry prerelease identifiers (brief §16).

## B. Vision and scope

The confirmed Version 1 direction includes application discovery, search, persistent customization,
optional weather, accessible supporting windows, original branding, per-user installation and
authenticated GitHub Releases updates with transactional recovery. Milestone 1 is the first runnable
Dock + Top Status Bar application; it is an intermediate milestone, not a reduction of that direction.

Version 1 has exactly **two persistent desktop surfaces**, the bottom Dock and Top Status Bar.
Transient search, settings, update dialogs, menus and tooltips are supporting UI permitted by ADR 0001
and the module work orders. They do not authorize a third persistent desktop surface. The Dock holds
applications and the search entry; system status belongs to the Top Status Bar.

Version 1 targets the primary monitor, Windows 10 2004+ and Windows 11 x64. Qualification on Windows 10,
monitor/DPI changes and Explorer restart remains required. Per-monitor expansion and ARM64 are future
possibilities, not committed Version 1 deliverables. Enterprise MSI deployment is outside Version 1
(ADR 0005). Exact branding palette and release performance budgets remain unresolved. There is no
approved Explorer-replacement, extra-panel or cross-platform roadmap.

### Approved V2 revision — 2026-10-18

The owner explicitly requested removal of the duplicate native taskbar during Aether Shell use,
proper bottom placement/reservation, readable semi-transparent glass, a Wi-Fi wave glyph and
available Windows pinned applications. D19 / [ADR 0009](docs/decisions/0009-reversible-taskbar-replacement.md)
approves reversible visual replacement and qualifies the earlier FR-001/NFR-003 no-hide
rule. D23 records the owner's requested launch behavior: replacement defaults on unless an explicit
opt-out was saved; later Desktop changes require Apply, and ResetToDefaults preserves this choice.
Setup discloses this behavior. At D23, start at sign-in remained a separate opt-in. D34 supersedes
that installer policy: fresh interactive setup visibly selects sign-in startup with an opt-out;
existing saved choices and Task Manager disables are preserved. Settings and portable profiles
still default to `startWithWindows=false`; an accepted setup choice or Settings Apply enables it.
Native Windows Settings taskbar takeover is
unsupported: that page remains Microsoft's interface. This preserves requirement IDs and records
the owner's scope change without converting implementation claims into acceptance evidence.

### Long-term direction (brief §18): exploratory, not approved for implementation
The architecture must leave room for separately scoped future modules: additional Dock positioning
modes, advanced multi-monitor behaviour, notification integration, a theme marketplace, additional
status providers, window-management tools, virtual-desktop integration, keyboard-shortcut management,
a plugin SDK, more advanced application launchers and an optional Windows Shell replacement. None of
these is a requirement until the owner approves it and it receives FR IDs. Any shell-replacement
feature must be separately designed, explicitly opt-in, compatibility-checked, recoverable without
Aether Shell running, tested across supported Windows versions and never active by default. That
future Explorer-replacement rule does not change D23's default for the current reversible visual
taskbar mode, which keeps Explorer running. D19/D21 recovery conditions are tracked as GAP-011.

### Development milestones (brief §16) and the current state
| Milestone | Content | Tasks | State on 2026-10-18 (details: section G, TASKS) |
|---|---|---|---|
| M0 Foundation | toolchain, module structure, CMake, lifecycle, diagnostics, first native window | T0–T2 | implemented and tested |
| M1 First visible application | real Dock and Top Bar, clock, real icons, launching, safe start/stop | T3, T6, T7, T10 | delivered as `0.1.0-alpha.1`; desktop qualification partial |
| M2 Core functionality | running apps, pinning, search, hover animation, settings, real providers, weather failures | T5, T6–T8, T16, T17 | implemented; module tests pass; owner-reported defects open (T17) |
| M3 Update foundation | SemVer, GitHub Releases, update UI, verified downloader, manifest validation | T4, T5a, T8 | implemented; no live signed update yet |
| M4 Safe installation | updater, staging, transaction, health check, rollback, installer, uninstall, repair | T9, T13 | implemented; real signed update and rollback unqualified |
| M5 Release qualification | Windows integration, accessibility, DPI, monitors, performance, offline, recovery, docs audit, release candidate | T11, T14, T15 | open |

## C. Functional requirements

IDs are permanent; retire an ID explicitly rather than reusing it. P0 = integrity/release gate;
P1 = required Version 1 capability; P2 = optional user-enabled capability. These priorities organize
the existing scope; the next implementation order remains in [TASKS.md](TASKS.md).
Module work orders supply detailed algorithms, bounds and fixtures beneath these requirements.
Dependencies describe functional integration, not a strict build-order graph; use TASKS and the
CMake module registry for implementation/build ordering.

| ID | Description and rationale | Priority | Acceptance criteria | Verification method | Dependencies |
|---|---|---|---|---|---|
| FR-001 | Two separate desktop bars alongside Explorer, keeping applications usable. | P0 | Dock at bottom and Top Status Bar at top of primary work area; D19/D23 reversible visual replacement defaults on with mandatory Dock reservation, preserving a saved opt-out; coexistence is available when disabled; top reservation defaults on, ordinary Dock reservation off; both hide for foreground full-screen apps (D27 permits a bounded6-second Dock-only genuine-attention peek without focus theft) and recover after Explorer/display/DPI changes. | Own-window model/capture tests; separately authorized desktop checks of reservation, full-screen, recovery and restart, per ADRs 0003/0009. | FR-017, FR-024; NFR-003, NFR-006 |
| FR-002 | Dock item model and persistent pinning so users control application access. | P1 | Search entry, ordered pins, then optional unpinned running apps; identities deduplicated; running indicators; pin/unpin/reorder persist; narrow width uses documented shrink/overflow behavior; read-only best-effort taskbar shortcut import preserves existing Aether pins and launch arguments, discloses incomplete Windows pin/order coverage. | Model tests including duplicates, ordering and overflow; temporary-settings and synthetic-shortcut integration. | FR-003, FR-014, FR-024 |
| FR-003 | Discover applications, obtain real icons, launch and activate existing windows. | P1 | Catalog covers supported desktop/packaged apps; launch uses validated application identities; activation/cycling tracks running windows; errors return useful failures; icon fallback is honest. | Catalog/icon/window integration, launch/activation fakes, later explicit manual launch and UWP-frame qualification. | NFR-001, NFR-002 |
| FR-004 | Usable Dock input and animation with accurate hit targets. | P1 | Click/keyboard/context menu and drag reorder follow dock work order; magnification never overlaps items; a captured icon follows horizontal pointer motion, stable slot crossing moves neighbors with interruptible springs and release settles continuously; no hover background tiles or pinned/running separator; hit-testing follows drawn geometry; tooltips/focus indicators work; reduced motion removes bounce/magnification animation; keyboard focus is not taken unnecessarily and Windows foreground-activation limits are respected and reported, never forced (brief §3.6). | Layout/model tests; fake-launcher window captures at rest/hover; keyboard/UIA checks. | FR-002; NFR-007, NFR-011 |
| FR-005 | Real localized clock/date, including time and zone changes. | P1 | Default minute ticks and optional seconds align to boundaries; immediate refresh on time/locale/resume notifications; system/12/24-hour and date formats retain locale conventions; unavailable time is never fabricated. | Fixed en-US/fr-FR/de-DE format tests; real-clock boundary, refresh and stop integration. | NFR-002, NFR-005 |
| FR-006 | Honest battery/power information across available hardware. | P1 | Real presence/percent/source/charge/saver/time-left/Windows low-critical flag when known; round proportional battery glyph has green fill at100% and red at<=5% or OS low-critical (high contrast uses system colors); absent is unavailable, unknown percent/time stays absent; PowerManager plus Win32 fallback; repeated identical readings suppressed. | Pure raw-reading mapping and real read-only/fallback integration; absent/unknown fixtures. | NFR-005 |
| FR-007 | Foreground application's input language with explicit limitations. | P1 | Foreground thread HKL supplies localized label/layout; foreground hook plus default 1 s comparison; fallback marked when no foreground window; console/elevation/IME limitations documented. | Injected HKL changes/refresh/restart tests and read-only installed-layout integration. | NFR-002, NFR-005 |
| FR-008 | Network status without exposing location-derived identifiers. | P1 | Real connectivity/kind and reported wireless strength; Wi-Fi uses waves, cellular bars and Ethernet a distinct glyph; unknown strength stays explicit; no profile name/SSID read or shown; event bursts debounced on background thread; no network polling. | Pure strength/glyph mapping; real read-only subscription and stop integration; source inspection for identifier access and own-window glyph review. | NFR-004, NFR-005 |
| FR-009 | Default audio/microphone status and explicit user controls. | P1 | Real endpoint availability/volume/mute; controls report failures; endpoint changes handled; microphone-in-use reported unsupported; no state changes without user input. D24 adds a microphone-menu action requesting Windows voice typing (Win+H) after restoring the previous application focus; mute/privacy actions remain separate and Aether captures/stores no speech. | Mapping, publisher and worker tests; read-only endpoint/stop integration; fake menu/focus admission tests without real dictation; explicit later manual controls/focus check. | NFR-011; NFR-003, NFR-005 |
| FR-010 | Optional weather for a location explicitly chosen by the user. | P2 | Disabled until first-use weather/location consent; D28 offers automatic Windows location with the OS permission prompt, bounded coarse position reads and efficient refresh on new installations; existing opt-outs/manual locations are preserved; no forecast while location is denied/unavailable; no automatic forecast requests while disabled/unconfigured (explicit location-search geocoding is separate); configurable location and temperature unit; automatic location follows Windows/user consent and never scrapes raw Wi-Fi/IP or Windows weather UI; HTTPS with timeout, cancellation and bounded retries; no embedded credentials; Open-Meteo attribution shown; the brief §4.8 states Loading, Available, Cached, Offline, Location not configured, Provider unavailable, Request failed and Unsupported are distinguishable; cached data carries its timestamp and is never displayed after expiry (engineering default 6 h, set in the work order, not by the owner); cancellation/config changes cannot publish stale-location results; a weather failure never prevents either bar from starting. | Fake HTTP/clock/cache/geocoding tests; later consent-based end-to-end UI/network check. | FR-014, FR-015; NFR-004 |
| FR-011 | Top Status Bar renders real providers clearly and remains usable on narrow screens. | P1 | Clock/date, battery, language, network, volume, microphone and optional weather reflect snapshots/visibility settings; no invented zeroes; logo menu and neutral tooltips, no hover light tiles or trailing native-taskbar arrow (D27); recovery remains in Settings/menu/CLI; absent battery handled; low-priority items collapse predictably. | Model/layout fixtures, dark/light own-window captures, read-only real-provider integration. | FR-005–FR-010, FR-014, FR-022; NFR-007 |
| FR-012 | Deterministic application-only search service. | P1 | Case/accent normalization; exact > name prefix > word prefix > acronym > substring > fuzzy; stable ties/original-coordinate highlights; empty query gives no results; background cancellable rebuild; typed text is never executed. | Ranking/Unicode/highlight/concurrent-rebuild tests and measured synthetic-index benchmark. Release latency budget unresolved (Q-001). | FR-003; NFR-002 |
| FR-013 | Keyboard-accessible search window from Dock or hotkey. | P1 | Real EDIT/IME input, current-index results on edit, arrows/Enter/Escape and empty/error states; launch only selected results; Escape clears then dismisses; deactivation closes. | Fake catalog/launcher integration, keyboard/IME/UIA checks and own-window capture. | FR-012, FR-003; NFR-007 |
| FR-014 | Durable versioned settings that survive invalid files and binary rollback. | P1 | Schema-v1 JSON validates documented model ranges; defaults on missing file without writing; atomic saves, corruption recovery and bounded backups; newer schemas protected before overwrite; write failures reported and pending state retried. | Temporary-file round trips, wrong types/ranges, migration/newer-schema/corruption/failed-write tests. | NFR-005 |
| FR-015 | Settings UI applies customization through the settings service. | P1 | Dock, Top Bar/weather, updates, general, Desktop and about controls work, covering at least the brief §8 list (Dock: size, bottom margin, icon spacing, opacity, animation intensity, pinned applications and order; Top Bar: top margin, opacity, date/time format, visible indicators, weather on/off and location/provider; Updates: automatic checks on/off, check interval, notifications on/off, manual check, current and available version; General: start with Windows after explicit consent, reduced motion or system inheritance, diagnostic log access, configuration reset); Desktop changes to the replacement choice require explicit Apply and show recovery/status limitations; reset preserves the replacement choice (D23); standard Win32 controls; changes persist; errors visible; about shows version, author and MIT; diagnostics export follows privacy rules. | Temporary-service control-message integration, keyboard/DPI/high-contrast checks and own-window captures. | FR-014, FR-018, FR-022, FR-024; NFR-004, NFR-007 |
| FR-016 | Disclosed per-user startup and safe global keyboard entry points (D34). | P0 | Fresh interactive setup visibly selects start at sign-in with an opt-out; accepting setup records that choice. Preserve existing saved choices and Task Manager disable; silent setup requires an explicit startup flag. Portable/settings defaults remain off until consent. Stable launcher uses bounded backoff for abnormal Windows-exception crashes only, revalidates the active installation and stops for intentional Quit/update/uninstall or ordinary errors. No elevation, service, hidden autorun, OS-critical/realtime priority or guaranteed Windows launch order/delay. Hotkeys configurable/disable-able, default Ctrl+Alt+Space and Ctrl+Alt+D; conflicts visible/nonfatal. | Injected registry, installer-choice and launcher exception/budget/active-target fixtures; separate owner-authorized installed startup/recovery verification. Reboot timing remains unverified unless actually measured. | FR-014, FR-017, FR-020, FR-024; NFR-003; D34 |
| FR-017 | Native application lifecycle integrates the modules reliably. | P1 | One instance, second-launch forwarding, both bars wired, provider results marshalled to UI, ordered shutdown/unregistration and update health signal; no dangling callbacks or hidden autostart. | Full app start/stop/second-instance integration; shutdown/failure fixtures and desktop observation. | FR-002, FR-004, FR-011, FR-013–FR-016, FR-019, FR-021 |
| FR-018 | Updates authenticated against the official release source. | P0 | Only configured official GitHub Releases; newer compatible SemVer/channel; signed manifest verified before fields are trusted; download size/SHA-256 checked; redirects bounded; never execute unverified content; no token needed; startup, periodic (configurable interval, bounded retries) and manual checks; offline, HTTP and rate-limit responses reported without disturbing the Dock; non-sensitive release metadata cached; assets validated for release/asset identity, naming, architecture, minimum Windows build and package type; placeholder metadata rejected. | Malformed/replay/channel/signature/hash/size/redirect/cancellation fixtures; later live official-release qualification (ADR 0004). | NFR-003, NFR-004 |
| FR-019 | User-facing update flow with transparent errors and safe release notes. | P1 | Check, download/install, progress, postpone/skip/disable notifications; verification state visible; plain-text sanitized notes only; allowed release-details links; installation disabled while busy; the dialog shows installed and available version, channel, publication date, download size when known, verification status and compatibility; postpone, check later, disable notifications and view release details are available; no update is ever forced (brief §9.4). | Coordinator fakes and temporary UI integration; malicious-note/link fixtures. | FR-018, FR-021, FR-014 |
| FR-020 | Per-user native install, stable launcher, repair and uninstall. | P1 | Complete versioned payload verified on extraction; native wizard and silent mode; no elevation; Start Menu/uninstall registration; repair preserves settings; uninstall keeps user data unless removal requested; errors/exit codes documented; setup pages cover welcome, description, location when configurable, explanation of system changes, progress, cancellation, errors, success with an optional launch, repair and uninstall, all keyboard-accessible; uninstall stops the app, removes files, startup entry and shortcuts, keeps or removes settings by explicit option and leaves the shell working, tested after a fresh install and after an upgrade (brief §10). | Temp install/data roots, fake/injected registry/process operations; install/repair/uninstall and payload-containment tests. | FR-014, FR-016, FR-018, FR-022; NFR-003 |
| FR-021 | Transactional update and recovery retain a working version. | P0 | Updater independently re-verifies package; journal follows ADR 0005; stage before switch; health check decides commit/rollback; interruption recoverable at each state; active/previous versions never removed prematurely; binary rollback preserves newer user data. | Injected failures at every state, temporary-root recovery/rollback, health success/timeout, malicious payload/path fixtures. | FR-018; shared install library of FR-020 |
| FR-022 | Original coherent identity and native visual assets. | P1 | Recognizable original mark at small sizes, consistent icon/installer art and light/dark variants; reproducible one-source asset generation; no copied third-party artwork or redistributed font binaries (installed system-font rasterization/references permitted); visual inspection recorded; the brief's variants exist (primary full-colour, monochrome, light, dark, transparent, vector source, ICO, installer and README art) and the same identity is used in the Dock icon, About, Settings, installer, update UI, README and release assets. | Asset inventory, dimensions/ICO sizes/regeneration, small-size light/dark visual review. Palette/concept selection remains Q-002. | NFR-009, NFR-011 |
| FR-023 | Reproducible public delivery with honest end-user documentation. | P1 | Official public repository and green Windows CI; qualified native setup/manifest/checksums; approved release publication; README/screenshots and install/update/rollback/limitations docs describe tested behavior; the brief §15.1 document set exists (README, ARCHITECTURE, BUILDING, INSTALLATION, USER_GUIDE, SECURITY, CONTRIBUTING, CHANGELOG, RELEASE_PROCESS, UPDATE_AND_ROLLBACK, TESTING, KNOWN_LIMITATIONS; see GAP-009); each release carries SemVer, changelog reference, notes, per-architecture packages with accurate sizes, integrity metadata, signing metadata when available, build configuration, test results, upgrade/rollback instructions and known limitations; stable publication is a separate gated step and CI never marks a build stable when required tests failed or did not run (brief §15). | Local build/package/signature checks, actual CI/release receipts, privacy/license and documentation review. | FR-017–FR-022; NFR-004, NFR-009 |
| FR-024 | Reversible visual native-taskbar replacement, approved as V2 by the owner. | P1 | Default true with a persistent explicit opt-out (D23); Desktop Apply changes either direction; reset preserves this choice; setup discloses replacement and optional recovery shortcut; worker-owned recovery helper restores on disable/parent death and parent fallback observes helper death; Dock reserves its animated footprint; `--quit` forwards to an existing instance and is a no-op without one; `--restore-taskbar` forwards to a running instance and, without one, restores the native taskbar itself from a baseline persisted before any hiding, as does setup repair (D21), and persists the choice off without rewriting a newer schema; no V2 alpha is published before the recovery path is verified; native Settings takeover unsupported; compatibility/double-failure limits explicit. | Fake backend/process-state tests, Apply/opt-out fixtures and source review; separately authorized real desktop failure/restart/full-screen qualification, never native mutations in automated tests. | D19/D21/D23/ADR 0009; FR-001, FR-014–FR-017; NFR-003, NFR-008 |
| FR-025 | Centralized logging and diagnostics, so that failures can be investigated without exposing private data (brief §12). | P1 | Severity levels; startup, module-initialization, Windows API, rendering, provider, application-launch, update, installer and rollback outcomes are logged; no credentials, tokens or unnecessary personal data; bounded retention (rotation) and bounded crash dumps; no per-frame logging; Settings offers log-folder access and a diagnostic export that states what it contains. | Logger filtering/sanitization/rotation tests; review of log call sites; inspection of an exported bundle for private data; manual Settings check. | FR-015; NFR-004, NFR-005 |
| FR-026 | Grouped quick controls (D26, owner approved). | P1 | One Wi-Fi/audio/battery button opens a transient accessible media/system panel. Real media session selection and supported playback, radio/volume/brightness controls; real signal/level/readback; animated brightness sun obeys reduced motion. Unsupported desktop toggles use clearly labelled Windows routes. Includes Wi-Fi, Bluetooth, airplane mode, accessibility, energy saver, captions, night light, hotspot, nearby sharing, Cast/Project, battery and all Settings. Unknown stays unknown; no discovery or blocking work on the UI thread. | Pure mapping/provider failure tests; own-window fake UI/RangeValue/keyboard tests; separate manual real-control checks, never real device mutation in automated tests. | FR-007–FR-010, FR-015; NFR-002–NFR-008 |
| FR-027 | Symmetric maximized-window side margins (D26, owner approved). | P1 | Maximized application work area aligns with the visible Top Bar left/right inset at the monitor DPI. Manually placed windows are never automatically moved/resized. Reversible infrastructure reservations only; respect other AppBars and restore on disable/quit/Explorer restart. Native Windows Settings takeover remains unsupported. | Pure geometry and isolated fake lifecycle tests; separate bounded desktop check with own maximized test window and restoration measurements. | FR-001, FR-024; NFR-003, NFR-006 |
| FR-028 | Real application attention without focus theft (D27). | P1 | Supported Windows shell flash requests mark eligible background applications, reset on foreground/removal, propagate through pinned/running grouping. Finite soft red cue and bounded Dock-only fullscreen peek; no invented alert from a new/title-changed window, no forced app activation/fullscreen interruption, no endless frames. Shell-hook compatibility limits explicit. | Pure policy/model tests and own-window/fake native lifecycle/animation checks; no owner-app manipulation. | FR-001–FR-004; NFR-002, NFR-005, NFR-007 |
| FR-029 | Native cinematic showcase capture and export (D30–D33). | P1 | A separate user-started tool captures the real scenic Aether desktop, inset Top Bar, centered Dock and genuine pinned logos without unrelated windows or promotional overlays. D33 requires this exact time-based sequence: two seconds of empty desktop with the real pointer visible; curved left detour/approach; proportional zoom and traversal of the entire genuine Dock using actual visible control bounds; hover the real Code pin for one second; click once; wait asynchronously for enabled onscreen loaded-workbench markers, not a first blank window; show the app for three seconds total after readiness, including camera reveal; return the pointer to its neutral start during reveal; request that scenario window close; require window absence plus two consecutive captured body/backdrop samples matching the original clean scene, then capture three visually empty seconds before stopping, excluding DWM's closing fade. Only afterward restore old apps, flush buffered footage and render. Current authored approach/zoom/travel durations are 1.1/1.4/2.4 seconds; the 1.6-second reveal leaves 1.4 stable seconds within the three-second ready interval. The After Effects-associated quality target means purposeful holds, continuous trajectories and readable easing, not an Adobe dependency. Default quintic easing settles position/velocity/acceleration; custom cubic overrides require zero-velocity endpoints and verified joins, and must not silently change when recorded/reopened. Render from elapsed time at subframe timestamps; blur derives from genuine captured temporal samples and preserves legibility. Target measured 60-FPS MP4 on capable hardware, distinguishing capture sampling, actual display updates and output frame rate. Preserve genuine pixels and bitmap/vector distinctions. Produce a high-quality MP4 and optimized looping GIF from the same source. Configure native-source, 1920×1080 and 3840×2160 delivery; 7680×4320 remains aspirational with sufficient source detail and measured performance. Upscaling raster input restores no detail. Outputs stay local, preserve existing files and remain private until every-frame review. Owner acceptance is 100/100 across the complete exact final export: all 60 adapted checks and ten blocking gates pass with evidence. No averaging across shots; changes invalidate affected QC. | Deterministic camera/join and fake ready/close/event tests; synthetic archive/buffer/compositor and own-window encode/decode tests; native pixel orientation/channel checks; complete normal-speed playback with audio; frame-level motion/privacy review; genuine acquisition/present/output cadence; separate authorized owner/device capture. | FR-001–FR-003, FR-022; NFR-001, NFR-004, NFR-005, NFR-009, NFR-011, NFR-012; D33 |

| FR-030 | Reusable versioned product presentation capture (D35). | P1 | Extend the separate user-started capture tool with bounded reusable desktop, grouped quick-controls, settings and volume demonstration scenarios; preserve real pixels, pointer and current pins, not mockups. Export genuine full-desktop PNG and MP4; allow safe repeatable selection of scenario/page/action while keeping D33 cinematic film available. Only explicitly authorized device interactions may run, restore observed volume on ordinary exit and disclose failures. Close/attempt to relaunch user apps through NFR-012, including browsers; do not touch shell/security/controller processes. Reject private app/media content and review every still/frame before public use. Keep raw metadata local and approved gallery/lineage by release, linked from README, for Alpha 3 comparisons. No unverified motion score. | Fake scenario/action/rollback tests, synthetic PNG/export tests, separate real capture, full-resolution stills and all-frame video privacy review with exact hashes/build identity. | FR-026, FR-029; NFR-004, NFR-008, NFR-012; D35 |

### Approved V3 functional extension — D38, 2026-10-23

The owner accepted Alpha 2 and authorized this design/interaction development track. V3 is not
finished by this first batch, and its release number/publication is not authorized yet. Detailed
work order: [V3 design and interaction](docs/specs/v3-design-and-interaction.md). Desired external-
window behavior remains subject to documented Windows capabilities and explicit qualification.

| ID | Requirement | Priority | Acceptance | Verification | Dependencies |
| --- | --- | --- | --- | --- | --- |
| FR-031 | Compact Dock and exact interactive footprint. | P0 | Modestly shorter default (owner estimate 4–5 screen pixels), same visible bottom gap, saved custom sizes preserved. Dock/Top Bar capture no input in empty outer margins; current animated icons remain interactive. Decorative shadows/entrance headroom never create a cross-process input wall. | Native-region and separate own-process routing regression; DPI/hover/drag/reduced-motion checks; physical visual acceptance. | FR-004; NFR-006–NFR-008, NFR-011 |
| FR-032 | Symmetric edge margins and minimal work-area exclusion. | P1 | Align side/top/bottom outer gaps with Top Bar/Dock insets; manually placed windows may use surrounding free space without automatic relocation. Maximize-only policy remains. Resolve rectangular AppBar constraints separately from hit regions; preserve content controls and reversible recovery. | Pure geometry/DPI tests, own maximized/manual windows and scoped native work-area restoration check. | FR-001, FR-027, FR-031; NFR-003, NFR-006 |
| FR-033 | Fluid running-dot/active-pill feedback. | P1 | Running background dot grows to the same width as the active pill while its icon/preview is hovered; actual foreground retains its active pill. Owner clarification 2026-10-25: calmer, perceptibly slower expansion/contraction and simultaneous old/new marker retargeting on observed activation. Launch/activation/layout/leave preserve continuous position/width without inventing running/foreground state or forcing a fully expanded marker to restart. Closed pins have no fake running indicator. | Deterministic interrupted motion/state tests, fake application windows and real normal-speed owner visual acceptance. | FR-002, FR-004; NFR-007, NFR-011 |
| FR-034 | Genuine transient window previews and supported actions. | P1 | Deliberate hover over a running app yields a rounded neutral-glass real-window miniature instead of text only, preserving aspect ratio with keyboard/UIA access. Owner refinement 2026-10-25: remove excessive card gutters/unused thumbnail space without shrinking the actual miniature; retain a readable header and usable X. Pointer travel into the preview/X retains it and hover emphasis; leaving both dismisses it. X requests normal closure of the clearly selected window only, preserving save/refusal dialogs. Show supported play/pause only for a confidently attributed real media session. Handle multiple/unknown/protected windows, dismissal and transient privacy honestly. | Fake window/session identity tests and own-window thumbnail/action/cleanup tests; separate owner acceptance. | FR-003, FR-015, FR-026; NFR-004, NFR-007, NFR-008 |
| FR-035 | Readable rounded system icons and truthful actionable controls. | P1 | Rounded proportional Wi-Fi/airplane/icon family; bright known-active and gray inactive waves, explicit unknown, current Windows-derived strength without fixed adjustment. Every tile performs its named supported action or labels its Windows route. D44 selects a complete Aether-owned Quick Settings panel faithful to the private reference, with polished glyphs, real interactions, neutral light/dark glass and below-Top-Bar anchoring; native Windows opening may remain an explicit alternative, with no embedding claim. | Signal mapping/freshness and fake control-route/error tests; reference icon/layout comparison, owned-window motion/UIA/anchor checks and explicit capability parity; separate OS/hidden-taskbar interaction gate, no real device mutation in tests. | FR-007–FR-011, FR-026; NFR-002, NFR-006–NFR-008, NFR-011; D44 |
| FR-036 | Icon-anchored window motion and coherent rounded corners. | P1 | Desired reveal/activation from clicked icon and minimize-return transition coordinated with marker growth; interruption/late launches and reduced motion remain truthful. Coherent rounded Aether windows; investigate external/maximized corners and motion through documented APIs, preserve original attributes and explicitly record unsupported states. Owner clarification 2026-10-25: excluded corner pixels must reveal the actual underlying scene completely, without opaque dark rectangles or a painted wallpaper approximation; region membership alone does not establish this. Universal external-window control is not presumed feasible. | Native own-window prototype, deterministic geometry/motion and cleanup tests, composed corner pixels over a changing private backdrop, compatibility matrix and owner visual acceptance. | FR-003, FR-015, FR-027, FR-033; NFR-003, NFR-006–NFR-008, NFR-011 |
| FR-037 | One coherent visual language across Aether-owned interfaces. | P1 | Owner request 2026-10-25, D41: carry the existing neutral translucent fine-grain glass, restrained light rim, refined rounded glyphs, readable typography and deliberate interruptible motion through the logo menu, every Settings page/control/dropdown, update/status/error/confirmation flows and other owned surfaces. The full installer/upgrade/repair/uninstall interface is explicitly mandatory, not a branding-only pass; preserve the safe deployment engine and clean-machine operation. Eliminate the stock white/grey form appearance without losing native input, accessibility, consent or persistence semantics. Inventory remaining surfaces and precise native exceptions; Windows-owned UI stays native. Preserve neutral lighting, exact hit regions and recent corner/shadow fixes. | Tracked surface/state inventory; focused functional/UIA/keyboard checks; changing private-backdrop pixel renders in dark/light and accessibility/failure modes; normal-speed motion inspection; isolated setup/maintenance preview; separately identified running prototype and genuine owner acceptance. | FR-004, FR-011, FR-015, FR-016, FR-019–FR-022, FR-026, FR-034; NFR-001–NFR-011 |

**FR-037 owner clarification, 2026-10-26 (D43/T47):** the current Settings presentation is not
owner-accepted. Continuous scrolling must keep text, glyphs, cards and native controls visually
registered without fragmentation or reconstruction on release. Every enabled actionable control
needs coherent hover/press/focus/state feedback, with responsive interruptible page/state transitions.
The title/caption region must share the intended neutral translucent material with the content in
supported normal mode while preserving native frame operations and system accessibility/fallback
policies. Verify intermediate moving frames and actual responsiveness; resting stills and final
positions are insufficient. Follow the [focused brief](docs/CLAUDE_V3_SETTINGS_FLUIDITY_PROMPT.md).
Its numeric tuning/performance targets remain engineering proposals on a stated configuration,
not new universal NFR-002 release budgets. Native input/UIA, consent, persistence and the current
Dock's accepted response are protected; no broader scope or release authorization changes.

**FR-026/FR-035 owner clarification, 2026-10-26 (D44/T41):** after the native-placement investigation,
the owner chooses a complete Aether-owned native Quick Settings recreation, faithful to the private
reference's tile/split-button/slider/footer composition, polished recognizable icon shapes and
interactive behavior, with Aether neutral frosted-grain light/dark materials. It opens below the
clicked Top Bar group. Claude implements; Codex prepares/transmits the brief and private pictures.
This supersedes native-panel preference as the target of this development batch: define an explicit
candidate route/default/migration, preserving installed Alpha 2 and any deliberately retained native
alternative. Preserve real supported behavior, consent, native UIA/input and FR-008 privacy; no
fabricated global toggle or success state. Unsupported parity is an explicit gap. Section 14 of the
focused brief defines QC-01–QC-10; nothing here declares implementation, verification or owner acceptance.

### Additional V3 owner direction — D45, 2026-10-26

| ID | Requirement | Priority | Acceptance | Verification | Dependencies |
| --- | --- | --- | --- | --- | --- |
| FR-038 | Genuine notifications and hidden notification-area access from the Top Bar. | P1 | Date/time opens the actual Windows notification center/calendar through the supported version-specific route. A distinct small chevron provides access to real hidden background-app notification-area icons; no fake icon inventory, notification content/badges or substituted Dock. Pointer, keyboard and UIA share guarded actions; preserve recovery, privacy and explicit unsupported/version/placement limits. Merely revealing the native taskbar is partial overflow completion. | Fake routing/error/lifecycle/layout/UIA checks; separately qualified Windows-version, hidden-taskbar, Explorer-restart and genuine overflow/manual behavior. | FR-011, FR-015, FR-017, FR-024; NFR-002–NFR-008; D45 |

**FR-011/D27 clarification:** D45 authorizes the dedicated hidden-icons chevron as a narrow exception
to the former trailing native-taskbar-arrow ban. It serves real tray overflow, with recovery retained
in Settings/logo menu/CLI. New default/migration must not let a legacy false silently suppress it.

**FR-036/FR-037 clarification:** all four visible corners and a thin continuous neutral Aether-style
perimeter are required around the complete window in normal and genuinely maximized presentation.
The owner's external-window still and rim close-ups reject treating region clipping alone as the
finished result. Preserve true maximize/placement/native caption/input and transparent changing-scene
cutouts; qualify Aether-owned and external rendering families separately. Unsupported targets remain
explicit. D40's bounded ownership/recovery rules remain; universal compatibility is not presumed.
Claude implements [the D45 brief](docs/CLAUDE_V3_NOTIFICATIONS_TRAY_FRAME_PROMPT.md); authorization
of the requirements does not establish implementation, runtime verification or owner acceptance.

## D. Non-functional requirements

| ID | Requirement and rationale | Priority | Acceptance / verification | Dependencies |
|---|---|---|---|---|
| NFR-001 | Native modular architecture without a heavyweight runtime. | P0 | C++20, Win32, Direct2D/DirectWrite and Windows.UI.Composition; no Electron; static CRT; module registry defines dependencies; no new third-party libraries without a recorded decision; Dock and Top Bar share no rendering or mutable UI state, update components do not depend on Dock rendering, no circular dependencies or shared writable globals, thread ownership and shutdown order documented (brief §4, §5.4). Inspect CMake and dependency closure. | D1, D8, D9 |
| NFR-002 | Responsive UI and event-driven idle behavior conserve resources. | P0 | Network/discovery/icon extraction/disk on workers with dispatcher delivery; UI requests no idle frames; animation uses elapsed time and aims for smooth 60 FPS on capable hardware without a continuous render loop when idle (brief §2). Instrument/inspect affected paths, measure startup/CPU/memory/search on stated configuration. No invented global numeric budget. | Q-001 |
| NFR-003 | Preserve shell integrity and limit system changes. | P0 | No Explorer replacement, Winlogon Shell edit, permanent taskbar disabling, security-feature disabling, elevation or hidden autoruns; D19/D23 permit only reversible visual taskbar hiding with independent recovery, a disclosed default and persistent opt-out; documented APIs preferred, shell-class visibility compatibility disclosed; Windows Settings takeover unsupported. Code/security review plus isolated integration and separate authorized desktop qualification. | FR-016, FR-018, FR-024 |
| NFR-004 | Public repository privacy and runtime data minimization. | P0 | No credentials, private keys or their locations, personal addresses/accounts/paths, machine names or unrelated content in public material; owner attribution allowed. No SSID or private request/exception details in logs. Automated test captures show only their own test windows over a private backdrop. Owner-initiated FR-029 showcase media may show the real desktop only after the user-started clean-session workflow and a frame-by-frame privacy review before public use. Review source/docs/fixtures/diffs and bounded log contents; do not claim a complete privacy audit without one. | AGENTS public-repository rule; FR-029 |
| NFR-005 | Reliable failure and lifetime handling. | P0 | Public module operations use Result<T>; exceptions do not escape module/thread boundaries; RAII; unavailable/error explicit; Stop quiesces listeners and releases provider resources. Exercise failures, cancellation, restart and listener lifetime; disclose unsupported concurrency/ownership cases. | NFR-001 |
| NFR-006 | Qualified platform/DPI behavior rather than assumed compatibility. | P1 | Target Windows 10 2004+/11 x64, asInvoker and Per-Monitor-V2; relayout on DPI/display change; Windows 10 tint fallback; claim only tested configurations. Record OS/DPI/monitor qualification. | D2–D4 |
| NFR-007 | Accessibility, keyboard, reduced motion and high contrast are required. | P0 | UIA names/control types/bounds/actions/focus and safe disconnect for custom surfaces; standard controls for supporting dialogs; keyboard completion and visible focus; reduced motion/high contrast verified. UIA and visual/manual review. | accessibility module |
| NFR-008 | Tests must protect the real desktop while providing relevant evidence. | P0 | Unit/integration/network labels correct; integrations capture only their test windows over private backdrop; temporary config/install roots and injected test registry; no real work-area/audio/Run/Uninstall changes or application launches in automation. Inspect tests before executing them. | D7 |
| NFR-009 | Reproducible quality, attribution and documentation. | P1 | Focused /W4 build warning-free, /WX Release CI, appropriate executed tests and independent review; MIT/SPDX/copyright in sources; English public docs/comments; no generated/build/local-tool state in Git; module docs and evidence updated. | FR-023; D7, D8, D12 |
| NFR-010 | Continuity and preservation across assistants. | P0 | Read handoff/spec/tasks/decisions, inspect Git and verify relevant prior work; preserve uncommitted/unrelated work, no unapproved destructive/publication actions; small coherent changes; no silent architectural reversal; update canonical handoff and requirement evidence. | D13, D15 |
| NFR-011 | Coherent native visual/input quality. | P1 | Floating rounded glass visibly reveals its backdrop with an honest no-blur fallback; Windows transparency-off/high contrast remain opaque; consistent original glyphs, light/dark and contrast support, interruptible animation and hit targets matching geometry; clipping/alignment/readability checked in own-window renders at relevant DPI. Owner refinement 2026-10-25: no rectangular shadow plates around raised or dragged Dock icons on light backgrounds; omit the shadow when the exact native footprint cannot retain a natural falloff. No claim of visual approval without inspection. | FR-004, FR-011, FR-022 |
| NFR-012 | Showcase capture integrity, cleanup and honest restoration. | P0 | The workflow is explicitly user-started; starting the separate tool is sufficient authorization with no second confirmation. D30 permits orderly closure then termination of remaining visible same-user apps, while protecting shell/security/critical processes, Aether, capture worker/controller ancestry and explicit retained infrastructure. Keep restart descriptors in memory only; never log/persist command lines or a process inventory. Use an isolated empty scenario-app profile and real pin identities; do not expose old documents. Per D33, stop only after scenario window absence, two captured body/backdrop matches with the original clean scene and a further three visually empty seconds, then attempt old-app relaunch before lossless buffer flush/offline rendering. Bound capture memory, fail explicitly on capacity/error and never silently drop source frames; keep blocking disk/provider work out of acquisition. Scene checks before/after captures reject foreign surfaces but cannot guarantee safe allowed-app contents or eliminate races: review every frame before sharing. Attempt restoration on ordinary success/cancellation/failure, report failed relaunches, and disclose that forced process termination/power loss bypass in-process cleanup. Unsaved work, tabs, documents, app state and arrangement cannot be guaranteed restored. Offline recipes remain local, bounded, source-coverage validated and free of process metadata; output never silently publishes or replaces existing files. Automated tests use fakes/own windows and never close real user apps. Verify through fake lifecycle/cancellation/failure tests, synthetic bounded-buffer/archive/recipe checks, process/identity and async-readiness/cleanup review, separately authorized native capture and every-frame privacy review. | D30, D33; FR-029; NFR-003–NFR-005, NFR-008 |

## E. Technical architecture and authority

The verified build structure is the CMake module registry in `cmake/AetherModules.cmake`; the service
contracts are `src/status/StatusTypes.h`, `src/applications/ApplicationTypes.h` and
`src/settings/Settings.h`. The existing base is `core → diagnostics → platform/windows → graphics → ui`.
Workers deliver to `IUiDispatcher`; windows/rendering belong to the STA UI thread. UTF-8 strings and
UTF-16 Windows strings use core conversion helpers. Module operations use `aether::Result<T>` and RAII.

Implemented consumers are Dock, Top Bar, search/settings UI and the `src/app` lifecycle. The install
library, launcher, updater and native installer are separate implemented targets, sharing trust and
transaction logic. Scoped test evidence and remaining product qualification are recorded below;
declaring a target alone is not implementation or acceptance evidence. Data roots are
`%LOCALAPPDATA%\AetherShell\{config,logs,cache,updates}`; installation layout is ADR 0005. Weather must
not depend on search; update verification must not depend on rendering. Detailed architecture and
the current file map remain in [PROJECT_CONTEXT.md](PROJECT_CONTEXT.md).

| Canonical file | Owns | Must not become |
|---|---|---|
| `PROJECT_SPEC.md` | Mission, scope, requirements, acceptance and traceability IDs | A rewritten reflection of whatever code currently does |
| `AGENTS.md` | Shared working rules, public privacy contract and discovery/build instructions | A competing product roadmap |
| `TASKS.md` | Ordered implementation tasks, dependencies and requirement links | Evidence that an unrun test passed |
| `DECISIONS.md`, `docs/decisions/` | Decisions, rationale, approval state and significant ADRs | An implicit approval of a proposal |
| `PROJECT_CONTEXT.md` | Repository map, architecture and limitations | A second requirements catalog |
| `HANDOFF.md` | Latest session's changes, exact checks, Git state and next steps | Permanent acceptance criteria |
| `docs/knowledge/TEST_STATUS.md` | Executed commands/results and qualification gaps | Product requirements or unverified claims |
| `docs/specs/`, `docs/modules/` | Detailed work orders and implementation references | Authority to override this spec or claim absent modules exist |

The Obsidian vault is the same `docs/` directory, not a second copy to synchronize. Root files sit
outside that vault and use relative Markdown links. Graphify data and its export are optional,
generated, local and uncommitted. Inferred graph edges are hints, never proof of behavior.

## F. Invariants and change control

- Preserve working behavior and all unrelated/pre-existing local work; avoid taste-driven rewrites.
- Preserve defined contracts/data compatibility; newer settings must survive a binary rollback.
- Never publish secrets/private material; keep the privacy rule in `AGENTS.md` authoritative even
  when a short reminder appears in a handoff or work order.
- Never execute unverified downloads, destroy the active installation prematurely, or make hidden
  system/startup changes. Keep exactly two persistent desktop surfaces in Version 1.
- Do not infer permission to publish a release/change repository settings from permission to code.
  Observe the current user's instructions for commits/pushes/destructive actions.
- A fundamental objective/protected-requirement change requires owner approval and a decision
  record; a source/spec mismatch is a defect or an explicit proposal, not an automatic spec edit.
- Update a requirement's acceptance, dependent tasks and evidence together. IDs remain stable.
- When approaches compete, the owner's order of preference applies (brief §19): system safety,
  documented Windows APIs, observable correctness, maintainable module boundaries, testable behaviour,
  visual performance, fewer dependencies, straightforward installation, graceful degradation, honest
  documentation. Never trade update authenticity, rollback safety, system stability or accessibility
  for appearance or speed of delivery.
- The owner's prohibitions (brief §21) always apply: no mockups or pseudocode presented as
  implementation, no unexecuted test claims, no invented repositories, releases, authentication or
  benchmarks, no downloaded scripts executed, no publishing credentials in the application, no
  automatic Explorer replacement, no disabled security mechanisms, no silent elevation, no deletion of
  the working installation before its replacement is validated, no fake clock, network, battery or
  weather, no status controls in the Dock, no monolithic source file, no concealed unsupported
  behaviour, no untested binary published as stable, no unrelated or unreviewed files committed.

## G. Definition of done and traceability

**Not started:** no implementation beyond declarations/contracts. **Implemented:** code/configuration
exists, possibly partial. **Tested:** named relevant checks actually passed on a stated revision and
configuration. **Verified:** every applicable acceptance criterion has evidence, independent review
issues are resolved/documented, and required manual/platform/security checks are complete.
**Blocked:** a named dependency or unresolved issue prevents the specified work; difficulty or a quota
interruption alone does not prove a technical blocker. **Unverified** qualifies any claim lacking
adequate evidence. Historical recorded checks retain their stated scope; label them historical and
not independently reverified this session rather than invalidating them solely because they are old.

Do not promote a whole requirement when only its pure helper or a fake passed. Record source/test
paths, command, configuration, result/count, revision plus dirty-tree qualification, date, review and
remaining manual checks in TEST_STATUS; link from task and requirement below. Re-run when affected
code/dependencies change or evidence is insufficient; do not repeat unrelated expensive checks.

### Version 1 release gate (owner definition of done, brief §17)
Version 1 is complete only when every row below is **Verified**. On 2026-10-18 no row is verified as a
whole; per-requirement evidence is in the traceability tables below.

| Brief §17 group | Items | Requirement IDs |
|---|---|---|
| Functional | Dock and Top Bar start and operate independently; real icons; launching; running detection and indicators; persistent pins; search returns real installed applications with keyboard use; real clock/date; battery accurate or unavailable; input language within documented limits; genuine network; volume accurate or unavailable; microphone scoped to supported APIs; weather online/offline states | FR-001–FR-013 |
| Reliability | responsive UI thread; provider failures contained; configuration errors handled; startup and shutdown; resources released; Windows shell keeps working; Explorer restarts handled; DPI/display changes tested; reduced motion | FR-001, FR-014, FR-017, FR-024; NFR-002, NFR-005–NFR-007 |
| Updates | release discovery; SemVer tests; manual and periodic checks; offline and rate-limit errors; compatibility and integrity validation; enforced publisher-authenticity policy; safe staging; tested installation, interrupted-update recovery and rollback | FR-018, FR-019, FR-021 |
| Distribution | installer built; fresh install, upgrade, repair and uninstall tested; shell intact; no developer tools needed | FR-016, FR-020; NFR-003 |
| Release | documentation matches the implementation; limitations recorded; test evidence available; assets and integrity metadata generated; release candidate validated; stable eligibility reviewed | FR-022, FR-023, FR-025; NFR-009 |

The T47 visual-unification implementation (FR-037/D41/D42) and Codex's interrupted-session
continuation are tracked in [S-049](docs/knowledge/TEST_STATUS.md) and the
[owned-surface inventory](docs/specs/v3-visual-unification-inventory.md). Native forms, ordinary
confirmations, popup menus, Settings, setup and updater share the presentation primitives. Native
combo dropdowns preserve their accessibility semantics as an explicit exception. Automated evidence,
own-window visual inspection, actual prototype identity and owner acceptance are separate gates;
this does not declare V3 or whole-product acceptance complete.

The following is the **0.1.0-alpha.1 baseline traceability** (2026-10-18), retained as historical
evidence rather than a passing V2 gate. **H** means historical recorded evidence in TEST_STATUS, not a new
execution or independent re-verification. **S** means this session's scoped evidence there. No entire
Version 1 requirement is declared verified by this table. Git/current-session details live in HANDOFF.
That baseline's complete Release /WX build and 426 unit cases in 17 executables passed. All 91 integration cases
in 14 executables have passing scoped final-binary runs; there is **no uniformly green combined
31/31 run**. The settled receipts and negative evidence are TEST_STATUS S-011. Aggregate GUI runs
encountered foreign full-screen/topmost overlap or focus-dependent
fixture closure; capture guards refused unsafe images, and isolated passing runs are recorded in
TEST_STATUS. Product source stayed fixed between those scoped runs; the final Dock changes were
fixture-only guard-band/z-order corrections. Top Bar idle assertions require bounded render
quiescence. The alpha was installed through production setup with startup absent, separately from
automated tests. Its public source, Windows CI and first prerelease receipts are in HANDOFF/S-012. These
observations do not establish whole-product or Version 1 acceptance.

Current combined V2 candidate (2026-10-20, TEST_STATUS S-022): full Release /W4 /WX build,
669 unit cases in 18 executables and 122 isolated integration cases in 14 executables pass.
The table below retains earlier per-requirement receipts; those counts are historical where S-022
supersedes them. This automated evidence does not qualify the new candidate on the real desktop,
physical monitors, actual Chrome/dictation/audio/startup workflows or a signed update cycle.

| Requirement | Implementation / verification entry points | Tasks | Current scope/evidence |
|---|---|---|---|
| FR-001 | `src/dock`, `src/topbar`, `src/app`; [surface evidence S-008 and lifecycle S-009](docs/knowledge/TEST_STATUS.md) | T3, T6, T7, T10, T11, T14 | Both bars and app wiring implemented; the isolated app lifecycle case and scoped own-window checks pass. Real reservation/full-screen/restart and physical DPI qualification remain pending. |
| FR-002, FR-004 | [Dock reference](docs/modules/dock.md); `tests/{unit,integration}/dock`; TEST_STATUS S-008 | T6, T11, T14 | Implemented; current Release /WX 14 unit + 2 native cases pass in scoped runs, including reviewed UIA/drag/AppBar fixes and guarded captures. Unsafe aggregate captures were refused; real activation/focus/desktop behavior remain unqualified. |
| FR-003 | `src/applications`; `tests/{unit,integration}/applications`; [applications reference](docs/modules/applications.md) | T5b, T11, T14 | Implemented; current Release 57 unit + 15 read-only integration cases pass (72 total). Actual external launches/UWP-frame qualification remain unverified. |
| FR-005–FR-008 | [status reference](docs/modules/status.md); `tests/{unit,integration}/status`; TEST_STATUS S-005 | T5c, T7, T11 | Implemented; fresh /WX 49 unit + 10 read-only integration + 1 isolated runtime-shutdown cases pass; independent review. GAP-003 resolved in scoped implementation; wider OS/hardware qualification pending. |
| FR-009 | [audio reference](docs/modules/status_audio.md); `tests/{unit,integration}/status_audio`; TEST_STATUS S-002 | T5d, T7, T11 | Implemented; scoped /WX 23 unit + 5 read-only integration cases and current dependency regression pass. Real controls/notification echo/device-service failures remain unqualified. |
| FR-010 | [weather reference](docs/modules/weather.md); `tests/unit/weather`; TEST_STATUS S-006 | T5e, T7, T8, T11 | Implemented; /WX 56 fake-based cases pass, including expiry while HTTP remains blocked. GAP-006 resolved; live forecast/geocoding and consent/attribution workflow remain unqualified. |
| FR-011 | [Top Bar reference](docs/modules/topbar.md); `tests/{unit,integration}/topbar`; TEST_STATUS S-008 | T7, T11, T14 | Implemented; current Release /WX 7 unit + 2 integration cases pass, including update-badge identity/clearing/idle assertions. Dark/light captures inspected and real providers sampled read-only; real AppBar/full-screen/restart pending. |
| FR-012 | [search reference](docs/modules/search.md); `tests/unit/search`; TEST_STATUS S-007 | T5g, T11, T14 | Implemented/reviewed; current Release /WX 7 cases pass. Synthetic p50/p95: 5000 apps 1.409/1.962 ms; 2000 apps 0.512/0.601 ms. Cancellation/closed-dispatcher checks pass; global release budgets remain Q-001. |
| FR-013 | [Search UI reference](docs/modules/search_ui.md); `tests/integration/search_ui`; TEST_STATUS S-008 | T8, T10, T11, T14 | Implemented; current Release /WX 5 integration cases pass with fake launches, guarded captures, finite GPU transitions and native EDIT name/Value/focusable checks from MTA. Actual IME/screen-reader flow remains unverified. |
| FR-014 | [settings reference](docs/modules/settings.md); `tests/unit/settings`; TEST_STATUS S-007 | T5f, T11 | Implemented; current Release /WX 33 unit cases pass for async persistence, validation, preservation/recovery, failures and shutdown quiescence. GAP-007 resolved through ADR 0007; isolated app shutdown also passes. |
| FR-015 | [settings native editor](docs/modules/settings.md); `tests/integration/settings_ui`; TEST_STATUS S-008 | T8, T11, T14 | Implemented; final scoped /WX 5 native cases pass after consent/model-merge/viewport corrections. Temporary files/injected callbacks only; physical DPI, screen-reader and live update/weather actions remain unqualified. |
| FR-016 | Settings chord/startup model and native editor; `src/app`, `src/install`; TEST_STATUS S-007–S-009 | T5f, T8, T9, T10, T11 | Configurable chords, consent-only startup and registration wiring implemented; model/fake-consent and isolated app gates pass. Authorized alpha installation observed startup absent. Real startup consent/Task Manager and shortcut conflicts remain unqualified. |
| FR-017 | [app reference](docs/modules/app.md); `src/app/ShellApplication.*`; `tests/integration/app`; TEST_STATUS S-009 | T2, T10, T11 | Native target/wiring implemented; current Release isolated lifecycle/health/channel/shutdown case passes with temporary data and private options. Health-before-discovery and cleanup-before-unlock reviewed; wider desktop/update handoff qualification remains. |
| FR-018 | `src/updates`, `src/platform/net`, `scripts/release`; [updates reference](docs/modules/updates.md); TEST_STATUS S-009 | T4, T5a, T11, T13 | Implemented; current Release net 24 unit + 28 read-only integration and updates 101 unit cases pass; 32 Python release/package cases pass. Three live-network cases excluded. Invalid signatures fail before shutdown; live owner-signed official update remains unverified. |
| FR-019 | Updates coordinator; [settings editor](docs/modules/settings.md); `src/app`, `src/topbar`; TEST_STATUS S-008–S-009 | T5a, T7, T8, T10, T11 | Coordinator, native controls and existing Top Bar update signal/menu/app wiring implemented; current editor/indicator regression passes. Launch observation has a timer fallback for failed UI completion posting. Real signed download/install flow remains unqualified. |
| FR-020, FR-021 | [install reference](docs/modules/install.md), [setup reference](docs/modules/installer.md); `launcher/`, `updater/`; TEST_STATUS S-009 | T9, T10, T11, T13 | Implemented; Release /WX 10 unit + 7 install integration and 5 setup cases pass, including maintenance/viewport and isolated app health checks. Authorized production setup installed the alpha without startup. Real signed update/recovery remains unqualified. |
| FR-022 | `resources/branding`; [branding reference](docs/BRANDING.md); TEST_STATUS S-007 | T5i, T15 | Original assets implemented as an alpha proposal; 12 raster sizes/8 ICO entries checked, contact sheet/banner inspected twice. Final owner concept/palette selection remains Q-002. |
| FR-023 | CMake/build/package scripts, `.github/workflows/ci.yml`; TEST_STATUS S-009–S-012 | T1, T9–T15 | Historical first alpha: source pushed, Windows CI passed, audited setup/checksums published. Its 426 unit and 91 scoped integration cases do not qualify V2; see the latest handoff for current delivery state. |
| FR-025 | `src/diagnostics` (rotating, sanitized logger), `src/app/CrashHandler.*` (bounded minidumps), Settings "Open log folder" and "Export diagnostics…" buttons | T2, T8, T10 | Implemented in part: logger, crash dumps and the Settings buttons exist in source; the historical logger unit case passed. The exported bundle's content and privacy statement have not been inspected: Unverified. |
| NFR-001 | CMake module registry, native targets and ADRs; TEST_STATUS S-009 | T1–T5, T10, T11 | Native implementations and complete Release /WX build with 426 unit cases pass; 91 integration cases have passing scoped runs. Target presence and this environment-limited aggregate evidence do not establish complete acceptance. |
| NFR-002 | WorkerPool/dispatcher/FrameScheduler; module threading/idle/search checks; TEST_STATUS S-005–S-008 | T2, T3, T5–T11, T14 | Current worker, quiescence/idle-frame and synthetic Release search checks pass in their stated scopes. Whole-product startup/CPU/memory/latency and owner-approved Q-001 budgets remain unqualified. |
| NFR-003, NFR-004 | App/install trust and mutation policy; privacy checks; TEST_STATUS S-009–S-012 | T4, T5, T9–T15 | Historical first alpha: temporary/fake/read-only checks, 457-file review and Release payload/export privacy gate passed. Old Debug output remains excluded. V2 changes require fresh source/package review; no complete security certification claimed. |
| NFR-005 | Result/RAII, status/settings/transport and install failure/lifetime tests; TEST_STATUS S-005–S-009 | T2, T4, T5, T9–T11 | GAP-003/GAP-006/GAP-007 fixes, shutdown quiescence, scoped install failures and isolated app health/channel/shutdown have tests/review. Terminal runtime/ownership limits documented; real signed production update/recovery remains unqualified. |
| NFR-006 | Manifest/platform DPI, scrollable native editors, ADR 0002; TEST_STATUS S-007–S-009 | T2, T11, T14 | Scoped Windows 11 x64 and synthetic DPI/viewport checks only. Windows 10, physical monitor/DPI transitions and real desktop behavior remain unqualified. |
| NFR-007 | [accessibility reference](docs/modules/accessibility.md), Dock/Top Bar/Search/settings editors; TEST_STATUS S-007–S-008 | T3, T5h, T6–T8, T14 | /WX 3 unit + 3 own-window UIA cases pass; separate-MTA consumer checks and keyboard/viewport regressions added/tested in scope. Successful foreground focus, actual screen-reader/IME/high-contrast product workflows remain unqualified. |
| NFR-008 | Test labels; temporary/fake/read-only/guarded-capture fixtures; TEST_STATUS S-005–S-009 | T1–T11, T14 | Reviewed temporary/fake/read-only fixtures and guarded captures; foreign overlap safely refused. Glass 2/Dock 2 and other native gates have passing scoped runs, with aggregate GUI environment failures retained. Automated tests do not alter real work area/audio/startup or launch external applications; authorized alpha delivery is separate. |
| NFR-009 | Build flags, SPDX/license, module references, packaging/CI and public docs; TEST_STATUS S-007–S-012 | T0–T5, T11–T15 | Historical first alpha: Release /W4 /WX, 426 unit cases, 91 scoped integration cases, independent reviews, export privacy, public CI and publication receipts recorded. No uniformly green combined matrix was claimed. V2 global gates remain separate. |
| NFR-010 | PROJECT_SPEC, AGENTS, TASKS, DECISIONS, HANDOFF and knowledge evidence | T-DOC-1, every task | Continuation evidence/gap dispositions maintained; stable requirement definitions preserved. D18 keeps original history local; final export/handoff audit remains required. |
| NFR-011 | Graphics/UI, original branding and Dock/Top Bar/Search captures; TEST_STATUS S-007–S-008 | T3, T5i, T6–T8, T14, T15 | Scoped light/dark synthetic captures inspected, geometry/idle checks and current GPU transition/update-badge cases pass. Physical DPI, high contrast, owner branding selection and complete product visual qualification remain pending. |
| FR-029, NFR-012 | `src/showcase`, `tools/showcase`; [capture notes](docs/showcase-capture.md); [D33 timeline](video_production/04_TIMELINE.md); [scorecard](video_production/06_SCORECARD.md); [TEST_STATUS](docs/knowledge/TEST_STATUS.md) S-031 and latest T36 receipts | T36 | Native protected lifecycle, isolated scene, nonblocking DXGI/actual cursor, bounded tile buffer, validated offline archive/recipe, asynchronous workbench readiness, eight-sample GPU/frame-ID cache and H.264/AAC/GIF are implemented. S-033 records 66 focused Release `/W4 /WX` cases and two own-window cases passing. Exact C5FF64A3 MP4 decodes all 1,030 1080p60 frames; two independent all-frame contact reviews plus critical full-resolution images observed only the permitted scene. Source samples average 57.3721/s with variable intervals; DXGI presents and offline render cost are reported separately. Ready/close timing and at least three visually empty central-scene seconds are measured. Cursor/toast/footer/orientation/GIF-channel defects are corrected. Normal-speed playback/listening and the full 60-check/ten-gate acceptance remain unverified; no 100/100 claim. |

All requirements have backlog coverage above. T0–T4 are prerequisite/maintenance work that also
support cross-cutting NFRs; they do not demonstrate the product is runnable. A requirement without a
task/evidence path must be recorded as a gap before it is called complete.

V2 revision evidence (T16; current full delivery gate pending):

| Requirement | Current scoped evidence and remaining work |
|---|---|
| FR-001, FR-002, FR-024 | [Dock reference](docs/modules/dock.md): 19 Debug `/WX` unit + 2 synthetic native cases pass with replacement layout enabled and real reservation prohibited. [Applications reference](docs/modules/applications.md): 66 Release `/WX` unit cases pass, including nine temporary-shortcut import cases. Platform fake-lease checks are in TEST_STATUS. Real native taskbar/AppBar/recovery qualification remains open. |
| FR-008, FR-011, NFR-011 | [Top Bar reference](docs/modules/topbar.md): 8 Release `/WX` unit + 2 integration cases pass; graphics 6 unit + 3 own-window glass probes pass. Backdrop change affects ordinary glass and leaves high-contrast material unchanged; eight synthetic captures inspected. Physical DPI/high-contrast user workflows remain open. |
| FR-024 (D21 recovery) | [platform](docs/modules/platform.md), [app](docs/modules/app.md), [setup](docs/modules/installer.md); TEST_STATUS S-015: 9 platform and 3 app simulated-taskbar cases plus 3 silent setup cases pass. Real hide → kill both processes → `--restore-taskbar` checked once on Windows 11 (S-016). Explorer re-showed the taskbar itself after the kill, so the hidden-after-double-failure case was not reproduced there. |
| FR-015, FR-017, FR-024 | [Settings](docs/modules/settings.md): fresh Debug `/WX` native editor suite passes seven cases with fake consent/import callbacks, including durable enable/disable and unchanged work area. [App](docs/modules/app.md) and [platform](docs/modules/platform.md) describe helper ownership and recovery commands; V2 app/global gates remain pending until recorded in TEST_STATUS. Source presence is not a passing gate. |


## H. Decisions, discrepancies and open questions

Decision IDs D1–D23 are retained in [DECISIONS.md](DECISIONS.md); ADRs 0001–0009 contain the significant
architecture rationale and alternatives. D15 records this specification/workflow and D18 the clean
public-history safeguard. A work-order example or future idea is not owner approval. Preserve
proposed/provisional/accepted distinctions.

| ID | Finding / current disposition | Owner / next action |
|---|---|---|
| Q-001 | No approved global startup, memory, idle-CPU or animation/search release budget. Preserve the existing search module engineering target <2 ms/2,000 apps; the suggested Debug p95 <20 ms assertion is an engineering example. Neither defines a universal release budget. | Release search cases pass: 2000-app p95 0.601 ms, 5000-app p95 1.962 ms. Owner still approves global budgets before whole-product performance qualification. |
| Q-002 | Original two-ribbon SVG/PNG/ICO/installer identity exists and has been visually inspected as an alpha proposal; no final owner-selected concept/palette is recorded. | T5i/T15 retain the concrete assets for owner direction; do not infer final branding approval from generation or scoped captures. |
| GAP-001 | ADR 0002/0003/0005 are accepted design choices; D3/D4/D6 retain provisional platform/product qualification. Scoped implementation tests do not settle those wider checks. | Preserve design and approval/verification distinction; T11/T14 record remaining desktop/platform qualification. Owner approval is needed for a design reversal, not routine implementation. |
| GAP-002 | Source-presence discrepancy resolved: settings/search/accessibility/Dock/Top Bar/app/install/launcher/updater/setup exist and the complete Release /WX build passes. Official clean-history source pushed, Windows CI green and the first manual-install alpha published (S-012). | Use TEST_STATUS S-005–S-010 for current scope; T11/T12 retain the scoped-versus-aggregate GUI distinction and finish public source/CI receipts. |
| GAP-003 | Resolved in scoped implementation: shared sessions/lifetime, Stop outside lifecycle locks and explicit terminal owned-runtime cleanup replace the unsafe overlap/self-destruction/MTA lifetime behavior. | ADR 0008 and TEST_STATUS S-005: fresh /WX 49 unit + 10 read-only integration + 1 isolated shutdown cases pass, independently reviewed. Preserve runtime ownership limits and wider OS qualification. |
| GAP-004 | Graphify runs from an ordinary shell on the development machine (`graphify --version` → 0.9.92, rechecked 2026-10-18 by Claude) but its launcher fails inside the Codex sandbox. No graph exists for this checkout (`graphify-out/` absent). | Optional local tooling: run `graphify update .` from an ordinary shell when wanted. Never a product or evidence prerequisite. |
| GAP-005 | Runnable app/installer source, Release build, 426 unit cases, isolated lifecycle and five setup cases pass. Production setup installed the alpha without startup; all 91 integration cases have passing scoped runs, but no uniformly green combined matrix. Package/export privacy and public CI/manual-alpha receipts are complete in S-012; older leaking binaries are excluded. | T9–T15 retain remaining platform/manual/update qualification; S-012 records the public clean source, green CI and audited manual alpha. |
| GAP-006 | Resolved without changing FR-010: the serial HTTP worker no longer blocks the scheduler that publishes six-hour cache expiry. | TEST_STATUS S-006: /WX 56 cases pass, including expiry before a blocked request returns; independently reviewed. Live forecast/geocoding and consent/UI qualification remain separate. |
| GAP-007 | Resolved by UI-owned settings state/notifications and FIFO worker disk persistence; no UI-thread disk exception was introduced. | ADR 0007 and TEST_STATUS S-007/S-009: current Release /WX 33 unit cases and isolated app shutdown pass. Retain accepted-versus-durable completion, model quiescence and flush ordering. |
| GAP-008 | Validated configurable chord fields and native editors now exist, including all fifteen nonzero modifier masks; the enable-only contract discrepancy is resolved in implementation. | TEST_STATUS S-007/S-008: Release /WX 33 model and 5 native editor cases pass. Isolated app regression passes; real global shortcut conflicts, startup policy and physical desktop qualification remain T10/T11/T14. |
| GAP-009 | Resolved in source on 2026-10-20: `ARCHITECTURE.md` and `TESTING.md` now provide public entry points linking PROJECT_CONTEXT/ADRs and BUILDING/TEST_STATUS. | T19: files and relative links inspected; keep implementation boundaries and test isolation guidance synchronized without duplicating live result counts. |
| GAP-010 | The public repository description differs from the one the owner specified. Brief: "A modular native Windows desktop interface with a floating application dock, system status bar, and original glass design." Current (`gh repo view`, 2026-10-18): "Native Windows application Dock and Top Status Bar, built with C++20, Win32 and Direct2D." | Owner decision: restore the brief's text or keep the current one. Changing repository settings needs explicit approval. |
| GAP-011 | D19 changes the brief’s native-taskbar coexistence rule; D21 requires recoverability without Aether Shell running before V2 publication. | D21 recovery is implemented: persisted baseline, standalone restore and setup repair/uninstall. Older owner-authorized desktop receipts are S-016/S-021 (e29e83e for auto-hide off/Explorer restart). S-021 records the owner dropping Windows 10 qualification for lack of a machine; Windows 10 runtime support is still unverified. D23 changes the launch default, preserving opt-out/recovery. The newly combined candidate’s desktop/recovery qualification remains open in T22–T24 / S-022. |
| GAP-012 | Resolved by D39/T45 (2026-10-25, S-040; pending Codex review). Formerly two local working copies plus linked/assistant worktrees diverged: the public clean checkout and the original private-history checkout (D18). | Claude, Codex and other assistants now share one checkout named `AetherShell` whose origin is the official repository (`AGENTS.md`, `docs/SESSION_START_PROMPT.md` step 1). The private history and retired worktrees are a verified local recovery archive outside the project, never a working folder. The owner's Obsidian vault path (`AetherShell/docs`) again points at the active documentation. |

None of these items invents a requirement. Owner decisions pending on 2026-10-18: GAP-010 (repository
description), Q-001 (performance
targets: brief §6 requires measurable targets for search opening, query latency, index rebuild and
memory; assistants propose values from measurements, the owner approves them) and Q-002 (branding
direction). Ask only when a palette, budget, architectural reversal or publication action is concrete
and actually needed.

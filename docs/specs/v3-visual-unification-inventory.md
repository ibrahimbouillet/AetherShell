<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Ibrahim Bouillet -->
# V3 visual unification — surface inventory and migration checklist (T47 / FR-037 / D41)

This is the tracked checklist required by the
[Claude visual-unification brief](../CLAUDE_V3_VISUAL_UNIFICATION_PROMPT.md). Each Aether-owned surface
records its caller, implementation, states, shared components, evidence and remaining exception.
Status labels are separate gates and are never inferred from one another:

- **implemented** — the redesigned source exists in this checkout;
- **tested** — focused automated checks executed and passed (see TEST_STATUS);
- **visually inspected** — own-window renders or a running prototype were looked at by the implementer;
- **owner accepted** — the owner said so explicitly, in their own words, for that surface;
- **blocked** — cannot be completed now; the reason is stated.

No row is owner accepted by this document. Windows-owned interfaces are listed only to record the boundary.

**Owner feedback, 2026-10-26:** the Settings motion/interaction and caption material were rejected
(D43). Claude implemented the [Settings fluidity brief](../CLAUDE_V3_SETTINGS_FLUIDITY_PROMPT.md)
(S-052); per-criterion evidence is in the SF table below. Earlier scoped passes remain historical
functional/layout evidence. Nothing here is owner accepted.

**Subsequent D44 direction:** Claude recreated the Quick Settings reference as a complete Aether-owned
anchored panel (section 14/QC-01–QC-10 of that brief, S-052); see the QC table below. Private
reference images stay local and were compared only in ignored scratch output.

Codex's final scoped gate is S-049: complete Release `/W4 /WX`, 24 unit executables and passing
runs of all 11 changed-consumer native executables. The first 11-suite native aggregate failed
only the two historical Dock entrance probes; their corrected fixture passes its full suite
separately. This is not a claim of one uniformly green aggregate rerun. All eight Settings pages
were captured in both themes; setup/updater have 32 state captures. Stills establish the inspected
regions, not every scrolled/focus/error state, live backdrop mode or normal-speed motion acceptance.

## Shared components

| Component | Implementation | Role |
|---|---|---|
| Design tokens | `src/graphics/Theme.*` (`FormTokens`, `MenuTokens`) | one material, 4-DIP rhythm, type scale and motion vocabulary for forms and menus |
| Glyph family | `src/graphics/Glyphs.*` | original 16-DIP round-stroke glyphs, extended with Settings, Appearance, Power, Taskbar, Plus, Minus, Folder, Download, Shield, Warning, Location, OpenExternal, Reset |
| Glass painter cache | `src/graphics/GlassPainter.*` | per-device caches (up to four) so form DC targets and composition surfaces share one UI thread without thrashing; `GlassStyle::border` |
| Nested modal loop | `src/platform/windows/MessageLoop.*` (`RunNested`) | menus/dialogs keep frame timers and dispatcher waits alive |
| FormSurface | `src/ui/form/FormSurface.*` | DWM Acrylic / opaque / native material for application windows; scene painting of native controls; layered opaque editors |
| FormScrollView | `src/ui/form/FormScrollView.*` | clipped scrolling content, overlay indicator, UIA Scroll pattern |
| ConfirmDialog | `src/ui/form/ConfirmDialog.*` | shared modal confirmation with MessageBox fallback |
| GlassMenuPresenter | `src/ui/GlassMenu.*` | composition glass popup menus behind `IMenuPresenter`, native fallbacks |
| Optional menu accessibility | `src/ui/accessibility/AccessibleSurface.*` | Invoke, Toggle, ExpandCollapse, Selection/SelectionItem and linked submenu fragments; existing models keep their defaults |

## Surfaces

| # | Surface | Caller → implementation | States | Shared components | Status | Remaining exception |
|---|---|---|---|---|---|---|
| 1 | Top Bar logo menu | `TopBarWindow::ShowItemMenu` → `BuildTopBarMenu(Logo)` | Search, Settings, reveal/resume taskbar (per replacement state), view update (when offered), check for updates, About, Quit; keyboard/mouse; outside click; Escape | GlassMenuPresenter, glyphs | implemented; S-049 gate | high contrast uses the system menu; owner visual acceptance pending |
| 2 | Top Bar item menus and tooltips | same, other `ItemKind`s; `TooltipWindow` | per item | GlassMenuPresenter; tooltips already neutral glass | implemented; S-049 gate | native Windows action qualification remains separate |
| 3 | Dock context and overflow menus | `DockWindow` menu path | item, pinned/running, overflow (~250 entries), background | GlassMenuPresenter | implemented; S-049 gate | owner motion/visual acceptance pending |
| 4 | Dock preview cards, tooltips | `DockPreviewWindow`, `TooltipWindow` | existing | already neutral glass (S-041–S-047) | unchanged | — |
| 5 | Search | `SearchWindow` | open, results, empty | neutral material (top sheen removed) | implemented; S-049 gate | physical IME workflow remains separate |
| 6 | Quick controls panel | Top Bar group → `ControlCenterWindow` (D44 owned Quick Settings) | two tile pages, split radios, Wi-Fi/Bluetooth/Accessibility/output details, sliders (pending/unavailable), footer, conditional media, keyboard/UIA, outside click, DPI close | neutral frosted grain, `SymbolRenderer` (installed icon fonts), shared tweens | implemented; tested (11 native cases, S-052); visually inspected (stills and motion sheets, dark/light) | real radio/audio/brightness/accessibility/media actions are manual checks; unsupported global switches are Settings routes; owner acceptance pending |
| 7 | Settings window and pages | `SettingsWindow` (Dock, Top Bar, Weather, Updates, General, About, Desktop, Appearance) | live apply, consent drafts, save failure, validation, small windows, DPI, theme change | FormSurface, FormScrollView, native combo dropdowns, ConfirmDialog | implemented; functional/layout/text-scale and own-window checks pass S-049 | native combo lists preserve Selection/ExpandCollapse; DWM shows its inactive fallback while the window is inactive; owner acceptance pending |
| 8 | Update presentations in Settings | Updates page | disabled, idle, checking, up to date, available, downloading (known/unknown total), ready (verified), error/retry, skip/postpone | FormSurface progress/status | implemented; injected state checks and own-window renders | live signed update cycle remains unqualified; engine unchanged |
| 9 | Reset confirmation | Settings Reset defaults… | confirm, decline | ConfirmDialog | implemented; shared keyboard/owner/scroll tests and reset seam | MessageBox only if the dialog cannot be created |
| 10 | Quit with unsaved settings | `ShellApplication` quit path | quit, keep open | ConfirmDialog | implemented and source-reviewed; shared dialog tested | no deliberate live settings-write failure imposed on the owner's profile |
| 11 | Diagnostics export / log folder | Settings General → app | exported, failed, cancelled | FormSurface status; Windows save dialog | implemented and source-reviewed | real file-picker/export walkthrough pending; save dialog is Windows-owned |
| 12 | Location permission explanation | Settings Weather | explanation, Windows location request, settings link | FormSurface | implemented; injected explicit-action tests | real Windows permission/forecast workflow remains separate |
| 13 | Setup wizard | `installer` (`AetherShellSetup`, `AetherShellSetupTest`) | welcome, licence, options (install/repair/uninstall), progress/cancel, done/failure, inline validation | FormSurface, FormScrollView | implemented; temporary-root journeys and dark/light captures | physical DPI, clean-machine qualification and owner acceptance pending; Browse/SmartScreen/UAC stay Windows-owned |
| 14 | Updater progress | `updater` (`AetherShellUpdater`) | verifying/applying, completed, failed | FormSurface, FormScrollView | implemented; actual presenter harness and dark/light captures | the harness excludes the updater entry point and never applies a real update |
| 15 | Emergency native dialogs | `main.cpp`, launcher, `ShellApplication` startup failures, `--restore-taskbar` result | invalid command line, data folders, unreadable settings, surfaces failed, recovery result | MessageBoxW (documented) | intentional exception | no renderer exists at these points |

## Settings fluidity — SF-01–SF-11 (D43, S-052)

Labels: *implemented*, *automated* (executed test passed), *frames* (moving own-window frames
inspected), *measured* (this configuration only: Windows 11, 150 %, one display). None is owner
accepted. Exact commands, numbers and failures are in TEST_STATUS S-052.

| ID | Evidence | Status and remaining gap |
|---|---|---|
| SF-01 | Before: dense own-window form scrolled by wheel bursts/reversals/fractional deltas showed detached or misregistered pieces in most distinct frames (both themes). After: atomic `PresentMove`, editor masking and retained layers; `ui_form motion` dark/light cases require zero incoherent frames. | implemented, automated, frames, measured |
| SF-02 | Same script: notch bursts, reversal, fast burst and fractional deltas stay coherent while the spring retargets with continuous velocity; fractional input applies immediately; move cadence is measured. | implemented, automated (wheel/touchpad deltas, scroll codes); a physical precision-touchpad gesture and scrollbar dragging by a person are unverified |
| SF-03 | Field wells animate a hover rim, navigation/list rows tween hover fills, buttons/switches keep press/hover tweens; containers take no hover; focus rings stay distinct. | implemented; per-control interruption matrix only partly automated; owner review pending |
| SF-04 | Page fades and container tweens present through the outermost container; tweens retarget from their current value. | implemented; rapid A→B→C→A navigation not separately automated |
| SF-05 | Integrated Acrylic title strip (brand mark, title, DWM caption buttons); settings_ui frame case checks caption/top/corner/menu/close/client hit testing, transparent title text and maximized client inset; dark/light own-window stills reviewed. | implemented, automated; inactive state is Windows' flat Acrylic fallback by design |
| SF-06 | Editors remain live native windows; masking only during motion, any input reveals; existing native editor and UIA cases pass. | implemented, automated; actual IME composition and screen-reader walkthroughs unverified |
| SF-07 | Retained layers are prepared during idle after a page is built; theme notifications without an appearance change keep them. | implemented; a captured cold/warm first-frame sequence was not recorded |
| SF-08 | Masked editors stay in place and hit-testable; caption hit testing automated; scrolled layout bounds checks pass. | implemented, partly automated |
| SF-09 | Reduced motion snaps scroll/tweens; Solid/Native materials use the same atomic path with full frames; existing material/policy cases pass. | implemented, automated for existing policy cases; policy switches during active scroll not separately captured |
| SF-10 | Existing idle cases (no form timers after settling) pass; layer preparation is a posted message, not a timer. | implemented, automated |
| SF-11 | settings_ui functional, consent, draft, persistence and DPI cases pass. | automated; real runtime in the private candidate (S-052) |

## Owned Quick Settings — QC-01–QC-10 (D44, S-052)

| ID | Evidence | Status and remaining gap |
|---|---|---|
| QC-01 | The private reference images were opened; the close panel was measured (360 × 388 DIPs at the reference scale) and mapped to tiles, page column, sliders and footer. | done; images stay local |
| QC-02 | UIA layout case asserts the exact page-1 item set; own-window stills match the reference's 3 × 2 grid, split tiles, labels, paging, slider zone and footer within about a pixel; media appears only with a session and never displaces page 1. | implemented, automated, visually inspected |
| QC-03 | Installed Segoe Fluent Icons (MDL2/vector fallbacks); unit cases check code points and centred ink at 100 % and 200 %; private side-by-side sheets at 1× and 2× compared every page-1 icon, including the airplane, with the reference. | implemented, automated, visually inspected; physical 100 %/200 % displays not used |
| QC-04 | Neutral frosted grain in dark and light over the private backdrop; high contrast uses system roles; no reference pixels or fonts are shipped. | implemented, visually inspected |
| QC-05 | Capability matrix in the [quick controls module](../modules/controlcenter.md); intents verified with synthetic snapshots for every tile, detail row, switch and slider; unknown/denied/unavailable states stay explicit. | implemented, automated; real device actions are manual checks |
| QC-06 | Motion case records entrance, hover retarget, press/release, interrupted and normal paging, interrupted and normal detail transitions, back and slider drag frame by frame; sheets reviewed. Capture loss ends a drag in source; not separately automated. | implemented, automated, frames |
| QC-07 | Anchor case: right edges meet, top 6 DIPs below the group, work-area clamping at left/right/bottom and off-monitor anchors, HWND placed before it is shown, repeated clicks, outside click, Escape, DPI close; keyboard and UIA cases. | implemented, automated; physical multi-monitor DPI unverified |
| QC-08 | Reference comparison sheets, dark/light stills, motion sheets; render counts and settle times per sequence; zero frames when settled or hidden. | measured on this configuration (UI-thread frames, not compositor latency) |
| QC-09 | Release `/W4 /WX`, graphics/system_controls/settings units, full unit gate and sequential native suites. | see S-052 for the exact aggregate and its exceptions |
| QC-10 | Local commit, private candidate with explicit Aether routing, preserved installed Alpha 2. | see HANDOFF and S-052; owner acceptance separate |

## Windows-owned (not redesigned)

Windows Settings pages, Quick Settings, the Start menu, Task View, voice typing, the calendar flyout,
location/microphone permission prompts, UAC/security prompts, SmartScreen and file/folder pickers stay
native. Aether's explanation before handing off to them is part of the owning Aether surface above.

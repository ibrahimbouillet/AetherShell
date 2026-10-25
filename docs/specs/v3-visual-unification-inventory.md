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
| 6 | Quick controls panel | `ControlCenterWindow` | existing | already neutral Bar material | audited, unchanged | — |
| 7 | Settings window and pages | `SettingsWindow` (Dock, Top Bar, Weather, Updates, General, About, Desktop, Appearance) | live apply, consent drafts, save failure, validation, small windows, DPI, theme change | FormSurface, FormScrollView, native combo dropdowns, ConfirmDialog | implemented; functional/layout/text-scale and own-window checks pass S-049 | native combo lists preserve Selection/ExpandCollapse; DWM shows its inactive fallback while the window is inactive; owner acceptance pending |
| 8 | Update presentations in Settings | Updates page | disabled, idle, checking, up to date, available, downloading (known/unknown total), ready (verified), error/retry, skip/postpone | FormSurface progress/status | implemented; injected state checks and own-window renders | live signed update cycle remains unqualified; engine unchanged |
| 9 | Reset confirmation | Settings Reset defaults… | confirm, decline | ConfirmDialog | implemented; shared keyboard/owner/scroll tests and reset seam | MessageBox only if the dialog cannot be created |
| 10 | Quit with unsaved settings | `ShellApplication` quit path | quit, keep open | ConfirmDialog | implemented and source-reviewed; shared dialog tested | no deliberate live settings-write failure imposed on the owner's profile |
| 11 | Diagnostics export / log folder | Settings General → app | exported, failed, cancelled | FormSurface status; Windows save dialog | implemented and source-reviewed | real file-picker/export walkthrough pending; save dialog is Windows-owned |
| 12 | Location permission explanation | Settings Weather | explanation, Windows location request, settings link | FormSurface | implemented; injected explicit-action tests | real Windows permission/forecast workflow remains separate |
| 13 | Setup wizard | `installer` (`AetherShellSetup`, `AetherShellSetupTest`) | welcome, licence, options (install/repair/uninstall), progress/cancel, done/failure, inline validation | FormSurface, FormScrollView | implemented; temporary-root journeys and dark/light captures | physical DPI, clean-machine qualification and owner acceptance pending; Browse/SmartScreen/UAC stay Windows-owned |
| 14 | Updater progress | `updater` (`AetherShellUpdater`) | verifying/applying, completed, failed | FormSurface, FormScrollView | implemented; actual presenter harness and dark/light captures | the harness excludes the updater entry point and never applies a real update |
| 15 | Emergency native dialogs | `main.cpp`, launcher, `ShellApplication` startup failures, `--restore-taskbar` result | invalid command line, data folders, unreadable settings, surfaces failed, recovery result | MessageBoxW (documented) | intentional exception | no renderer exists at these points |

## Windows-owned (not redesigned)

Windows Settings pages, Quick Settings, the Start menu, Task View, voice typing, the calendar flyout,
location/microphone permission prompts, UAC/security prompts, SmartScreen and file/folder pickers stay
native. Aether's explanation before handing off to them is part of the owning Aether surface above.

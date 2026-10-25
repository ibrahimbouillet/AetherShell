# Claude implementation brief — one coherent Aether visual language

You are continuing Aether Shell as the native C++ UI implementer. The owner rejects the unfinished
appearance of Settings and asks for a substantial, polished redesign across **all Aether-owned
interfaces**, starting at the Top Status Bar logo menu. Implement the working product, inspect it,
and leave a precise handoff for Codex's independent review. Do not stop at a mood board, mockup,
palette change, or a plan.

## 1. Establish the real starting point

Work in the **one shared AetherShell checkout**. Verify that `origin` is
https://github.com/ibrahimbouillet/AetherShell, then inspect HEAD and staged/unstaged/untracked work.
Do not create another clone/worktree, reset files, discard drafts, or assume another assistant has
finished because a previous response says so. Coordinate disjoint files if using parallel agents.

Read AGENTS.md, HANDOFF.md including NEXT SESSION START HERE, PROJECT_SPEC.md FR-011/FR-015/FR-016/
FR-019–FR-022/FR-026/FR-034/FR-037 and NFR-001–NFR-011, TASKS.md T47, DECISIONS.md D26/D27/D38–D41,
docs/specs/v3-design-and-interaction.md, and the relevant graphics/UI/settings module notes.
Read TEST_STATUS S-046/S-047 before altering the latest rendering fixes. Markdown and current
source are the shared contract; optional Graphify/Obsidian availability is not a prerequisite.

At preparation, documentation HEAD was `a442b2e`; the active private prototype was built from
`661a4cd` (verify actual process/path/hash/receipt before relying on this historical checkpoint):

- `eb15de0`: transparent maximized-window cutouts with independent DWM material/region recovery.
- `661a4cd`: removal of clipped rectangular Dock panel/lift shadows on light backgrounds.
- S-046: 58 app unit and 12 native cases pass; S-047: two dark/light Dock pixel probes pass.
- The installed Alpha 2 and startup registration are preserved. The temporary prototype retains
  Alpha 2 version metadata; it is not an installed or released Alpha 3.
- The owner withdrew an apparent automatic acceptance reply. Do not convert it into approval.

The supplied private Settings screenshot shows the problem: a large white client area, grey
rectangular label strips, a blue stock list selection, raw values such as `0.800000`, cramped or
truncated controls and little relationship to Aether's Dock/Top Bar. Do not copy that screenshot,
its surrounding desktop, or other app content into this public repository.

## 2. Product and art direction

The complete journey must feel like **one carefully designed native product**:

**Top Bar logo -> Aether menu -> Settings -> every page, dropdown, confirmation, update state and
return to the desktop.** No visually unfinished side of the product should appear after an
otherwise attractive entry point.

Use the existing Aether identity as the foundation: neutral translucent frosted glass, a very fine
sand-like grain, a restrained light rim, refined round geometry, clear typography, proportional
rounded icons and fluid, readable motion. The ambition is the care of a premium desktop product,
with the deliberate motion the owner associates with Apple and After Effects. Those are quality
references, not permission to copy proprietary assets or add a web/Adobe runtime.

The owner previously rejected directional lighting. Their new request for attractive lighting
means subtle edge definition, restrained focus feedback and balanced contrast: **no simulated
sunlight, bright top-to-dark-bottom sheen, travelling glare, neon bloom, glossy plastic, or square
hover plates behind Dock logos**. Keep the material neutral on every side. A quiet, excellent
interface is preferable to piling on effects.

### Material and geometry

- Reuse and extend `graphics::DesignTokens` in `src/graphics/Theme.*`, `GlassPainter.*`,
  `CompositionSurface.*` and `ui::SurfaceWindow`; do not create a parallel per-window design system.
- Make the backdrop genuinely visible through the window where supported, with actual blur and
  a subtle stable grain. A grey opaque fill labelled "glass" does not satisfy the normal mode.
  Keep text and control contrast readable over bright, dark and busy private test backgrounds.
- Use a thin balanced rim and consistent nested corner radii. Avoid a stack of heavy borders,
  oversized empty cards, excessive padding, large blank margins and decorative chrome.
- Use a shared 4-DIP spacing rhythm. Starting proposals: outer spacing 20–24 DIPs, section gaps
  16–24, related-row gaps 8–12, controls around 36–44 high, window radius around 16–20 and control
  radius around 8–10. These are tuning proposals, not new fixed owner-approved measurements.
- Rounded cutouts must reveal the real scene, never a black backing or copied wallpaper. Preserve
  S-046's material recovery. Preserve S-047's shadow removal: no decorative expansion of the native
  hit region and no reintroduction of clipped square shadow fragments.
- Windows transparency disabled, high contrast, unsupported backdrop APIs and device loss need an
  intentional readable fallback. Accessibility policy takes precedence over translucency.

### Typography, colour and icons

- Use the shared DirectWrite type system, preferably installed Segoe UI Variable with a system
  fallback. Do not redistribute font binaries. Define a compact hierarchy for window title,
  section title, body, helper text and status; support Windows text scaling without clipping.
- Aim initially for readable 13–14-DIP body text and a 22–26-DIP page title, then inspect at real
  DPI. Use weight and spacing purposefully; do not make every label large, bold or uppercase.
- Use neutral graphite/soft translucent light surfaces with restrained existing Aether/Windows
  accent colour for selection, focus and primary actions. Keep dark/light modes equally finished.
- Draw consistent native vector glyphs through the existing icon family. Match optical size,
  stroke, rounded caps and alignment; avoid stretched symbols, emoji or unrelated downloaded packs.
- Give disabled, hovered, pressed, selected, focused, busy and error states distinct, accessible
  appearances. Keyboard focus must remain visible even when hover decoration is subtle.

## 3. Complete surface inventory and priority

Audit all Aether-owned visible surfaces and create a tracked migration checklist. Each entry must
identify its caller, implementation, normal/error/disabled states, shared components, test evidence
and remaining exception. Do not declare the whole application coherent from one Settings image.

**First deliver a complete vertical slice:** logo menu, Settings window, one complete settings page,
its input widgets/dropdowns and a real confirmation/status flow. Carry that visual standard through
the remaining inventory rather than stopping at the first attractive screen.

Inventory includes:

1. The Top Bar Aether logo menu, its submenus and item tooltips.
2. Every Settings page: Dock, Top Bar, Weather, Updates, General, About, Desktop and Appearance.
   Preserve the page enumeration/routes and any additional real controls found in current source.
3. Update checking/available/current/downloading/verifying/installing/error/retry/skip/postpone
   presentations and Aether-owned confirmations.
4. Aether-owned reset, save failure, diagnostic export, permission explanation and quit flows.
5. Dock context menus, preview chrome/tooltips, Search, Aether quick-controls/flyouts and any other
   owned popup. Reuse what is already coherent; change only the parts that violate the shared style.
6. **The complete installer and maintenance interface is mandatory in this redesign**, not an
   optional branding pass. Include fresh installation, upgrade, repair, uninstall and recovery
   presentation, preserving the existing safe deployment engine and consent policies. Follow
   the installer-specific acceptance below; do not leave the current stock wizard untouched.

Native Windows Settings, Quick Settings, UAC/security prompts, system permission dialogs, file
pickers and other applications remain Windows-owned. Do not impersonate, embed, reskin or hook
those interfaces. Aether's explanation before handing off to Windows must still be coherent.

### Installer and maintenance specifics — explicitly confirmed by the owner

The first screen a new user sees must already feel like Aether. Apply the same neutral frosted
material, fine grain, restrained rim, rounded native icon family, typography, spacing and motion
to the actual installer in `installer/main.cpp`. A logo/banner replacement or recoloured old
wizard is insufficient. Read `docs/modules/installer.md`, `docs/modules/install.md`,
`installer/CMakeLists.txt` and the existing isolated setup tests before editing.

Cover the full journey and every available state:

- Welcome/product identity and the explanation of reversible taskbar replacement.
- Readable complete MIT licence, acceptance/navigation and keyboard access.
- Installation destination/Browse, shortcuts, sign-in startup choice and existing-setting state.
- Genuine installation/upgrade progress, verification state, cancellation availability and any
  point after which atomic activation must finish. Unknown progress stays indeterminate.
- Finish/launch choice, failure/retry guidance and recovery instructions.
- Repair and uninstall entry, confirmation, progress, result and explicit keep/remove-data choices.

Preserve D34's disclosed startup choice and opt-out, existing choices/Task Manager disables,
the restore-taskbar shortcut, safe process handoff, exact embedded payload/hash verification,
transaction/rollback and user-data preservation. Do not change command-line/silent behaviour,
exit codes, elevation policy or registration semantics for visual reasons.

Setup must work on a clean supported machine **before Aether is installed**, using its bundled
resources and supported Windows components. Do not depend on an existing Aether process, user
profile, developer tools, downloaded fonts or a new heavyweight runtime. Share suitable rendering
primitives/resources without coupling setup to the running Dock or settings instance. Keep an
intentional accessible opaque fallback when glass is unavailable, and a reliable native emergency
error path before graphics starts; these bounded fallbacks do not exempt the normal wizard from
the redesign. Windows-owned Browse/security/SmartScreen interfaces remain native.

Build the real `AetherShellSetup` and isolated `AetherShellSetupTest` targets. Inspect all wizard
and maintenance pages over owned private backdrops, including keyboard, text scaling, small
viewport, DPI, contrast, cancellation and error states. Use temporary roots and the existing
test-only isolation path: no real installation, startup/registry change, owner-app closure or
uninstall on the owner's computer for automated checks. Produce a reviewable setup candidate,
without installing over Alpha 2 or publishing it. A working application-only redesign does not
complete T47 while the installer is still unfinished.

### Logo menu specifics

Retain the current `MenuEntry` command IDs, availability, enabled states and event routes. Include
Search, Settings, notification-area/replacement recovery, available update, check for updates,
About and Quit, plus any other commands found in the actual model. The current source says
"Show Windows notification area for 60 seconds"; the owner's spoken example is not permission to
change the timeout. Reflect the actual policy/state and preserve resume/recovery access.

Use a compact frosted panel anchored to the clicked logo, with a consistent icon column, clear
labels, aligned shortcuts, subtle grouping and restrained hover/press feedback. Long labels must
fit or wrap deliberately. No giant menu, clipped text, unexplained blank gutters or full-width
stock blue blocks. Submenus, checks, radio states, separators and disabled commands belong to the
same system. Check/update items must display real state, never a simulated success.

Opening/closing, outside-click dismissal, Escape, arrow navigation, mnemonics, submenu travel,
focus restoration and display-edge placement must work throughout. A translucent screenshot is
not a substitute for a usable menu.

### Settings specifics

Build a coherent native settings workspace: modest identity/title area, compact icon navigation,
readable selected-page indication, properly aligned content sections and a clear status/action
area. Size it to useful content; support resizing, small work areas, scrolling and text expansion.
Avoid the current fixed-coordinate collection of unrelated stock rectangles.

Use well-spaced setting rows with short labels, optional helper text and properly aligned controls.
Use switches/checks for booleans, appropriate sliders/steppers for ranges and legible combo choices.
Format values for people: e.g. `80%` rather than `0.800000`; retain exact underlying values and
validation. Explain units where useful. Reuse existing actions rather than adding decorative knobs.

Do not cut content off behind a footer. Keep status, retry and consent actions visible when the
page scrolls. A healthy layout has no arbitrary large blank columns or dense tiny controls.

Preserve all functional contracts:

- Ordinary changes currently use debounced live application; startup and taskbar replacement need
  explicit Apply consent. Display those differences honestly. Navigation/theme/status refresh must
  not commit consent drafts, overwrite a user's edit, reset focus/selection/scroll or lose input.
- Keep settings validation, migrations, future-schema preservation, backups, asynchronous durable
  save, revision/generation checks, dirty/error/retry states and `PrepareToClose` draining.
- Keep pin identity/order/import controls, weather/location permission/privacy text and actual
  provider/update state. Do not invent a location, SSID, percentage or update version for appearance.
- Reset must preserve the taskbar replacement choice under D23 and obey existing startup/reset
  semantics. Export diagnostics must retain the privacy protections.
- About must show the genuine version, public author, MIT and required provider attribution.
- Signed-manifest/SHA-256 update verification and installation/recovery behavior are not redesign
  targets. A successful-looking animation must never stand in for verified completion.

## 4. Native architecture: solve the actual rendering gaps

Start with the real integration points:

- `src/settings/ui/SettingsWindow.*`, `src/settings/SettingsService.*` and settings integration tests.
- `src/app/ShellApplication.cpp`, especially Settings construction and `ApplySettings` theme routing.
- `src/topbar/TopBarMenu.*`, `TopBarWindow::ShowItemMenu` and action validation.
- `src/ui/PopupMenu.*`, `ThemedMenu.*`, `SurfaceWindow.*`, `TooltipWindow.*`.
- `src/graphics/Theme.*`, `GlassPainter.*`, `Glyphs.*`, `TextCache.*`, `Animation.*`,
  `Transition.*`, `CompositionSurface.*`, `FrameScheduler.*`.
- Existing Search/ControlCenter/Dock preview surfaces and UIA providers as appropriate.

Verified code facts at prompt preparation:

- Settings creates standard STATIC/EDIT/BUTTON/LISTBOX/COMBOBOX children over an opaque
  `COLOR_WINDOW` client area. Applying dark mode only to its native title bar does not theme its
  contents. It currently receives neither the shared DesignTokens nor MotionPolicy. Add an explicit
  theme/motion update route, including system changes, while preserving current editor state.
- `ThemedMenu` owner-draws an HMENU through a Direct2D DC target with alpha ignored and a solid
  popup background. Changing its RGB brush or whole-window opacity does not produce true backdrop
  glass; whole-window opacity would also fade text. Do not claim otherwise.

Choose a focused native/hybrid presentation architecture. Keep useful service/controller code.
For glass popups, a composition-backed transient presenter can implement the existing
`IMenuPresenter`/`MenuEntry` contract, but must reproduce its command, lifetime, focus and keyboard
semantics. Reuse a shared component rather than creating a special Settings-only menu.

Settings must remain a normal activatable, resizable application window with caption dragging,
minimize/maximize/close, Alt+Tab, text focus and IME. Do not inherit `SurfaceWindow`'s shell-surface
defaults (`topmost`, `noActivate`, `toolWindow`) unchanged. Give transient popups their appropriate
owner/activation behavior separately. Prove the first hybrid native-control/compositor slice in
actual pixels and keyboard use: GDI children over a NOREDIRECTIONBITMAP composition host can have
redirection/airspace issues. A correctly tinted parent does not prove its child inputs render.

NFR-007 still requires standard controls for supporting dialogs. Preserve native text editing,
IME, clipboard, selection, caret, shortcuts and accessibility where practical through native
editors/control hosts and suitable owner drawing. Do not replace a working textbox with painted
text and mouse handlers. If a custom control is necessary, record a compatible design decision
and prove the equivalent UIA patterns/keyboard behavior before replacing the native path; this
brief does not waive NFR-007. The existing generic accessibility surface alone may not provide
Value, Selection, ExpandCollapse or Scroll semantics required by Settings.

Reliable native error dialogs remain a documented last resort before graphics initialization or
after catastrophic renderer failure. Ordinary healthy-state Aether confirmations should use the
shared presentation. Do not hide an initialization failure behind an unusable transparent dialog.

## 5. Motion must be visible, continuous and restrained

Create a small shared motion vocabulary for menus, page changes, selection indicators, buttons,
switches, dropdowns and async progress. Do not globally retune the Dock magnification the owner
already likes. Suggested starting ranges are 160–220 ms for small feedback, 220–300 for popups and
260–360 for page transitions; tune at normal speed rather than adopting numbers blindly.

Opening should settle into its anchor with a small eased translation/scale and fade. Closing should
be coherent and cancellable. Page changes can use a subtle short slide/crossfade with stable
navigation; selection/focus feedback should transition cleanly. Avoid wobble, long theatrical
delays, excessive blur, flashing and large bouncing forms.

Retarget from the current state/velocity during interruptions. Rapid page switching, open-close-
open, resize, theme change, display/DPI change and async updates must not snap, ghost, lose focus
or leave an invisible input window. The first click must work before a decorative entrance ends.

Use elapsed time, native composition and the existing scheduler. No UI-thread sleep, network,
disk or blocking discovery; no fixed-step animation tied to frame count and no idle render loop.
Target smooth cadence on the tested hardware, measure representative transitions, and report
actual frame/cadence evidence without inventing a universal FPS guarantee. Reduced motion and
high contrast must immediately stop unnecessary motion while keeping every action usable.

## 6. Verification and delivery gates

Keep a surface/state checklist with **implemented / tested / visually inspected / owner accepted /
blocked** labels. A build, still image or attractive description does not establish all five.

Use meaningful focused checks for changed behavior, not thousands of redundant tests. Include:

- Every Settings page opens, lays out and keeps its real controls/actions; long labels/values,
  small windows, scrolling, resize and representative 96/144/192-DPI layouts remain usable.
- Light/dark, text scaling, high contrast, reduced motion, transparency disabled, backdrop failure
  and device recreation retain readable content and accessible focus.
- Menus preserve command IDs/disabled state, nested selection, keyboard/mnemonics, Escape,
  outside-click dismissal, edge placement, foreground restoration and cancellation during teardown.
- Draft/consent/save behavior, stale async completions, page close, update state changes and actual
  failure messages remain correct. Tests use temporary settings and injected operations.
- Own-window composited renders over changing private bright/dark patterns establish genuine
  transparency, readable contrast, clear rounded cutouts and no clipped square shadow fragments.
  Verify actual pixels rather than merely asserting a flag or region shape.
- Inspect normal-speed animations, not only contact sheets. Measure idle frame cessation and
  representative motion cadence; preserve the S-046/S-047 regressions where affected.

Build the integrated application and focused changed targets through `scripts/build.ps1`, Release
`/W4 /WX`. Tests must not change real devices/settings/startup/work area or close/activate owner apps.
Use fake providers and owned windows over guarded private backdrops. No fake success data in the
real product. Keep all screenshots/video local until privacy review; no unrelated content in Git.

The owner wants to **see the finished prototype running on their computer**. After a coherent
gate, announce the concrete prototype and launch it through the existing bounded private-profile
swap. Their session request already authorizes that prototype swap; do not introduce another
generic approval checkpoint. Preserve current pins and installed Alpha 2, avoid concurrent shell
instances, suppress private-profile startup/update/pin-import side effects, verify actual executable
hash/parented helpers/readiness, and keep a recovery path. Do not close unrelated owner apps.

D36 is separate: any assistant-operated real desktop capture/recording still requires a fresh
readiness question and explicit ready reply for that attempt. Do not use prototype-launch
authorization to bypass recording readiness. Own-window automated renders are separate.

Do not install over Alpha 2, push, tag or publish V3. Make coherent verified **local commits** and
leave HANDOFF.md, TASKS.md, TEST_STATUS, affected module docs and requirements/decisions current in
English. Record exact source/build/test/runtime receipts, known exceptions and unfinished surfaces.
If interrupted, leave a usable checkpoint and clear next steps rather than claiming completion.

Your final report must identify the surface inventory covered, concrete before/after changes,
commits, executed checks, inspected visual/motion evidence, actual running prototype and remaining
limits. Invite the owner's genuine visual feedback; never infer it from silence or an automatically
arrived response. Codex will review the implementation afterward. Do not label V3 complete merely
because this redesign batch is ready.

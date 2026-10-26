# Quick controls

Requirements FR-026/FR-015 and D26. `controlcenter` is a transient accessible panel, not another
persistent desktop surface. Production Top Bar groups network, sound and battery into one action;
individual preferences still determine which status components are displayed.

V3 source follow-up (T41/S-036): on Windows 11 the grouped action requests Windows Quick Settings
with one documented Win+A chord when the saved flyout preference is enabled. The Top Bar page exposes
**Use Windows Quick Settings (Windows 11)**; clearing it uses Aether controls. Older/unknown Windows
and isolated tests keep the custom panel. The app closes only its own panel before requesting Windows.
A submitted chord does not acknowledge shell opening; UIPI can silently filter it. No blind retry or
competing popup follows uncertainty. Native placement with taskbar replacement is still unverified.
Installed Alpha 2 is unchanged.

Owner follow-up, 2026-10-26 (T41/FR-035): native opening works in their trial, but its bottom placement
is rejected; they request an anchor beside the clicked Top Bar group. The current Win+A wrapper
accepts no rectangle. `ShellApplication` uses the emitted anchor only for the owned Aether panel,
whose `Toggle(anchor)` positions below/right-aligned and clamps to the clicked monitor work area.
No documented native placement contract was identified during source/docs inspection. Follow
[the focused brief, section 14](../CLAUDE_V3_SETTINGS_FLUIDITY_PROMPT.md); this is feasibility work,
not an implemented or verified shell-panel relocation. Preserve the saved native/fallback choice.

The panel receives immutable real snapshots and emits explicit intents. It never discovers devices,
opens Settings, changes audio/radios/brightness or injects input itself. The app bridge admits actual
controls only for the production native-access instance, queues blocking work on a dedicated serial
MTA worker, cancels and joins it before teardown. Test instances cannot invoke the bridge.

The upper card presents a selected live SMTC session with supported previous/play-pause/next actions
and a source-cycle action when several sessions exist. Selection is local to Aether, not a change to
Windows' global current session. No private media metadata is logged or persisted. No compatible
session is an explicit unavailable state. Embedded artwork/source logos are not implemented yet.

The lower card has two pages. Wi-Fi/Bluetooth have separate explicit toggle/settings targets;
permission is requested only after a direct radio click on the UI STA, never on panel open or refresh.
Policy/hardware/unknown states fall back to the corresponding Settings page. Airplane mode, energy
saver, night light, hotspot and nearby sharing are labelled Settings actions: no unsupported global
switch is emulated. Accessibility/Live captions/Cast/Project use fixed supported Windows routes;
captions requires build22621. Battery, sound, display and all-Settings shortcuts remain available.

Brightness supports only one confidently matched active internal display; unavailable layouts use
Display Settings. Slider rendering reports observed values (or an explicitly dragged target), never
claims an unconfirmed requested setting was applied. Volume uses the existing async Core Audio
provider. Sun-ray length animates with brightness and snaps for reduced motion. Unknown values are
unavailable, not zero. Signal waves use the existing Windows-reported network bars; unknown remains
distinct from known zero. There are no SSIDs or device identifiers in public fixtures.

The application also republishes current audio observations immediately to an open panel through
the Top Bar's UI-thread `AudioObservedChanged` signal. External endpoint changes no longer wait for
the 1.5-second media/status refresh before reaching the slider and UIA RangeValue. The event does
not issue audio commands, discover devices or schedule work for a hidden panel. S-034 records the
isolated regression and separately coordinated real-capture boundary.

UIA RangeValue and keyboard controls share real enabled/range semantics. Closed/deactivated/DPI-changed
panels reject queued actions; split-radio UIA bounds do not overlap. Refresh is on demand while shown;
closed panels schedule no polling. Source presence is not qualification: see TEST_STATUS S-026,
including the prior S-025 MTA discovery issue. Real radio/media/brightness actions require separate
manual checks and are never automated by integration tests.

S-026 executed scope: full Release /WX build,698 aggregate units; this panel's4 fake native cases and
accessibility6 native cases pass. The copied local preview is active at source checkpoint d7e17c4.
Actual controls/session support still require owner manual checks; the moving radial capture overlay
has no proven cause. No package/install/publication is qualified by these checks.

# Quick controls

Requirements FR-026/FR-015 and D26. `controlcenter` is a transient accessible panel, not another
persistent desktop surface. Production Top Bar groups network, sound and battery into one action;
individual preferences still determine which status components are displayed.

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

UIA RangeValue and keyboard controls share real enabled/range semantics. Closed/deactivated/DPI-changed
panels reject queued actions; split-radio UIA bounds do not overlap. Refresh is on demand while shown;
closed panels schedule no polling. Source presence is not qualification: see TEST_STATUS S-026,
including the prior S-025 MTA discovery issue. Real radio/media/brightness actions require separate
manual checks and are never automated by integration tests.

S-026 executed scope: full Release /WX build,698 aggregate units; this panel's4 fake native cases and
accessibility6 native cases pass. The copied local preview is active at source checkpoint d7e17c4.
Actual controls/session support still require owner manual checks; the moving radial capture overlay
has no proven cause. No package/install/publication is qualified by these checks.

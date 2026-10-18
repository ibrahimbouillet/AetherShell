# Top Status Bar — implemented, scoped checks passed

Requirements: [FR-001, FR-005–FR-011](../../../PROJECT_SPEC.md). Task T7;
[executed scope S-008/S-011](../TEST_STATUS.md), [implementation reference](../../modules/topbar.md).

`src/topbar` owns a separate floating native window with localized clock/date, battery, input
language, network, volume, microphone and optional weather. Real provider snapshots arrive through
the UI dispatcher; unavailable values remain unknown. Narrow layouts collapse weather then language
before higher-priority items. Original vector glyphs, tooltips, logo menu, keyboard actions and UIA
use stable item identities. No system status is placed in the Dock.

The current Release /WX gate passed 7 unit + 2 integration cases. Dark/light synthetic captures were opened;
material pixels, unchanged work area and idle frames checked, real providers sampled read-only.
Independent review corrected wheel accumulation, semibold clock width and AppBar feedback/restart.
The existing logo's update-available arrow/status/menu API and application wiring are implemented.
Its stable-identity, immediate-clearing and duplicate-flag idle assertions pass after bounded render
quiescence; an unending frame loop cannot satisfy that fixture gate.

Runtime top-edge reservation, full-screen hiding and Explorer-restart recovery follow
[window behavior](../../decisions/0003-window-behavior.md). Tests prohibit AppBar operations and use
only private opaque backdrops. Real reservation/full-screen/restart, physical DPI/Windows 10,
real audio controls and full screen-reader workflows remain unqualified.

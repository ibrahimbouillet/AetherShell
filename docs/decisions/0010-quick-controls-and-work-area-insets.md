# ADR 0010: Quick controls and maximized-window side insets

Status: owner-approved scope (D26, 2026-10-21); native implementation qualification in progress.

## Context
The owner requests a grouped network/audio/battery action with media and system controls, neutral
sanded Dock/Bar glass, and symmetric margins matching the Top Bar. The owner explicitly limits
horizontal alignment to maximized windows and asks to keep a usable local preview active.

## Decision
Use one transient native quick-controls HWND containing media/system cards, backed by existing
status/audio services and bounded MTA radio/SMTC/internal-brightness adapters. Each action needs
explicit input. Unknown stays unknown; unsupported global desktop toggles route to Windows Settings.
The UI emits intents, never performs blocking discovery/device work. Isolated app tests cannot reach
real actions. Permission completions and pending controls are fenced by panel lifetime/generation;
closing cancels pending controls, shutdown cancels/joins workers before destruction.

For maximized windows, reserve two twelve-DIP side strips through temporary documented AppBar leases.
Infrastructure HWNDs are alpha-zero, click-through, nonactivating tool windows, not visible desktop
surfaces. Respect Shell-approved allocations, coalesce notifications, release both leases on failure,
disable/reveal/quit, and re-register after Explorer restart. Do not write SPI_SETWORKAREA or move any
foreign application HWND. Normal manually positioned windows remain under user control. Windows
uses the work area for other standard placement such as snapping too; this is not a window manager.

Dock/Bar material retains backdrop blur, tint, grain and uniform border with centered shadow; it
adds no directional sunlight. High contrast and transparency-off stay opaque. Popup styling remains
independently defined. No Windows Settings takeover, Explorer replacement, registry startup change
or new third-party dependency is authorized. D25 still forbids push/tag/release.

## Qualification
Pure provider/geometry and fake own-window tests precede separate bounded desktop checks of exact
work area, own maximized test-window frame and restoration. Automated tests must never change real
radios/audio/brightness/media/startup or capture owner windows. Real-control manual qualification,
physical multi-monitor/DPI and accessibility gaps remain explicit in TEST_STATUS/HANDOFF.

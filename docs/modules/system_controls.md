# System controls adapter

`system_controls` exposes Result-based real radio state and pure Windows action mappings for FR-026.
Query/mutation run on an MTA worker with deadlines/cancellation. UI STA consent is an explicit user
radio action and completes asynchronously through a shutdown-safe dispatcher gate. Session permission
is reused; policy refusal clears it. Partial device failures require refresh even after an error.

Radio names/addresses/SSIDs/profile names are not read. Mixed/unknown/hardware-blocked radios cannot
be toggled optimistically. Unsupported global toggles are honest fixed Windows Settings routes;
energy-saver state, when available, is read from SYSTEM_POWER_STATUS. Captions is build gated.

Automated tests use pure mappings and invalid/pre-cancelled early exits only; no actual radio/state
change, Settings launch or input injection. Current execution receipts: TEST_STATUS S-026.

D44 additions (T41, 2026-10-26): `QuerySystemControls` also reads Sticky Keys (`SPI_GETSTICKYKEYS`,
unknown unless Windows reports the feature as available) and whether any screen reader is running
(`SPI_GETSCREENREADER`, read only). `SetStickyKeys` runs on the worker only after an explicit user
action, saves the documented flag to the profile with a settings broadcast and succeeds only when a
reread confirms the requested state. `ActionFor(AccessibilityFeature)` maps Magnifier, Narrator,
On-screen keyboard and Live captions (build 22621+) to their documented Windows shortcuts and the
accessibility page to `ms-settings:easeofaccess`. Unit tests cover the pure mappings and the Sticky
Keys reading; no automated test changes an accessibility setting.

D45 additions (T48, 2026-10-26): `SystemShortcut` gains `NotificationCenter` (Win+N),
`ActionCenter` (Win+A) and `Calendar` (Win+Alt+D). `NotificationsAction(build)` returns the
Windows 11 notification center/calendar for build 22000+, Action Center for Windows 10 (10240+)
and `Unsupported` below; `CalendarAction(build)` returns the calendar chord for 10240+. The app
sends every chord through one guarded keyboard path; nothing reads, clears or counts notifications,
and Do Not Disturb/Focus are never toggled. Unit tests cover the version mapping only.

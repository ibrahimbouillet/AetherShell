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

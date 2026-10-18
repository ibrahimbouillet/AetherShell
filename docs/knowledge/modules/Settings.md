# Settings — implemented service and native editor

Requirements: [FR-014–FR-016](../../../PROJECT_SPEC.md). Tasks T5f/T8/T10;
[executed scope S-007/S-008/S-011](../TEST_STATUS.md) and [implementation reference](../../modules/settings.md).

`src/settings` implements schema-v1 JSON, validation/clamping, migration, exact-byte newer-schema
preservation, corruption recovery, bounded backups and atomic saves. The UI owns the model and
notifications; a serialized worker owns disk operations. Update acceptance and durable completion
are separate. Revision-aware dirty state and `FlushAsync` preserve save ordering. ADR 0007 resolves
GAP-007 without allowing UI-thread disk access.

The configurable search/focus-Dock chords have validated main-key/modifier fields. Native editors
cover all fifteen nonzero modifier combinations. Startup is consent-only: automatic saves exclude
the checkbox; Apply authorizes the worker seam. Task Manager's disabled state is respected where
its known format is supported. Real Run/Task Manager behavior remains unqualified.

`src/settings/ui` provides Dock, Top Bar, Weather, Updates, General and About pages. Tests cover
live debounce, model merging, pending startup/reset ordering, close readiness, MIT text, fonts and
scrollable constrained viewports. The current Release service passed 33 unit cases /WX; the final native editor
passed 5 cases /WX using temporary files and injected startup callbacks. Those checks resolve the
scoped GAP-008 model/editor discrepancy; actual hotkey conflicts, physical DPI, screen-reader,
weather and update workflows still require qualification.

The isolated app lifecycle/shutdown case also passes. Real startup and complete product durability
qualification remain separate. Runtime settings live under
`%LOCALAPPDATA%\AetherShell\config\settings.json`; test data uses isolated temporary roots.

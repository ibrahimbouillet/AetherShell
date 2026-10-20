# Media and brightness adapter

`media_controls` provides worker-only bounded SMTC and internal-display WMI adapters for FR-026.
Constructors perform no discovery. Call serially on an MTA worker, cancel/join before destruction.
SMTC sessions have opaque provider-local live keys, capability checks and cancellable async actions.
Selection changes only Aether's local target. Titles/artist/device identity are never logged/persisted.

Brightness is available only for a confidently identified single active internal display. Enumeration,
method call and readback have deadlines; topology is checked before and after writes. External or
ambiguous displays stay unavailable and the UI offers Display Settings. A successful setter returns
fresh observed state; it does not assert that hardware applied exactly the requested percentage.

Safe automated cases reject invalid values/deadlines and pre-cancelled operations before discovery;
no actual media/brightness operation is executed. Runtime qualification remains separate (S-026).

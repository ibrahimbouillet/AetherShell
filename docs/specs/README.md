# Module work orders

Each file is an existing implementation work order with detailed algorithms, fixtures and design
guidance. Product objectives/acceptance are owned by [PROJECT_SPEC.md](../../PROJECT_SPEC.md), shared
rules by [AGENTS.md](../../AGENTS.md), progress by [TASKS.md](../../TASKS.md). Work-order examples are
not automatically owner-approved product changes; names of APIs must be checked against headers.
If a work order contradicts a protected requirement or the source's actual state, record the gap
and resolve it explicitly; do not silently change the requirement or claim an absent dependency exists.

Traceability: status → FR-005–FR-008; status_audio → FR-009; weather → FR-010; applications → FR-003;
search → FR-012; settings → FR-014/FR-016; accessibility → NFR-007; branding → FR-022; dock →
FR-001–FR-004; topbar → FR-001/FR-005–FR-011; search_ui → FR-013; settings_ui → FR-015/FR-019;
net/updates → FR-018; install/installer → FR-020/FR-021. All follow NFR-004/NFR-008/NFR-010.

Phase A (services): net, updates, applications, status, status_audio, weather, settings, search,
accessibility, branding. Phase B (surfaces and distribution): dock, topbar, search_ui, settings_ui,
install (library + launcher + updater), installer.

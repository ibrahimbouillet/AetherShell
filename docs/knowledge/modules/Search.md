# Search — implemented service and transient native window

Requirements: [FR-012/FR-013](../../../PROJECT_SPEC.md). Tasks T5g/T8/T10;
[executed scope S-007/S-008/S-011](../TEST_STATUS.md), [service reference](../../modules/search.md)
and [window reference](../../modules/search_ui.md).

`src/search` normalizes case/accents, ranks application identities deterministically and maps
highlights back to original text. An immutable index remains queryable while cancellable worker
rebuilds publish through the UI dispatcher. Typed text is never executed. Current Release /WX
evidence is 7 cases, including cancellation and a closed dispatcher. Synthetic p50/p95 is
1.409/1.962 ms for 5000 apps and 0.512/0.601 ms for 2000 apps. Initial Debug evidence was 5 cases
at 6.238/8.747 ms for 5000 apps; global release budgets remain Q-001.

`src/search/ui` owns native EDIT text/selection/caret/IME semantics and themed DirectWrite rendering,
debounced results, keyboard navigation, visible launch errors and short-viewport scrolling. Result
UIA IDs stay stable across filtering; offscreen rows report empty bounds. A repeated Open preserves
the remembered foreground restore target.

Five current Release integration cases passed /WX with fake launches and guarded synthetic dark/light
captures inspected. A separate MTA client checked the native edit's accessible name, Value pattern
and focusable property. This does not prove actual IME composition, successful foreground restoration
under Windows policy or end-to-end screen-reader interaction. Finite GPU entrance/exit transitions
and logical-close input guards pass their regression. No UI idle frame loop is used for transitions.
The isolated rerun passed after a foreground/capture-environment refusal; guards were retained.
Physical DPI/Windows 10 qualification remains pending.

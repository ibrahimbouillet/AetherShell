# ADR 0006 — Test framework

- Status: accepted (2026-10-17)

## Decision

Use a small in-house framework (`tests/framework/AetherTest.h`, ~250 lines) with CTest as the runner
instead of GoogleTest/Catch2/doctest.

- No third-party download at configure time (builds work offline and stay reproducible), no extra
  licence to track.
- Each module produces up to three executables registered with CTest and labelled `unit`,
  `integration` (needs an interactive Windows desktop) or `network` (needs Internet access).
- CTest provides JUnit XML for CI (`--output-junit`), timeouts and label filtering.

## Consequences

- Fewer conveniences (no parameterized tests, no matchers). Acceptable for this codebase size;
  migrating to a mainstream framework later is mechanical because test bodies use plain macros.
- Mocked tests are never treated as proof that real installation or real system integration works;
  those are covered by integration tests and the manual test plan in `tests/manual/`.

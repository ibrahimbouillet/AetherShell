# ADR 0008: owned status WinRT runtime and shutdown

Status: accepted implementation design, 2026-10-18. Requirements: NFR-005, FR-005–FR-008.

Provider sessions and worker callbacks share implementation/session ownership. Lifecycle locks protect
selection only; they are released before callbacks, close or joins. Stop joins retired sessions and
Start refuses a new generation while stopping. This permits callback-side destruction and concurrent
Stop without a cycle through the host mutex.

Each provider thread balances its own COM initialization. Cached C++/WinRT factories can outlive an
individual provider, so the module owns an MTA runtime cookie with process lifetime and explicit
terminal cleanup. `FinalizeStatusRuntime()` refuses active provider scopes, clears factories, releases
the cookie once and prevents future provider starts. The application calls it after all providers,
workers and graphics have shut down; static RAII is a fallback for process termination.

An attempted per-provider final MTA teardown crashed during real read-only integration and was
rejected. The final fresh /WX gate passes49unit,10read-onlyintegration and1isolatedterminal-shutdown
cases. Source independently reviewed. This is resource ownership with a defined shutdown operation,
not a waiver of the no-leak requirement. Windows10 qualification remains separate.

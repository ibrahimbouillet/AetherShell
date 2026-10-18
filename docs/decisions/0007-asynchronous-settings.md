# ADR 0007 — UI-owned settings with serialized asynchronous persistence

- Status: accepted implementation design (2026-10-18); qualification is tracked by T5f.
- Requirements: FR-014, FR-016, NFR-002, NFR-005; resolves the design conflict GAP-007.

The legacy settings work order combined a UI-thread-only model with synchronous file writes.
The shared responsiveness requirement takes precedence. SettingsService keeps validated Current
and Changed notifications on the UI thread, while one private WorkerPool serializes Load, Save,
Reset and Flush disk work. Completions return through IUiDispatcher.

Update applies validated state and announces it immediately; its return reports validation and
queue acceptance. A separate completion reports durable persistence. Failed writes retain dirty
memory; a later Update retries. Flush is a FIFO barrier that reports an earlier save failure rather
than silently treating it as durable. Revisions prevent an older completion from declaring newer
state clean. Migration/newer-schema/corruption backups precede overwrites on the same worker.
Shutdown suppresses queued callbacks; the app requests FlushAsync before orderly shutdown.

Blocking UI writes were rejected because they violate NFR-002. Blocking UI until a worker finishes
was rejected for the same reason. This changes an unimplemented service API, not released data.
Schema-v1 existing fields/defaults are retained. Configurable hotkey chords extend the model with
Ctrl+Alt+Space and Ctrl+Alt+D defaults, validation and duplicate-chord handling (GAP-008).

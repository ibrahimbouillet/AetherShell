<!-- Generated from the session's agent workflow; edit freely. -->
# Module specification: settings (phase A)

Read `AGENTS.md` first; it states the rules every implementer follows. This is the work order that
was given to the implementing engineer. The real headers in the repository are authoritative where
they differ from names used here.

Requirements: PROJECT_SPEC FR-014/FR-016, NFR-002/NFR-005. **GAP-007:** the legacy UI-thread-only
Load/Update disk wording below must not override the shared no-UI-thread-disk invariant. Before
implementing the service, record worker persistence and UI-owned state/notification ordering,
including write-failure/retry semantics. **GAP-008:** ADR 0003's configurable hotkeys are not modeled
by Settings.h's enabled flag; propose the model/editor coverage in a reviewed decision, preserving
the requirement rather than silently removing it.

Module: settings (directory src/settings, target aether_settings; deps: aether_core aether_diagnostics aether_platform advapi32).
Your files: the .h/.cpp files directly in src/settings/ EXCEPT src/settings/Settings.h (model contract owned by the lead; read it; propose changes in followUps), src/settings/CMakeLists.txt (NOT src/settings/ui/**), tests/unit/settings/**, docs/modules/settings.md.
1. SettingsSerialization: ToJson(const Settings&) -> aether::json::Value with stable camelCase keys and enums as lower-case strings ("system", "hours12", ...); FromJson(const Value&) -> Settings plus vector<std::string> warnings: unknown keys ignored with a warning, wrong types fall back to defaults with a warning, out-of-range values are clamped with a warning; pinned apps validated (max 64 entries, strings max 1024 characters, id required, duplicates removed). Never throws.
2. Validate(Settings&) -> warnings, clamping the ranges documented in Settings.h.
3. ConfigurationMigrator: chain of migrations by schemaVersion (missing schemaVersion treated as v0 -> v1). A newer schema than supported: load what is understood, keep the service read-only until the user changes something, and back up the newer file to settings.v<N>.backup.json before ever overwriting it, so a binary rollback never destroys newer settings. Document binary rollback versus user-data rollback.
4. SettingsService(path): Load(): missing file -> defaults (do not write until a change); unreadable or corrupt JSON -> rename to settings.json.corrupt-<UTC timestamp> (keep at most 3), use defaults, report it; size limit 1 MB. Expose a LoadReport {status Loaded|Defaults|RecoveredFromCorruption|NewerSchema, warnings}. Current() (UI thread only). Update(mutator): validate, serialize pretty JSON, write atomically with aether::fs::WriteFileAtomic, then notify subscribers (aether::Signal); if the write fails keep the in-memory change, return the error, and retry on the next Update (document). ResetToDefaults() keeps a settings.json.bak backup. UI-thread-only by design (debug assertion on the thread id).
5. StartupRegistration: per-user "Start with Windows" through HKCU\Software\Microsoft\Windows\CurrentVersion\Run value "AetherShell" = ""<launcher path>" --autostart". IsEnabled(expectedPath), Enable(path) (absolute existing .exe, quoted), Disable(). Called only after explicit user consent. Detect when the user disabled it in Task Manager (HKCU\...\Explorer\StartupApproved\Run binary value whose first byte is 0x03) and report DisabledByUser without ever overriding it. Make the registry key paths injectable so tests use HKCU\Software\AetherShellTests\... and never touch the real Run key (delete the test key at the end).
Tests: default round trip; every enum string; clamping; wrong types; unknown keys; pinned validation; corrupt file recovery and the backup cap; newer schema handling and backup; missing file causes no write; atomic writes leave no temp files; Update notifies; StartupRegistration with the injected key (enable, disable, disabled-by-user detection, quoting paths with spaces).

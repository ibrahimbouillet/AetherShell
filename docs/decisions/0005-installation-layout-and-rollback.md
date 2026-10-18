# ADR 0005 — Installation layout, update transaction and rollback

- Status: accepted (2026-10-17)

## Installer technology

A **custom native per-user installer** (`AetherShell-<version>-win-<arch>-setup.exe`, C++/Win32)
was chosen over WiX (MSI) and NSIS:

- The update system needs a side-by-side, versioned layout with a transactional switch and health
  check. MSI major upgrades remove the old version, and NSIS scripts would duplicate the logic the
  updater already needs. A native installer shares one C++ library (`src/install`) with the
  updater, so install, upgrade, repair, update and rollback follow the same tested code paths.
- No extra build tools are required (WiX would also need the .NET tool and, in recent versions, an
  EULA acceptance), and the setup UI can use the product's branding.
- Trade-offs: no MSI features (Group Policy deployment, MSI repair). Enterprise deployment is out
  of scope for Version 1.

The app payload is embedded in the setup executable as `RCDATA` resources together with a payload
manifest (file names, sizes, SHA-256). Because the whole setup executable is covered by the signed
release manifest (ADR 0004), the embedded payload manifest is authenticated too. The updater reads
the payload with `LoadLibraryEx(LOAD_LIBRARY_AS_DATAFILE | LOAD_LIBRARY_AS_IMAGE_RESOURCE)`, so a
downloaded package is never executed to apply an update.

## Layout (per user, no elevation)

```
%LOCALAPPDATA%\Programs\AetherShell\        install root (configurable at first install)
  AetherShell.exe                           launcher: stable path for shortcuts and the Run key
  AetherShellSetup.exe                      maintenance copy of the installer (repair/uninstall)
  install-state.json                        transaction journal (schema v1, written atomically)
  versions\<semver>\                        one complete application per version
     AetherShellApp.exe  AetherShellUpdater.exe  LICENSE  THIRD_PARTY_NOTICES.md  payload-manifest.json
  staging\<semver>.<id>\                    extraction area on the same volume (atomic rename)
```

User data never lives in the install root: settings, logs, caches and downloads are under
`%LOCALAPPDATA%\AetherShell` (see `platform/windows/AppPaths.h`).

## Update transaction

States recorded in `install-state.json` before each step:

`Prepared → Verified → Staged → Applying → Installed → HealthCheckPending → Committed`
with failure branches `RollbackRequired → RolledBack`.

1. **Prepared** — the running app hands the updater the downloaded package, manifest and signature.
2. **Verified** — the updater independently re-verifies the manifest signature against the
   compiled-in key, the package size and SHA-256, the architecture and the minimum Windows build.
3. **Staged** — payload files are extracted to `staging\` and each file is checked against the
   payload manifest; disk space is checked first.
4. **Applying** — the running app is asked to exit through the instance channel and the updater
   waits for the process to end (no forced termination of a healthy app; on timeout the update is
   abandoned and the current version keeps running). The staged folder is renamed into `versions\`.
5. **Installed** — `activeVersion` switches to the new version, `previousVersion` keeps the old one.
6. **HealthCheckPending** — the launcher starts the new version with a one-time health token; the app
   signals a named event once its Dock and Status Bar are up. The updater waits a bounded time.
7. **Committed** — older versions beyond `previousVersion` are deleted, the maintenance setup copy is
   refreshed.
8. **RollbackRequired / RolledBack** — on a missing health signal or a crash, `activeVersion` returns to
   the previous version, which is started again; the failed version folder is removed.

The previous working version is never deleted before its replacement is committed.

## Interrupted updates (power loss, crash, cancellation)

The launcher (and the updater) read the journal at start:
- Prepared/Verified/Staged/Applying: the old version was never switched off → delete staging/partial
  folders, clear the transaction, keep running the old version.
- Installed/HealthCheckPending without a live updater: roll back to `previousVersion` (safe default)
  and record `RolledBack`; the app tells the user the update was not completed.

## Binary rollback vs user data

Rolling back binaries does not roll back settings. `SettingsService` keeps newer-schema files intact
(backup before any overwrite), so a rolled-back version can still start with defaults for unknown
fields without destroying the user's newer configuration.

## Uninstall and repair

- Uninstall (`AetherShellSetup.exe --uninstall`, registered under
  `HKCU\Software\Microsoft\Windows\CurrentVersion\Uninstall\AetherShell`): stops the app through the
  instance channel, removes the Run entry, shortcuts, uninstall key and the install root, and keeps or
  removes `%LOCALAPPDATA%\AetherShell` according to an explicit choice. Because a running executable
  cannot delete itself, the maintenance copy re-launches from a temporary copy (left in `%TEMP%`).
- Repair (`--repair`) re-extracts and re-verifies the active version, restores the launcher, the
  shortcuts and the registration, and never touches user settings.

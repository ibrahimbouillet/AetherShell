# Updates and rollback

The app queries only the official `ibrahimbouillet/AetherShell` GitHub Releases endpoint. Downloaded
release notes are plain text. Automatic checks can be disabled and do not download/install without
a user action. Stable and prerelease channels are separate.

An update is accepted only after exact-byte ECDSA P-256 manifest verification with the compiled
public trust anchor, followed by the installer's size and SHA-256 checks. Never distribute an
unsigned manifest as an update. Authenticode and manifest signatures serve different purposes;
the manifest signature is required even while the alpha lacks an Authenticode certificate.

The updater stages payloads as data, verifies each embedded file and requests graceful app shutdown.
It activates a versioned folder, starts the new app with a one-use health token, and commits after
health is signalled. On health failure it stops only its own unhealthy child, restores the previous
version and starts that version. An interrupted journal is recovered before launching normally.
A working installation is retained until its replacement is committed.

Keep at most the current and previous committed version. Settings/user data live outside the
program directory. Repair and uninstall are available through the installed maintenance setup.
See [test evidence](docs/knowledge/TEST_STATUS.md) for the scenarios actually exercised.

The first public alpha's live signed-release path remains dependent on owner-controlled signing.
No private key, credential or key location belongs in this repository. There is no signature bypass
in the production app/updater. If no authenticated release is available, manual installation is the
only available distribution path.

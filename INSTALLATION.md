# Installation

Use only the setup executable from the [official repository's Releases page](https://github.com/ibrahimbouillet/AetherShell/releases).
The second public alpha, [0.2.0-alpha.2](https://github.com/ibrahimbouillet/AetherShell/releases/tag/v0.2.0-alpha.2), is a manual-install preview; wider Version 1 qualification remains open.

Run setup as your ordinary Windows user. The wizard explains the product, displays the MIT license,
and lets you choose a folder (default `%LOCALAPPDATA%\Programs\AetherShell`), shortcuts and optional
startup. It installs a stable launcher, a maintenance setup and versioned program files. It can
create a Start Menu shortcut and a per-user entry in Windows Installed apps. No administrator
rights or Explorer replacement are used. On a fresh interactive Alpha 2 installation, **Start at
sign-in** is visibly selected: clear it to opt out before installing. Accepting setup saves that
choice; an existing saved choice and Task Manager disable are respected. Silent setup enables
startup only with the explicit `--start-with-windows` flag; `--no-start-with-windows` selects off.
Portable profiles remain off until consent. Windows controls sign-in launch order and can delay it.

The V2 shell visually replaces the Windows taskbar by default when Aether Shell starts. The setup
discloses this behavior; the app keeps a recovery helper and a **Settings → Desktop** opt-out. A
normal exit restores the Windows taskbar. Explorer remains running, and Windows Settings continues
to configure Windows rather than Aether Shell. Recovery limitations and emergency commands are in
the [user guide](USER_GUIDE.md).

Unsigned application/setup binaries can trigger SmartScreen. This alpha has no Authenticode
publisher certificate. Check the exact release source and SHA-256 before deciding to run it:

```powershell
Get-FileHash .\AetherShell-0.2.0-alpha.2-win-x64-setup.exe -Algorithm SHA256
```

Compare with the corresponding checksums asset. A checksum alone does not authenticate a publisher.
Automatic updates additionally require the signed release manifest described in
[UPDATE_AND_ROLLBACK](UPDATE_AND_ROLLBACK.md).

Use Installed apps → Aether Shell → Modify to repair the active version with its matching setup.
Uninstall requests graceful app shutdown and removes only the installation's registered entries
and files. Keep user data is the default; explicitly select removal to delete settings/cache/logs.
An older setup is rejected unless downgrade was explicitly permitted. Preserve a backup of user
data before any intentional downgrade.

Silent commands below use the maintenance executable inside the installation folder; the downloaded
setup filename includes the version and architecture. Use absolute custom paths:

```powershell
.\AetherShellSetup.exe --silent --no-launch
.\AetherShellSetup.exe --silent --repair --no-launch
.\AetherShellSetup.exe --silent --uninstall --keep-user-data
```

Options: `--install-root <folder>`, `--data-dir <folder>`, `--no-shortcuts`, `--no-launch`,
`--start-with-windows` (explicit consent), `--repair`, `--uninstall`, `--keep-user-data`,
`--no-start-with-windows` (explicit opt-out),
`--remove-user-data`, `--allow-downgrade`, `--log <file>`. Silent mode never prompts. Exit codes:
0 success, 1 operation failure, 2 invalid arguments/path setup failure. The normal GUI blocks closing
during the active operation; cancellation and self-uninstall qualification are tracked in
[KNOWN_LIMITATIONS](KNOWN_LIMITATIONS.md).

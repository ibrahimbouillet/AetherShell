# Native setup

`installer/`, `AetherShellSetup`. `GeneratePayload.cmake` runs after the real app/launcher/updater
executables build. It embeds those files, LICENSE and THIRD_PARTY_NOTICES as RCDATA and writes an
exact payload manifest with real file sizes/SHA-256. The install library opens resources as data.

The native per-user wizard has Welcome, full MIT License, Options/Browse/shortcut/startup consent,
indeterminate Progress and Finish/Error pages. All file work runs on a worker; cancellation is
observed while staging and before activation. Activation completes atomically after that boundary.
DPI rebuilds controls/fonts; native controls support keyboard navigation and Windows contrast.

CLI and exit codes are documented in INSTALLATION. Installed maintenance relocates an identical
copy to a unique temp directory, inherits only a parent synchronization handle, verifies the copy
and waits for the original image to exit before modifying/removing its root. Exit0 from the
original means handoff accepted; the child/log records final outcome. A temp executable can remain.

Tests compile a separate `AetherShellSetupTest`; its private isolation flag disables registration,
shortcuts, startup and application launch and injects no-op process operations. This flag is absent
from the production binary. Initial /WX test pass:3cases in `build/wf-app2` cover silent temporary
installation/payload hashes/repair/uninstall keeping data/explicit data removal/malformed arguments.
The settled complete Release build passed all five setup cases, adding installed-maintenance
relocation and an isolated 200% DPI/scroll/focus viewport case. Production registration was observed
separately during owner-authorized alpha delivery: exit 0, Installed apps entry and launcher present,
startup disabled and no app launch. This delivery observation is not an automated mutation test.

**Taskbar recovery (D21).** When no Aether Shell instance is running, repair and uninstall call
`platform::RecoverTaskbars` on `<data>\config\taskbar-baseline.json` before the package operation.
A running instance and its helper restore the taskbar themselves when the operation asks them to exit.
Only an existing baseline triggers recovery, so settings, other installations and automated tests
(temporary data roots) are unaffected. A failure does not block repair or uninstall; the setup log
records it with the manual Explorer-restart instruction.

**Default-on replacement (D23).** The Welcome page says that the Dock replaces the Windows taskbar
while Aether Shell runs and how to get it back (quit, Settings > Desktop, or the Start menu entry
"Restore Windows taskbar"). The Options page repeats it next to the startup choice, which defaults
checked for a fresh interactive installation (D34), with a visible opt-out before Install. Existing
settings and Task Manager disables take precedence. Silent setup preserves startup unless
`--start-with-windows` or `--no-start-with-windows` is supplied. Setup does not override an existing replacement
choice. Saving startup consent can create settings with the replacement default, while repair
preserves an existing explicit opt-out.

Opening or cancelling the wizard only reads the bounded settings JSON; it never invokes settings
recovery or prunes backups. An accepted startup opt-out removes only this installation's Run target.
The StartupApproved value stays read-only, and unsupported approval data is preserved. Accepted
startup choices are saved in settings; a failed save restores the previous Run entry. Test-mode
setup bypasses real startup registry inspection as well as writes and application launches.

Normal setup/Finish launches use the stable launcher so the installed session is supervised. The
updater's transaction health-check child remains a directly owned process, as documented in
[Installation, launcher and updater](install.md).

**"Restore Windows taskbar" shortcut.** With Start menu shortcuts selected (the default), install
and repair create a per-user `Restore Windows taskbar.lnk` in the Start menu Programs folder. It
runs the launcher with `--restore-taskbar`, which the launcher forwards to the active version: a
running instance turns replacement off, otherwise the D21 recovery restores the taskbar and writes
the setting off. A file of the same name that does not target this installation's launcher is never
replaced or removed. Uninstall removes the shortcut. A shortcut failure after a committed operation
is only noted in the setup log. The test executable accepts `--test-shortcut-dir <absolute folder>`
together with its isolation flag, so the integration suite checks creation (target and argument),
the foreign-file rule and removal on uninstall in a temporary folder; the user's Start menu is never
touched by tests, and the production binary has no such switch.

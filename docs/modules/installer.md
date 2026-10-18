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

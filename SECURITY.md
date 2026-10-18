# Security policy

## Reporting a vulnerability

Please report vulnerabilities **privately** through GitHub's
[security advisory form](https://github.com/ibrahimbouillet/AetherShell/security/advisories/new).
Do not open a public issue. You can expect an acknowledgement within 7 days.

Supported versions: only the latest published release (and the current `main` branch) receive
security fixes while the project is in its 0.x development phase.

## Security design summary

- **No elevation.** Aether Shell installs and runs per user (`asInvoker`). It never asks for
  administrator rights, never replaces `explorer.exe`, never edits the Winlogon `Shell` value and
  never disables Windows security features.
- **Updates** come only from GitHub Releases of `ibrahimbouillet/AetherShell`:
  - the release manifest is signed with an ECDSA P-256 key whose public half is compiled into the
    application; the private key is kept offline by the maintainer and is only used by the release
    pipeline through a protected CI secret;
  - every downloaded asset must match the size and SHA-256 listed in the signed manifest — a checksum
    published next to an asset is never accepted as proof of authenticity on its own;
  - downloads are HTTPS-only with a host allow-list for redirects and a size limit;
  - the updater never executes the downloaded package: it reads the embedded payload as data,
    verifies every file, installs side by side and keeps the previous version for rollback.
  Details: `docs/decisions/0004-update-trust-model.md`, `docs/decisions/0005-installation-layout-and-rollback.md`.
- **Authenticode**: the project does not own a code-signing certificate yet. Until it does, Windows
  SmartScreen may warn about the installer; the signed manifest remains the authenticity mechanism
  for updates.
- **Untrusted input**: release notes, weather responses and GitHub API data are parsed with size and
  depth limits and rendered as plain text only. Search never executes typed text, and applications
  are launched only through the Windows shell from discovered entries, never from constructed command
  lines.
- **Privacy**: no telemetry. Network access is limited to GitHub (update checks, can be disabled) and
  Open-Meteo (weather, disabled by default). Logs avoid personal data and stay on the device.

## Handling secrets in this repository

Never commit credentials, tokens or private keys. `.gitignore` excludes common key formats; the
release signing key must only exist in the maintainer's offline storage and in the CI secret
`AETHER_RELEASE_SIGNING_KEY`.

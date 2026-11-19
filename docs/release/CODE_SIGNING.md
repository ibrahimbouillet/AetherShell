# Code signing (Authenticode) and Windows SmartScreen

Owner, 2026-11-19: Windows warned about the setup "as if it were a virus", which would frighten people. Audit item
A04: the setup carries no publisher signature.

## Why Windows warns

A file downloaded with a browser is marked as coming from the Internet. When it starts, Microsoft Defender
SmartScreen looks at two things:

- **Who published it.** An Authenticode signature names the publisher and proves the file was not changed after
  signing. Without one, Windows shows "Unknown publisher" and "Windows protected your PC".
- **Its reputation.** SmartScreen builds a reputation for each signing identity (and, unsigned, for each exact
  file) from how many people run it without trouble. A new identity or a new unsigned build starts with none.

Aether Shell's setup installs for the current account and never asks for administrator rights, so there is no UAC
prompt: SmartScreen is the only warning. It cannot and must not be bypassed. Removing the Internet mark, packing the
setup to hide it, or any other trick is evasion of a security feature: never part of this project. The legitimate
way is to sign every program with a real identity and let that identity earn its reputation.

Until a release is signed, the person can still run it: **More info** > **Run anyway**. INSTALLATION.md says so.

## What the build does now

`scripts/package.ps1 -Sign` signs, in this order:

1. the application, the launcher and the updater, as copies, **before** setup embeds them
   (`installer/GeneratePayload.cmake`, CMake option `AETHER_CODE_SIGN`): the payload manifest pins the signed bytes,
   so setup's own SHA-256 checks keep working, and the installed `AetherShell.exe` is signed too;
2. the setup itself, after the privacy audit and before the release manifest and checksums, which therefore pin the
   signed setup.

Signing is `scripts/release/sign_code.ps1` around Windows SDK `signtool` (SHA-256 digest, RFC 3161 time stamp, so a
signature outlives its certificate). It verifies each signature against Windows' own trust (`signtool verify /pa`)
and fails the package otherwise. It never creates, imports or trusts a certificate and never changes a certificate
store: the identity is the maintainer's and is configured privately through the environment:

| `AETHER_SIGN_MODE` | Settings | For |
|---|---|---|
| `certificate` | `AETHER_SIGN_CERT_THUMBPRINT` | a certificate in the signer's own store: a hardware token, Certum SimplySign, a cloud HSM's provider |
| `pfx` | `AETHER_SIGN_PFX`, `AETHER_SIGN_PFX_PASSWORD` | a key in a file (no longer issued for public code signing; local tests) |
| `artifact-signing` | `AETHER_SIGN_DLIB`, `AETHER_SIGN_METADATA` | Azure Artifact Signing (formerly Trusted Signing) |

`AETHER_SIGN_TIMESTAMP_URL` overrides the time stamp server; `AETHER_SIGNTOOL` the signtool path. Never commit or
print these values. `-Sign -TestCertificate` accepts a local test certificate (Windows does not trust it): it proves
the mechanics only and such a package is never published.

Without `-Sign` the package says `UNSIGNED PROGRAMS` and SmartScreen will warn.

## Getting an identity (the owner's choice)

All public code-signing keys must now live in hardware or a cloud HSM, and the issuer verifies the person or
organisation first. The options, cheapest first (check current prices and eligibility on each provider's site):

1. **SignPath Foundation** (free for open-source projects). The program is signed by SignPath's own certificate
   from a GitHub Actions build of the public repository; the publisher shown is "SignPath Foundation" rather than
   the owner's name. Needs an OSI licence (MIT: yes), an application and a CI build that SignPath can verify.
2. **Certum Open Source Code Signing** (low yearly price). An individual's certificate in the owner's name, with
   the key in Certum's SimplySign cloud or on a card; signs locally with `AETHER_SIGN_MODE=certificate`.
3. **Azure Artifact Signing** (monthly subscription). Microsoft's managed signing, short-lived certificates chained
   to Microsoft's root; identity validation is available for individuals only in some countries. Works from a
   local build or from GitHub Actions with `AETHER_SIGN_MODE=artifact-signing`.
4. **A commercial OV certificate** (DigiCert, Sectigo, GlobalSign...): the owner's or a company's name, on a token.
   Since 2024 an EV certificate no longer skips SmartScreen's reputation either.

With any of them, the first signed releases may still show a SmartScreen notice for a while: reputation follows
downloads of that identity, and signed builds keep it across versions (an unsigned build starts from zero each
time). Publishing to **winget** or the **Microsoft Store** later adds an installation path people already trust;
it does not replace the signature.

## Checking a release

On the file people actually download (`dist/AetherShell-<version>-win-x64-setup.exe`):

```powershell
Get-AuthenticodeSignature .\AetherShell-<version>-win-x64-setup.exe | Format-List Status, SignerCertificate, TimeStamperCertificate
```

`Status` must be `Valid`, the signer the expected identity, and a time stamp present. The release notes say whether
a release is signed; a signed update manifest (ECDSA, docs/decisions/0004-update-trust-model.md) is a separate
mechanism and stays required for automatic updates.

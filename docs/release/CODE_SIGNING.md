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

## Releases signed through SignPath (the owner's choice, 2026-11-19)

**Status: not accepted yet.** On 2026-11-20 SignPath Foundation answered that the project does not yet show enough
public visibility (recognition, users, community) and invited a new application once it has gained broader
recognition; it also offers SignPath's paid subscription. The owner's choice (2026-11-21): no certificate for now,
neither paid nor Certum; make the project better known, then apply again. Until then releases are unsigned and the
README's policy carries no SignPath attribution. Everything below stays ready and has not run on GitHub.

The owner chose **SignPath Foundation** (D88). SignPath signs only what GitHub Actions built from this public
repository, on GitHub's own runners, after the maintainer approves each request in SignPath. The certificate is
SignPath Foundation's, so Windows shows "SignPath Foundation" as the publisher; the README's
[code signing policy](../../README.md#code-signing-policy) says so, as SignPath Foundation requires.

**The workflow** (`.github/workflows/signed-release.yml`, started by hand from the Actions tab on `main`):

1. builds the Release application, launcher and updater (`AETHER_SOURCE_REVISION` = the commit);
2. uploads them as one artifact and asks SignPath to sign them (artifact configuration `programs`); the maintainer
   approves the request in SignPath, and the signed programs come back;
3. builds the setup around them with `AETHER_SIGNED_PROGRAMS_DIR`: `installer/GeneratePayload.cmake` runs
   `scripts/release/check_signed.ps1` on each, which requires the program this build made, byte for byte, plus a
   signature Windows trusts (only the CheckSum, the certificate table entry and the appended certificate may differ),
   and the payload manifest pins the signed bytes;
4. asks SignPath to sign the setup (artifact configuration `setup`), a second approval;
5. runs `scripts/package.ps1 -SkipBuild -SignedSetup ...`: the package privacy audit on the built setup, the same
   check on the signed one (which then replaces it), the manifest, the language packs and the checksums;
6. uploads the signed package as a workflow artifact for review. Publishing the GitHub release stays manual.

The artifact configurations are kept in `.signpath/artifact-configurations/` (`programs.xml`, `setup.xml`): both
require the product name "Aether Shell" and this release's version (the `version` parameter the workflow passes).

**What the owner does, once:**

1. Turn on two-factor authentication for the GitHub account (SignPath Foundation requires it for everyone with
   access to the repository and to SignPath).
2. Let the repository receive this work (`main` pushed, so the workflow, `.signpath/`, `PRIVACY.md` and the README's
   policy are public: SignPath reviews the repository).
3. Apply at [signpath.org/apply](https://signpath.org/apply) for the open-source programme: the repository
   `https://github.com/ibrahimbouillet/AetherShell`, MIT, built by GitHub Actions, a Windows desktop application with
   a per-user setup, already released on GitHub (Alpha 4). Acceptance is SignPath Foundation's decision and takes
   some days.
4. When accepted, in SignPath: link the predefined **GitHub.com** trusted build system to the project; create the
   artifact configurations `programs` and `setup` from the two XML files; check that the release signing policy names
   the owner as approver; create an API token for a user allowed to submit signing requests.
5. In GitHub, Settings > Secrets and variables > Actions: the secret `SIGNPATH_API_TOKEN` (that token) and the
   variables `SIGNPATH_ORGANIZATION_ID`, `SIGNPATH_PROJECT_SLUG` and `SIGNPATH_SIGNING_POLICY_SLUG` (as SignPath shows
   them). Never put these in the repository.

**Each release:** run *Signed release* on `main`, approve the two requests in SignPath, download the artifact, check
`Get-AuthenticodeSignature` (below), then publish the release from those files.

Locally, the same path can be rehearsed with a throwaway certificate standing in for SignPath (sign copies of the
built programs with `sign_code.ps1 -AllowUntrusted`, build with `AETHER_SIGNED_PROGRAMS_DIR` and
`AETHER_CODE_SIGN_ALLOW_UNTRUSTED=ON`, then `package.ps1 -SkipBuild -SignedSetup <signed setup> -TestCertificate`);
such a package is never published.

## Getting an identity (the owner's choice)

All public code-signing keys must now live in hardware or a cloud HSM, and the issuer verifies the person or
organisation first. The options, cheapest first (check current prices and eligibility on each provider's site):

1. **SignPath Foundation** (free for open-source projects; **chosen**, above; not accepted yet, 2026-11-20). The program is signed by SignPath's own certificate
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

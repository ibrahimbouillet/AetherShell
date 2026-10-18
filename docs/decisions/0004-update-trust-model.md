<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Ibrahim Bouillet -->
# ADR 0004 — Update trust model

- Status: accepted (2026-10-18)
- Deciders: Ibrahim Bouillet (owner), implementation by the engineering session
- Implemented in: `src/updates` (verification), `scripts/release` (publishing), `updater/` (re-verification, ADR 0005)

## Context

Aether Shell updates itself from the public GitHub repository `ibrahimbouillet/AetherShell`
(`aether::build::kUpdateRepositoryOwner` / `kUpdateRepositoryName`). Everything the application
receives from GitHub — release metadata, release notes, asset URLs, the files themselves — travels
over TLS from a service the project does not control, and the GitHub account itself can be
compromised. The update path must therefore answer two separate questions:

1. **Integrity**: are the downloaded bytes exactly the bytes that were published?
2. **Authenticity**: were they published by the project owner?

A SHA-256 checksum answers only the first question, and only if the checksum itself is trustworthy.
A checksum published next to the file it describes (for example `checksums.txt` in the same release)
proves nothing about who published either of them: whoever can replace the installer can replace
the checksum too.

The project has **no code-signing (Authenticode) certificate** yet, so Windows cannot vouch for the
publisher of the installer either.

## Decision

### Authenticity: a signed release manifest

Every release carries `AetherShell-<version>-manifest.json`, a small JSON document (schema v1,
`src/updates/UpdateManifest.h`) that binds the version to the exact file name, architecture, size
and SHA-256 of each installer, the minimum Windows build and the channel:

```json
{"schemaVersion":1,"product":"AetherShell","version":"1.2.0","channel":"stable",
 "minimumWindowsBuild":19041,
 "releaseNotesUrl":"https://github.com/ibrahimbouillet/AetherShell/releases/tag/v1.2.0",
 "assets":[{"filename":"AetherShell-1.2.0-win-x64-setup.exe","architecture":"x64",
            "format":"exe","sha256":"<64 lowercase hex>","size":12345678}]}
```

The manifest is signed with **ECDSA P-256 over SHA-256 of the exact manifest bytes**. The signature
is published as `AetherShell-<version>-manifest.json.sig`: base64 (RFC 4648, strict) of the 64-byte
IEEE P1363 form `r || s`, followed by one newline. A strict, minimal-encoding DER `ECDSA-Sig-Value`
is accepted as well (what `openssl dgst -sign` produces natively).

The application verifies the signature with **Windows CNG** (`BCRYPT_ECDSA_P256_ALGORITHM`, public
key imported as `BCRYPT_ECCPUBLIC_BLOB` / `BCRYPT_ECDSA_PUBLIC_P256_MAGIC`) against public keys
**compiled into the application** (`src/updates/TrustedKeys.h`). The production key is:

```
X||Y = 2b0310c62dd1d8ec4e6b16c9caf44fced9f8df43f3807bc9d3e425ca28f4e12f
       66b5ed190dad7d96f79405a82e7f5e60e5caa622a379bc19edfb42c92cb0ce02
```

Its private key is held **offline by the project owner** and never enters the repository, CI logs
or the engineering sessions (`*.pem` is git-ignored). Production code builds its verifier only from
`TrustedKeys.h` (`EcdsaP256Verifier::ForReleaseManifests()`); tests construct verifiers with their own
throw-away test keys through the constructor.

### Integrity: size and SHA-256 from the signed manifest

The installer is streamed to a `.partial` file while SHA-256 (CNG) is computed. The transfer is
aborted as soon as it exceeds the signed size; at the end size and digest must equal the signed
values, otherwise the partial file is deleted (`ErrorCode::Integrity`). Only then is the file
renamed atomically to its final name. A matching digest is meaningful **only because the digest
comes from a manifest whose signature verified**.

`AetherShell-<version>-checksums.txt` (sha256sum format) is published for people who verify
downloads by hand. The application never trusts it.

### Order of checks (fail closed)

1. GitHub metadata (`/releases/latest`, or `/releases?per_page=30` for the prerelease channel) is
   parsed strictly and treated as **untrusted**: it only says where to look. URLs must be under
   `https://github.com/<owner>/<repo>/` (`.../releases/download/` for assets); redirects are
   allowed only to `api.github.com` (API) and `github.com`, `objects.githubusercontent.com`,
   `release-assets.githubusercontent.com` (downloads); no token is ever sent.
2. Only a release newer than the running version (SemVer precedence) is considered — downgrades
   are never offered.
3. Manifest and signature are downloaded (64 KiB and 512 bytes at most) and the **signature is
   verified before any manifest field is used**.
4. The manifest is parsed strictly: exact product, unknown members rejected, version identical to
   the release tag version (prevents replaying an older signed manifest under a new tag), channel
   consistent with the release's prerelease flag, real digests (placeholders such as all zeros or
   `<release-generated-digest>` rejected), sizes in (0, 512 MiB], conventional file names without
   path components, known architectures, no duplicates.
5. Compatibility: architecture of the running build and the Windows build number
   (`ErrorCode::Unsupported` otherwise). The GitHub asset size must equal the signed size.
6. On user request: verified download (above), then the optional Authenticode policy, then
   `ReadyToInstall` with the package, the manifest bytes and the signature side by side.
7. The separate updater executable **re-verifies** signature, size, digest, architecture and
   Windows build before applying anything (ADR 0005). This module never executes the package.

### Authenticode: optional, honest default

`WinVerifyTrust` (`WINTRUST_ACTION_GENERIC_VERIFY_V2`, no UI, whole-chain revocation checks) is
available behind `AuthenticodePolicy`:

| Policy | Behaviour |
|---|---|
| `NotRequired` (**default**) | not checked |
| `RequiredIfPresent` | unsigned files pass; a present embedded signature must be valid |
| `Required` | the file must carry a valid embedded signature |

The default is `NotRequired` because **official installers are currently not Authenticode-signed**.
Consequences that are stated, not hidden: Windows SmartScreen may warn about a manually downloaded
installer, and authenticity of in-app updates rests entirely on the manifest signature. Even when a
certificate exists, Authenticode is defence in depth: the generic policy accepts any certificate that
chains to a trusted root (the publisher identity is not pinned), revocation checks may fail offline
(so `Required` fails closed without network), and only embedded signatures are examined.

### Key rotation

`TrustedKeys.h` holds a list; a manifest is accepted when **any** listed key verifies it (the
verifier reports which one). To rotate: generate the new key offline, add its public key to
`TrustedKeys.h`, ship a release **signed with the old key** that contains both keys, wait until all
supported versions trust the new key, then sign with the new key and remove the old entry in a
later release. `sign_manifest.py` refuses keys that are not listed in `TrustedKeys.h`, so a release
cannot be signed with a key the application would reject.

## Threats considered

| Threat | Outcome |
|---|---|
| Installer replaced on GitHub or in transit | Digest/size mismatch with the signed manifest → rejected |
| Installer **and** checksums replaced (compromised GitHub account) | Manifest signature fails → rejected |
| Old, validly signed manifest re-published under a newer tag | Version must equal the tag → rejected |
| Downgrade to an older signed release | Only versions newer than the running one are offered |
| Stable/prerelease channel confusion | Signed channel must match the release flag; prerelease versions never on stable |
| Redirect to a foreign host, oversized responses | Host allow-lists, final-URL check, bounded sizes |
| Malicious release notes (control characters, bidi overrides) | Untrusted text, sanitized, rendered as plain text only |
| Path traversal through asset names | Plain file names only; final path verified inside `<updates>\downloads` |
| **Freeze attack** (attacker hides newer releases) | **Not mitigated**: the manifest has no expiry; a compromised account can withhold updates |
| **Compromise of the offline signing key** | **Not mitigated** beyond rotation; keep the key offline |

## Alternatives considered

- **Checksums only** — rejected: proves integrity relative to an unauthenticated value, never who
  published it.
- **Authenticode only** — not possible yet (no certificate) and, with the generic policy, it would
  trust any publisher with any valid certificate.
- **GPG/minisign signatures** — would need a third-party parser; ECDSA P-256 is built into CNG and
  into the OpenSSL CLI used by the release scripts.
- **TUF (The Update Framework)** — addresses freeze and key compromise properly but is heavy for
  version 1; revisit if the project grows (expiring signed metadata is the first step).

## Consequences

- Releases require the offline key: `scripts/release/make_manifest.py` → `sign_manifest.py` →
  `make_checksums.py` (see `scripts/release/README.md`). The CI must get the key as a secret before
  signed releases can be automated.
- A release without a valid manifest and signature is never offered; it fails visibly with
  `ErrorCode::Validation` or `ErrorCode::Signature` instead of being installed.
- The trust anchor ships inside the binary: a compromised update channel cannot change it, but a key
  change always needs a transition release.

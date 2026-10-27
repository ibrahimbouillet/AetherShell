<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Ibrahim Bouillet -->
# Release scripts

Python 3.11+ standard library only, plus the OpenSSL command-line tool for signing.
The trust model is described in `docs/decisions/0004-update-trust-model.md`.

For a release `v<version>` with installers in `dist/`:

```powershell
# 1. Manifest with real SHA-256 digests and sizes (refuses placeholders and misnamed files).
python scripts/release/make_manifest.py --version 1.2.0 --channel stable `
    dist/AetherShell-1.2.0-win-x64-setup.exe dist/AetherShell-1.2.0-win-arm64-setup.exe

# 2. Signature over the exact manifest bytes with the offline release key.
#    The key never leaves the signing machine and is never printed.
# Set AETHER_RELEASE_SIGNING_KEY_FILE locally to the offline key; never record its value here.
python scripts/release/sign_manifest.py dist/AetherShell-1.2.0-manifest.json

# 3. sha256sum-compatible checksums for people who verify downloads by hand.
python scripts/release/make_checksums.py --version 1.2.0 dist/AetherShell-1.2.0-win-*-setup.exe `
    dist/AetherShell-1.2.0-manifest.json dist/AetherShell-1.2.0-manifest.json.sig
```

Upload the installers, `AetherShell-<version>-manifest.json`, `AetherShell-<version>-manifest.json.sig`
and `AetherShell-<version>-checksums.txt` to the GitHub release whose tag is `v<version>`. Mark the
release as a prerelease exactly when the manifest channel is `prerelease` (a version with prerelease
identifiers, such as `2.0.0-beta.1`, must use that channel; a plain version may use either). Do not
edit the manifest after signing: the application verifies its exact bytes.

For a manual-install alpha without the trusted offline signing key, `scripts/package.ps1` keeps the
unsigned manifest local and writes checksums for the setup executable only. Upload exactly the
versioned setup executable and its checksums file; never upload an unsigned manifest or use a broad
`dist/*` upload. This is the policy for `v0.2.0-alpha.2` and `v0.3.0-alpha.3`; automatic updates
remain unavailable.
A plain checksum checks integrity and does not authenticate the publisher. Before manifest generation,
packaging invalidates any same-version signature left in a reused output directory so a prior
signature cannot accompany newly generated unsigned bytes or survive a failed signing attempt.

Package from a fresh, tested Release `/W4 /WX` build of the intended revision. Confirm the generated
`kVersion` is the intended version, since an existing CMake cache can retain a prerelease override.
Use an empty version-specific output directory and review its exact file list. The packaging privacy
audit checks the setup and all uncompressed embedded app/launcher/updater payload bytes without
executing them, including source/PDB paths and source freshness. It is a scoped byte audit, not a
guarantee that arbitrary secrets or runtime behavior have been reviewed.

`make_manifest.py` applies the application's own rules to its inputs, so a manifest it writes is one
the application can accept: `--release-notes-url` must be a page of the repository on
`https://github.com/` without query string or fragment, and versions are limited so that every asset
name stays within 200 characters.

Avoid build metadata (`+...`) in published versions. GitHub documents that it renames uploaded asset
files whose names contain special or non-alphanumeric characters; if it rewrites the `+`, the
application no longer finds `AetherShell-<version>-manifest.json` under its expected name and reports
the release as having no signed manifest (it fails closed, but the release cannot be installed). This
has not been verified against a live upload.

`sign_manifest.py` refuses a key whose public half is not listed in `src/updates/TrustedKeys.h`
(the application would reject the signature). `--allow-untrusted-key` exists only for test fixtures
(`tests/unit/updates/generate_test_fixtures.py`). It prints only a short fingerprint of the public
key, never key material, and deletes the signature again if verification fails.

## Tests

```powershell
python -m unittest discover -s scripts/release -p "test_*.py" -v
```

Signing tests use throw-away keys in a temporary directory and need the OpenSSL CLI
(`AETHER_OPENSSL`, `PATH`, or `C:\Program Files\Git\usr\bin\openssl.exe`); they are skipped without it.
`tests/unit/updates/generate_test_fixtures.py` runs all three scripts end to end and embeds the public
results in the C++ tests.

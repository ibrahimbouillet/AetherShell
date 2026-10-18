# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Ibrahim Bouillet
"""Creates the release manifest (schema v1) for Aether Shell installer assets.

Every digest and size is computed from the real files; nothing is typed by hand,
and inputs that look like unfilled template values are refused.

Example:
    python scripts/release/make_manifest.py --version 1.2.0 --channel stable \
        dist/AetherShell-1.2.0-win-x64-setup.exe dist/AetherShell-1.2.0-win-arm64-setup.exe

The manifest is written next to the first asset as AetherShell-<version>-manifest.json
unless --output is given. Sign it afterwards with sign_manifest.py.
"""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

import release_common as rc


def build_manifest(version: str, channel: str, assets: list[Path], minimum_build: int,
                   release_notes_url: str, owner: str, repository: str) -> dict:
    rc.parse_semver(version)
    if channel not in rc.CHANNELS:
        raise rc.ReleaseError(f"channel must be one of {', '.join(rc.CHANNELS)}")
    if channel == "stable" and rc.expected_channel(version) != "stable":
        raise rc.ReleaseError(f"version {version} has prerelease identifiers and cannot be published as stable")
    if rc.expected_channel(version) == "prerelease" and channel != "prerelease":
        raise rc.ReleaseError(f"version {version} must use the prerelease channel")
    if not 0 < minimum_build < 1_000_000:
        raise rc.ReleaseError("minimum Windows build must be a positive build number")

    rc.reject_placeholder("release notes URL", release_notes_url)
    # Same rule as the application (IsRepositoryWebUrl): a URL it would reject makes every client
    # refuse the signed manifest, so the release could never be installed.
    if not rc.is_repository_web_url(release_notes_url, owner, repository):
        raise rc.ReleaseError("release notes URL must be an allowed page in the configured GitHub repository")

    if not assets:
        raise rc.ReleaseError("at least one installer asset is required")
    for name in (rc.manifest_name(version), rc.signature_name(version), rc.checksums_name(version),
                 *(rc.installer_name(version, arch) for arch in rc.ARCHITECTURES)):
        if len(name) > rc.MAX_ASSET_FILE_NAME_LENGTH:
            raise rc.ReleaseError(f"version {version} is too long: asset names would exceed "
                                  f"{rc.MAX_ASSET_FILE_NAME_LENGTH} characters")

    entries = []
    seen_names: set[str] = set()
    for path in assets:
        name = path.name
        architecture = next((arch for arch in rc.ARCHITECTURES if name == rc.installer_name(version, arch)), None)
        if architecture is None:
            raise rc.ReleaseError("installer asset does not follow the Aether Shell naming convention")
        if name in seen_names:
            raise rc.ReleaseError(f"asset '{name}' is listed twice")
        seen_names.add(name)
        if not path.is_file():
            raise rc.ReleaseError("installer asset is not available")

        digest, size = rc.sha256_file(path)
        if size == 0:
            raise rc.ReleaseError(f"asset '{name}' is empty")
        if size > rc.MAX_ASSET_BYTES:
            raise rc.ReleaseError(f"asset '{name}' is larger than {rc.MAX_ASSET_BYTES} bytes")
        if rc.is_placeholder_digest(digest):
            raise rc.ReleaseError(f"asset '{name}' produced a placeholder-like digest")
        entries.append({
            "filename": name,
            "architecture": architecture,
            "format": rc.INSTALLER_FORMAT,
            "sha256": digest,
            "size": size,
        })

    entries.sort(key=lambda entry: entry["filename"])
    # Key order follows the documented schema; the application verifies the exact bytes, so the
    # file must not be reformatted after signing.
    return {
        "schemaVersion": rc.MANIFEST_SCHEMA_VERSION,
        "product": rc.PRODUCT,
        "version": version,
        "channel": channel,
        "minimumWindowsBuild": minimum_build,
        "releaseNotesUrl": release_notes_url,
        "assets": entries,
    }


def main(argv: list[str]) -> int:
    parser = rc.PrivateArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--version", required=True, help="SemVer 2.0.0 version without a leading 'v'")
    parser.add_argument("--channel", required=True, choices=rc.CHANNELS)
    parser.add_argument("--minimum-windows-build", type=int, default=rc.DEFAULT_MINIMUM_WINDOWS_BUILD)
    parser.add_argument("--release-notes-url", help="defaults to the GitHub release page of the tag v<version>")
    parser.add_argument("--owner", default=rc.DEFAULT_OWNER)
    parser.add_argument("--repository", default=rc.DEFAULT_REPOSITORY)
    parser.add_argument("--output", type=Path, help="manifest path (default: next to the first asset)")
    parser.add_argument("assets", nargs="+", type=Path, help="installer files to describe")
    args = parser.parse_args(argv)

    try:
        notes_url = args.release_notes_url or (
            f"https://github.com/{args.owner}/{args.repository}/releases/tag/v{args.version}")
        manifest = build_manifest(args.version, args.channel, args.assets, args.minimum_windows_build,
                                  notes_url, args.owner, args.repository)
        output = args.output or args.assets[0].parent / rc.manifest_name(args.version)
        if output.name != rc.manifest_name(args.version):
            raise rc.ReleaseError(f"the manifest must be named {rc.manifest_name(args.version)}")
        text = json.dumps(manifest, indent=2, ensure_ascii=True) + "\n"
        rc.write_atomic(output, text.encode("ascii"))
    except (rc.ReleaseError, OSError, ValueError) as error:
        print(f"make_manifest: error: {rc.safe_error(error)}", file=sys.stderr)
        return 1

    print(f"manifest written: {output.name}")
    for entry in manifest["assets"]:
        print(f"  {entry['filename']}  {entry['size']} bytes  sha256 {entry['sha256']}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))

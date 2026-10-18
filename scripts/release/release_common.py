# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Ibrahim Bouillet
"""Shared helpers for the Aether Shell release scripts.

Standard library only (Python 3.11+). The rules here mirror the C++ validation
in src/updates (SemanticVersion, AssetNaming, UpdateManifest) so a release that
the scripts accept is a release the application accepts.
"""

from __future__ import annotations

import argparse
import hashlib
import os
import re
import tempfile
from pathlib import Path

PRODUCT = "AetherShell"
DEFAULT_OWNER = "ibrahimbouillet"
DEFAULT_REPOSITORY = "AetherShell"
ARCHITECTURES = ("x64", "arm64")
CHANNELS = ("stable", "prerelease")
INSTALLER_FORMAT = "exe"
MANIFEST_SCHEMA_VERSION = 1
DEFAULT_MINIMUM_WINDOWS_BUILD = 19041  # Windows 10, version 2004
MAX_ASSET_BYTES = 512 * 1024 * 1024
UINT64_MAX = 2**64 - 1
MAX_SEMVER_LENGTH = 256          # kMaxSemanticVersionLength (SemanticVersion.h)
MAX_ASSET_FILE_NAME_LENGTH = 200  # kMaxAssetFileNameLength (AssetNaming.h)

# SemVer 2.0.0 grammar (https://semver.org), ASCII only.
_NUMERIC = r"(?:0|[1-9][0-9]*)"
_PRERELEASE_ID = r"(?:0|[1-9][0-9]*|[0-9]*[A-Za-z-][0-9A-Za-z-]*)"
_BUILD_ID = r"[0-9A-Za-z-]+"
_SEMVER = re.compile(
    rf"({_NUMERIC})\.({_NUMERIC})\.({_NUMERIC})"
    rf"(?:-({_PRERELEASE_ID}(?:\.{_PRERELEASE_ID})*))?"
    rf"(?:\+({_BUILD_ID}(?:\.{_BUILD_ID})*))?"
)

# Text that indicates a template value was never filled in.
_PLACEHOLDER_MARKERS = ("<", ">", "{", "}", "todo", "tbd", "placeholder", "changeme", "xxx")


class ReleaseError(Exception):
    """A release input is invalid. Use controlled messages without raw paths or key material."""


class PrivateArgumentParser(argparse.ArgumentParser):
    """ArgumentParser's default errors echo arbitrary values, which can contain private paths."""

    def error(self, message: str) -> None:
        self.exit(2, "release tool: error: invalid command-line arguments; use --help\n")


def safe_error(error: Exception) -> str:
    """File and decoder exceptions may include private locations or input fragments."""
    if isinstance(error, OSError):
        return "file operation failed"
    if isinstance(error, ReleaseError):
        return str(error)
    return "invalid release data"


def parse_semver(text: str) -> tuple[int, int, int, str | None, str | None]:
    """Strictly parses a SemVer 2.0.0 version (no leading "v"). Returns (major, minor, patch, pre, build)."""
    if len(text) > MAX_SEMVER_LENGTH:
        raise ReleaseError(f"the version is longer than {MAX_SEMVER_LENGTH} characters")
    match = _SEMVER.fullmatch(text)
    if match is None:
        raise ReleaseError("invalid SemVer 2.0.0 version (expected MAJOR.MINOR.PATCH[-pre][+build])")
    major, minor, patch = (int(match.group(i)) for i in (1, 2, 3))
    if max(major, minor, patch) > UINT64_MAX:
        raise ReleaseError("the version has a numeric component larger than 2^64-1")
    return major, minor, patch, match.group(4), match.group(5)


def expected_channel(version: str) -> str:
    """Versions with prerelease identifiers belong to the prerelease channel."""
    return "prerelease" if parse_semver(version)[3] is not None else "stable"


def installer_name(version: str, architecture: str) -> str:
    return f"{PRODUCT}-{version}-win-{architecture}-setup.exe"


def manifest_name(version: str) -> str:
    return f"{PRODUCT}-{version}-manifest.json"


def signature_name(version: str) -> str:
    return f"{PRODUCT}-{version}-manifest.json.sig"


def checksums_name(version: str) -> str:
    return f"{PRODUCT}-{version}-checksums.txt"


_URL_SEGMENT = re.compile(r"[A-Za-z0-9._~%+-]+")


def is_repository_web_url(url: str, owner: str, repository: str) -> bool:
    """Mirrors IsRepositoryWebUrl (UpdateTypes.cpp), which the application applies to releaseNotesUrl.

    https://github.com/<owner>/<repository>/<path>: owner and name compared case-insensitively, a
    non-empty path of segments from [A-Za-z0-9._~%+-] that are not "." or "..", one optional trailing
    slash, and no query, fragment, empty segment or other character.
    """
    origin = "https://github.com/"
    if not url.startswith(origin):
        return False
    parts = url[len(origin):].split("/", 2)
    if len(parts) != 3 or not (parts[0].isascii() and parts[1].isascii()):
        return False
    if parts[0].lower() != owner.lower() or parts[1].lower() != repository.lower():
        return False
    path = parts[2]
    if path.endswith("/"):
        path = path[:-1]
    if not path:
        return False
    return all(_URL_SEGMENT.fullmatch(segment) is not None and segment not in (".", "..")
               for segment in path.split("/"))


def reject_placeholder(label: str, value: str) -> None:
    lowered = value.lower()
    for marker in _PLACEHOLDER_MARKERS:
        if marker in lowered:
            raise ReleaseError(f"{label} looks like an unfilled placeholder")
    if not value.strip():
        raise ReleaseError(f"{label} is empty")


def sha256_file(path: Path) -> tuple[str, int]:
    """Returns (lowercase hex SHA-256, size in bytes) of a file, streaming it."""
    digest = hashlib.sha256()
    size = 0
    with path.open("rb") as stream:
        while chunk := stream.read(1024 * 1024):
            digest.update(chunk)
            size += len(chunk)
    return digest.hexdigest(), size


def is_placeholder_digest(hex_digest: str) -> bool:
    """Digests that a real release asset can never have (mirrors IsPlaceholderDigest in UpdateManifest.cpp).

    The digest of empty input, and any text that repeats with a period of at most 16 characters
    ("000...", "abab...", "deadbeef...", "0123456789abcdef..."). A real SHA-256 output has that
    property with negligible probability.
    """
    if hex_digest == hashlib.sha256(b"").hexdigest():
        return True
    for period in range(1, 17):
        if all(hex_digest[i] == hex_digest[i - period] for i in range(period, len(hex_digest))):
            return True
    return False


def is_sha256_hex(text: str) -> bool:
    """Exactly 64 lowercase hex characters (the manifest digest format)."""
    return re.fullmatch(r"[0-9a-f]{64}", text) is not None


def write_atomic(path: Path, data: bytes) -> None:
    """Writes `data` to a temporary sibling and renames it over `path`."""
    path.parent.mkdir(parents=True, exist_ok=True)
    handle, temp_name = tempfile.mkstemp(prefix=path.name + ".", suffix=".tmp", dir=path.parent)
    try:
        with os.fdopen(handle, "wb") as stream:
            stream.write(data)
            stream.flush()
            os.fsync(stream.fileno())
        os.replace(temp_name, path)
    except BaseException:
        try:
            os.unlink(temp_name)
        except OSError:
            pass
        raise

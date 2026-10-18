# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Ibrahim Bouillet
"""Reject private build information before a native setup is copied into public assets.

The setup stores its complete payload as uncompressed RCDATA. Scanning every byte also scans
all embedded app/launcher/updater binaries and their CodeView records, without loading code.
Findings contain categories only: matched paths, machine names and source bytes are never printed.
"""
from __future__ import annotations

import os
import re
import sys
from pathlib import Path

import release_common as rc

_WIDE_ASCII = re.compile(rb"(?:[\x20-\x7e]\x00){4,}")
_ABSOLUTE_SOURCE = re.compile(
    rb"(?<![A-Za-z0-9:/])(?:[A-Za-z]:[\\/]|/)[^\x00-\x20\"<>|]{1,1024}"
    rb"\.(?:c|cc|cpp|cxx|h|hh|hpp|hxx|inl|cmake|py|ps1)(?=[\x00-\x20\"]|$)", re.IGNORECASE)
_PUBLIC_ATTRIBUTION = (b"ibrahim bouillet", b"ibrahimbouillet")
_INPUT_DIRECTORIES = ("src", "launcher", "updater", "installer", "resources", "cmake")
_INPUT_FILES = ("CMakeLists.txt", "LICENSE", "THIRD_PARTY_NOTICES.md")


def _path_variants(value: str) -> set[bytes]:
    normalized = value.rstrip("/\\")
    if not normalized:
        return set()
    variants = (normalized, normalized.replace("\\", "/"), normalized.replace("/", "\\"))
    return {text.encode(encoding).lower() for text in variants for encoding in ("utf-8", "utf-16le")}


def audit_bytes(data: bytes, *, source_root: str = "", home: str = "", machine: str = "") -> set[str]:
    findings: set[str] = set()
    lowered = data.lower()
    for label, value in (("source_root", source_root), ("personal_root", home)):
        if any(pattern in lowered for pattern in _path_variants(value)):
            findings.add(label)
    for prefix in (":\\users\\", ":/users/", ":\\documents and settings\\", "/home/", "/users/"):
        if any(prefix.encode(encoding) in lowered for encoding in ("ascii", "utf-16le")):
            findings.add("personal_path")
    if _ABSOLUTE_SOURCE.search(data):
        findings.add("absolute_source_path")

    machine_forms = (machine, machine.casefold(), machine.upper()) if machine else ()
    machine_pattern = re.compile(rb"(?<![a-z0-9_-])(?:" + b"|".join(
        re.escape(value.encode("utf-8").lower()) for value in machine_forms) +
        rb")(?![a-z0-9_-])") if machine else None

    def inspect_machine(view: bytes) -> None:
        if machine_pattern is None:
            return
        for attribution in _PUBLIC_ATTRIBUTION:
            view = view.replace(attribution, b"")
        if machine_pattern.search(view):
            findings.add("machine_name")

    inspect_machine(lowered)
    for match in _WIDE_ASCII.finditer(data):
        view = match.group()[::2]
        if _ABSOLUTE_SOURCE.search(view):
            findings.add("absolute_source_path")
        inspect_machine(view.lower())
    if machine and not machine.isascii() and any(value.encode("utf-16le").lower() in lowered for value in machine_forms):
        findings.add("machine_name")
    if b"-----begin private key-----" in lowered or b"-----begin ec private key-----" in lowered:
        findings.add("private_key_material")

    offset = 0
    while (offset := data.find(b"RSDS", offset)) != -1:
        start = offset + 24  # signature + GUID (16 bytes) + age (4 bytes)
        end = data.find(b"\0", start, min(len(data), start + 4096))
        offset += 4
        if start >= len(data) or end == -1:
            continue
        name = data[start:end]
        if not name or any(byte < 32 or byte == 127 for byte in name):
            continue
        # Separators are ASCII in UTF-8 and legacy Windows encodings alike. Decoding as UTF-8
        # would miss CodeView paths containing an ANSI-encoded account/directory name.
        if b"/" in name or b"\\" in name or b":" in name or name in (b".", b".."):
            findings.add("codeview_path")
    return findings


def source_is_newer(package: Path, source_root: Path) -> bool:
    """Conservative source-input timestamp guard; it does not replace build/test qualification."""
    stamp = package.stat().st_mtime_ns
    for name in _INPUT_FILES:
        path = source_root / name
        if path.exists() and path.stat().st_mtime_ns > stamp:
            return True
    for name in _INPUT_DIRECTORIES:
        directory = source_root / name
        if not directory.exists():
            continue
        for path in (directory, *directory.rglob("*")):
            if path.is_symlink():
                continue
            if path.stat().st_mtime_ns > stamp:
                return True
    return False


def main(arguments: list[str]) -> int:
    parser = rc.PrivateArgumentParser(description=__doc__)
    parser.add_argument("package", type=Path)
    parser.add_argument("--source-root", type=Path)
    parser.add_argument("--check-freshness", action="store_true")
    parser.add_argument("--version")
    args = parser.parse_args(arguments)
    try:
        if args.source_root is not None:
            args.source_root = args.source_root.resolve()
        if args.version is not None:
            rc.parse_semver(args.version)
        if args.package.stat().st_size > rc.MAX_ASSET_BYTES:
            raise rc.ReleaseError("package exceeds the supported size limit")
        if args.check_freshness and (args.source_root is None or source_is_newer(args.package, args.source_root)):
            raise rc.ReleaseError("package is older than product source inputs; rebuild Release")
        findings = audit_bytes(args.package.read_bytes(), source_root=str(args.source_root or ""),
                               home=str(Path.home()), machine=os.environ.get("COMPUTERNAME", ""))
        if findings:
            print("package audit: rejected private build information (" + ", ".join(sorted(findings)) + ")",
                  file=sys.stderr)
            return 1
    except (OSError, ValueError, rc.ReleaseError) as error:
        print("package audit: " + rc.safe_error(error), file=sys.stderr)
        return 1
    print("Package privacy audit passed (setup and uncompressed embedded payload bytes).")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))

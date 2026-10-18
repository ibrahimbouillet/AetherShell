# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Ibrahim Bouillet
"""Writes AetherShell-<version>-checksums.txt in sha256sum format.

Each line is "<64 lowercase hex>  <file name>" (two spaces, LF line endings),
sorted by file name, so users can run "sha256sum -c" on Linux/Git Bash or
compare against Get-FileHash on Windows. Checksums prove integrity only: the
signed manifest is what proves who published a release.

Example:
    python scripts/release/make_checksums.py --version 1.2.0 dist/AetherShell-1.2.0-*
"""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

import release_common as rc


def build_checksums(files: list[Path]) -> str:
    if not files:
        raise rc.ReleaseError("no files given")
    lines = []
    names: set[str] = set()
    for path in sorted(files, key=lambda item: item.name):
        if not path.is_file():
            raise rc.ReleaseError("input file is not available")
        if path.name in names:
            raise rc.ReleaseError("two input files have the same name")
        if not path.name.isascii() or any(c.isspace() for c in path.name) or "\\" in path.name:
            raise rc.ReleaseError("input file name cannot be represented in the checksum format")
        names.add(path.name)
        digest, size = rc.sha256_file(path)
        if size == 0:
            raise rc.ReleaseError("input file is empty")
        lines.append(f"{digest}  {path.name}\n")
    return "".join(lines)


def main(argv: list[str]) -> int:
    parser = rc.PrivateArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--version", required=True, help="release version (names the output file)")
    parser.add_argument("--output", type=Path, help="output path (default: next to the first file)")
    parser.add_argument("files", nargs="+", type=Path)
    args = parser.parse_args(argv)

    try:
        rc.parse_semver(args.version)
        output = args.output or args.files[0].parent / rc.checksums_name(args.version)
        if output.name != rc.checksums_name(args.version):
            raise rc.ReleaseError(f"the checksums file must be named {rc.checksums_name(args.version)}")
        files = [path for path in args.files if path.resolve() != output.resolve()]
        rc.write_atomic(output, build_checksums(files).encode("ascii"))
    except (rc.ReleaseError, OSError, ValueError) as error:
        print(f"make_checksums: error: {rc.safe_error(error)}", file=sys.stderr)
        return 1

    print(f"checksums written: {output.name}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))

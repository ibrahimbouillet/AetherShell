# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Ibrahim Bouillet
"""Regenerates UpdateTestFixtures.h by running the real release scripts end to end.

A throw-away ECDSA P-256 TEST key is generated with OpenSSL inside a temporary
directory, used to sign the fixture manifests with scripts/release/sign_manifest.py,
and deleted with the directory. Only public data (public keys, manifests,
signatures, checksums) is written to the generated header. The production
release key is not involved and no private key is ever stored in the repository.

Usage (from the repository root):
    python tests/unit/updates/generate_test_fixtures.py [--openssl PATH]
"""

from __future__ import annotations

import argparse
import base64
import os
import shutil
import subprocess
import sys
import tempfile
from dataclasses import dataclass
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
SCRIPTS = ROOT / "scripts" / "release"
OUTPUT = Path(__file__).with_name("UpdateTestFixtures.h")
P256_SPKI_PREFIX = bytes.fromhex("3059301306072a8648ce3d020106082a8648ce3d030107034200")


@dataclass(frozen=True)
class Asset:
    architecture: str
    size: int
    seed: int


@dataclass(frozen=True)
class Fixture:
    symbol: str
    version: str
    channel: str
    minimum_build: int
    assets: tuple[Asset, ...]


FIXTURES = (
    Fixture("kStable_1_2_0", "1.2.0", "stable", 19041, (Asset("x64", 300_000, 1),)),
    Fixture("kPrerelease_2_0_0_beta_2", "2.0.0-beta.2", "prerelease", 19041, (Asset("x64", 123_457, 2),)),
    Fixture("kStable_1_3_0_MultiArch", "1.3.0", "stable", 22000,
            (Asset("x64", 4_096, 3), Asset("arm64", 5_000, 4))),
)


def asset_bytes(size: int, seed: int) -> bytes:
    """Deterministic test payload; TestAssetBytes in UpdateTestSupport.h uses the same formula."""
    return bytes(((i * 131 + seed * 17 + (i >> 8)) & 0xFF) for i in range(size))


def run(arguments: list[str]) -> subprocess.CompletedProcess:
    result = subprocess.run(arguments, stdin=subprocess.DEVNULL, capture_output=True, check=False)
    if result.returncode != 0:
        sys.stderr.write(result.stdout.decode("utf-8", "replace"))
        sys.stderr.write(result.stderr.decode("utf-8", "replace"))
        raise SystemExit(f"command failed: {Path(arguments[0]).name} {arguments[1] if len(arguments) > 1 else ''}")
    return result


def public_key_hex(openssl: str, key: Path, temp: Path) -> str:
    der = temp / (key.stem + ".pub.der")
    run([openssl, "pkey", "-in", str(key), "-pubout", "-outform", "DER", "-out", str(der)])
    spki = der.read_bytes()
    if not spki.startswith(P256_SPKI_PREFIX) or len(spki) != len(P256_SPKI_PREFIX) + 65:
        raise SystemExit("OpenSSL did not produce an uncompressed P-256 public key")
    return spki[len(P256_SPKI_PREFIX) + 1:].hex()


def c_literal(text: str) -> str:
    """Splits text into C++ string literal lines with explicit escapes (exact bytes, no raw strings)."""
    out = []
    for line in text.splitlines(keepends=True):
        escaped = line.replace("\\", "\\\\").replace('"', '\\"').replace("\n", "\\n").replace("\r", "\\r")
        out.append(f'    "{escaped}"')
    return "\n".join(out)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--openssl", default=os.environ.get("AETHER_OPENSSL") or shutil.which("openssl")
                        or r"C:\Program Files\Git\usr\bin\openssl.exe")
    args = parser.parse_args()
    openssl = args.openssl
    python = sys.executable

    entries = []
    with tempfile.TemporaryDirectory(prefix="aether-test-fixtures-") as temp_name:
        temp = Path(temp_name)
        key = temp / "test-signing-key.pem"
        other = temp / "unrelated-key.pem"
        run([openssl, "ecparam", "-name", "prime256v1", "-genkey", "-noout", "-out", str(key)])
        run([openssl, "ecparam", "-name", "prime256v1", "-genkey", "-noout", "-out", str(other)])
        test_public = public_key_hex(openssl, key, temp)
        other_public = public_key_hex(openssl, other, temp)

        for fixture in FIXTURES:
            folder = temp / fixture.version
            folder.mkdir()
            asset_paths = []
            for asset in fixture.assets:
                path = folder / f"AetherShell-{fixture.version}-win-{asset.architecture}-setup.exe"
                path.write_bytes(asset_bytes(asset.size, asset.seed))
                asset_paths.append(path)
            manifest = folder / f"AetherShell-{fixture.version}-manifest.json"
            signature = folder / f"AetherShell-{fixture.version}-manifest.json.sig"
            checksums = folder / f"AetherShell-{fixture.version}-checksums.txt"

            run([python, str(SCRIPTS / "make_manifest.py"), "--version", fixture.version,
                 "--channel", fixture.channel, "--minimum-windows-build", str(fixture.minimum_build),
                 "--output", str(manifest), *map(str, asset_paths)])
            run([python, str(SCRIPTS / "sign_manifest.py"), str(manifest), "--key", str(key),
                 "--openssl", openssl, "--allow-untrusted-key"])
            run([python, str(SCRIPTS / "make_checksums.py"), "--version", fixture.version,
                 "--output", str(checksums), *map(str, asset_paths), str(manifest), str(signature)])
            # A DER signature straight from OpenSSL, for the verifier's DER acceptance tests.
            der = folder / "signature.der"
            run([openssl, "dgst", "-sha256", "-sign", str(key), "-out", str(der), str(manifest)])

            entries.append((fixture, manifest.read_bytes().decode("ascii"),
                            signature.read_bytes().decode("ascii"),
                            base64.b64encode(der.read_bytes()).decode("ascii"),
                            checksums.read_bytes().decode("ascii")))
        # Leaving the with-block deletes both private keys.

    parts = [
        "// SPDX-License-Identifier: MIT",
        "// Copyright (c) 2026 Ibrahim Bouillet",
        "//",
        "// GENERATED by tests/unit/updates/generate_test_fixtures.py - do not edit by hand.",
        "// Manifests, signatures and checksums were produced by scripts/release/*.py with a",
        "// throw-away TEST key that existed only in a temporary directory. This is NOT the",
        "// production release key (src/updates/TrustedKeys.h).",
        "#pragma once",
        "",
        "#include <cstdint>",
        "#include <span>",
        "#include <string_view>",
        "",
        "namespace aether::updates::fixtures {",
        "",
        "// Public half of the test signing key (X || Y).",
        f'inline constexpr std::string_view kTestPublicKeyHex =\n    "{test_public[:64]}"\n    "{test_public[64:]}";',
        "// Public key of an unrelated P-256 key pair (signatures below must not verify with it).",
        f'inline constexpr std::string_view kUnrelatedPublicKeyHex =\n    "{other_public[:64]}"\n    "{other_public[64:]}";',
        "",
        "struct FixtureAsset {",
        "    std::string_view fileName;",
        "    std::string_view architecture;",
        "    std::uint64_t size;",
        "    std::uint32_t seed;  // payload = TestAssetBytes(size, seed)",
        "};",
        "",
        "struct SignedRelease {",
        "    std::string_view version;",
        "    std::string_view channel;",
        "    std::uint32_t minimumWindowsBuild;",
        "    std::span<const FixtureAsset> assets;",
        "    std::string_view manifest;            // exact signed bytes",
        "    std::string_view signature;           // .sig asset: base64 P1363 r||s plus newline",
        "    std::string_view derSignatureBase64;  // same key, DER encoding from OpenSSL, base64",
        "    std::string_view checksums;           // make_checksums.py output",
        "};",
        "",
    ]
    for fixture, manifest_text, signature_text, der_text, checksums_text in entries:
        parts.append(f"inline constexpr FixtureAsset {fixture.symbol}_Assets[] = {{")
        for asset in fixture.assets:
            name = f"AetherShell-{fixture.version}-win-{asset.architecture}-setup.exe"
            parts.append(f'    {{"{name}", "{asset.architecture}", {asset.size}, {asset.seed}}},')
        parts += [
            "};",
            "",
            f"inline constexpr SignedRelease {fixture.symbol}{{",
            f'    "{fixture.version}",',
            f'    "{fixture.channel}",',
            f"    {fixture.minimum_build},",
            f"    {fixture.symbol}_Assets,",
            c_literal(manifest_text) + ",",
            c_literal(signature_text) + ",",
            f'    "{der_text}",',
            c_literal(checksums_text) + ",",
            "};",
            "",
        ]
    parts += ["}  // namespace aether::updates::fixtures", ""]
    OUTPUT.write_bytes("\n".join(parts).encode("ascii"))
    print(f"wrote {OUTPUT.relative_to(ROOT)}")
    return 0


if __name__ == "__main__":
    sys.exit(main())

# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Ibrahim Bouillet
"""Signs an Aether Shell release manifest with the offline ECDSA P-256 release key.

The signature covers the exact bytes of the manifest file. The OpenSSL CLI
("openssl dgst -sha256 -sign <key>") produces a DER signature, which this script
converts to the 64-byte IEEE P1363 form (r || s) and writes base64-encoded to
AetherShell-<version>-manifest.json.sig. The written signature is then decoded
again and verified with the public key derived from the private key, and that
public key must be one of the keys compiled into the application
(src/updates/TrustedKeys.h) unless --allow-untrusted-key is given (test keys).

Key material is never printed. The private key path comes from --key or the
AETHER_RELEASE_SIGNING_KEY_FILE environment variable; an encrypted key reads its
passphrase from AETHER_RELEASE_SIGNING_KEY_PASSPHRASE (passed to OpenSSL by
variable name, never on the command line).

Example:
    # Set the signing-key environment variable privately before running this command.
    python scripts/release/sign_manifest.py dist/AetherShell-1.2.0-manifest.json
"""

from __future__ import annotations

import argparse
import base64
import hashlib
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

import release_common as rc

KEY_FILE_VARIABLE = "AETHER_RELEASE_SIGNING_KEY_FILE"
PASSPHRASE_VARIABLE = "AETHER_RELEASE_SIGNING_KEY_PASSPHRASE"
OPENSSL_VARIABLE = "AETHER_OPENSSL"

# Order of the P-256 group (signature components must be in [1, n-1]).
P256_ORDER = int("FFFFFFFF00000000FFFFFFFFFFFFFFFFBCE6FAADA7179E84F3B9CAC2FC632551", 16)
# DER SubjectPublicKeyInfo prefix for id-ecPublicKey + prime256v1 with an uncompressed point.
P256_SPKI_PREFIX = bytes.fromhex("3059301306072a8648ce3d020106082a8648ce3d030107034200")
DEFAULT_TRUSTED_KEYS = Path(__file__).resolve().parents[2] / "src" / "updates" / "TrustedKeys.h"


def find_openssl(explicit: str | None) -> str:
    candidates = [explicit, os.environ.get(OPENSSL_VARIABLE), shutil.which("openssl")]
    if os.name == "nt":
        candidates.append(r"C:\Program Files\Git\usr\bin\openssl.exe")
    for candidate in candidates:
        if candidate and Path(candidate).is_file():
            return candidate
    raise rc.ReleaseError("OpenSSL was not found; pass --openssl or set AETHER_OPENSSL")


def run_openssl(openssl: str, arguments: list[str], what: str) -> subprocess.CompletedProcess:
    # stdin is closed so an encrypted key without a passphrase fails instead of waiting for input.
    result = subprocess.run([openssl, *arguments], stdin=subprocess.DEVNULL, capture_output=True, check=False)
    if result.returncode != 0:
        # OpenSSL stderr may reveal private key paths or provider configuration.
        raise rc.ReleaseError(f"openssl failed to {what} (exit code {result.returncode})")
    return result


def passphrase_arguments() -> list[str]:
    return ["-passin", f"env:{PASSPHRASE_VARIABLE}"] if os.environ.get(PASSPHRASE_VARIABLE) else []


def parse_der_signature(der: bytes) -> tuple[int, int]:
    """Strict DER ECDSA-Sig-Value parser (mirrors EcdsaP256Verifier's DER rules)."""
    if len(der) < 8 or der[0] != 0x30 or der[1] >= 0x80 or der[1] != len(der) - 2:
        raise rc.ReleaseError("OpenSSL produced a malformed DER signature")
    position = 2
    values = []
    for _ in range(2):
        if position + 2 > len(der) or der[position] != 0x02:
            raise rc.ReleaseError("OpenSSL produced a malformed DER signature")
        length = der[position + 1]
        body = der[position + 2:position + 2 + length]
        if length == 0 or length > 33 or len(body) != length or body[0] & 0x80:
            raise rc.ReleaseError("OpenSSL produced a malformed DER signature")
        if length > 1 and body[0] == 0 and not body[1] & 0x80:
            raise rc.ReleaseError("OpenSSL produced a non-minimal DER integer")
        values.append(int.from_bytes(body, "big"))
        position += 2 + length
    if position != len(der):
        raise rc.ReleaseError("OpenSSL produced trailing bytes after the DER signature")
    r, s = values
    if not (0 < r < P256_ORDER and 0 < s < P256_ORDER):
        raise rc.ReleaseError("signature components are out of range for P-256")
    return r, s


def encode_der_signature(r: int, s: int) -> bytes:
    def integer(value: int) -> bytes:
        body = value.to_bytes(max(1, (value.bit_length() + 7) // 8), "big")
        if body[0] & 0x80:
            body = b"\x00" + body
        return b"\x02" + bytes([len(body)]) + body

    sequence = integer(r) + integer(s)
    return b"\x30" + bytes([len(sequence)]) + sequence


def check_manifest(path: Path) -> str:
    """Light sanity check so the wrong file is never signed. Returns the manifest version."""
    data = path.read_bytes()
    if data.startswith(b"\xef\xbb\xbf"):
        raise rc.ReleaseError("the manifest must not start with a byte order mark")
    try:
        manifest = json.loads(data.decode("utf-8"))
    except (UnicodeDecodeError, json.JSONDecodeError) as error:
        raise rc.ReleaseError(f"the manifest is not valid JSON: {error}") from None
    if not isinstance(manifest, dict) or manifest.get("schemaVersion") != rc.MANIFEST_SCHEMA_VERSION \
            or manifest.get("product") != rc.PRODUCT:
        raise rc.ReleaseError("the file is not an Aether Shell manifest (schemaVersion 1)")
    version = manifest.get("version")
    if not isinstance(version, str):
        raise rc.ReleaseError("the manifest has no version")
    rc.parse_semver(version)
    if path.name != rc.manifest_name(version):
        raise rc.ReleaseError(f"the manifest file must be named {rc.manifest_name(version)}")
    return version


def trusted_key_hexes(header: Path) -> set[str]:
    text = header.read_text(encoding="utf-8")
    text = re.sub(r'"\s*"', "", text)  # join adjacent C++ string literals ("ab" "cd" -> "abcd")
    return {match.lower() for match in re.findall(r'"([0-9A-Fa-f]{128})"', text)}


def main(argv: list[str]) -> int:
    parser = rc.PrivateArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("manifest", type=Path)
    parser.add_argument("--key", type=Path, help=f"private key (PEM); default: ${KEY_FILE_VARIABLE}")
    parser.add_argument("--output", type=Path, help="signature path (default: <manifest>.sig)")
    parser.add_argument("--openssl", help=f"OpenSSL executable; default: ${OPENSSL_VARIABLE} or PATH")
    parser.add_argument("--trusted-keys", type=Path, default=DEFAULT_TRUSTED_KEYS,
                        help="TrustedKeys.h whose keys the signing key must match")
    parser.add_argument("--allow-untrusted-key", action="store_true",
                        help="accept a key that is not in TrustedKeys.h (test fixtures only)")
    args = parser.parse_args(argv)

    output = args.output or args.manifest.with_name(args.manifest.name + ".sig")
    wrote_output = False
    try:
        key_value = args.key or os.environ.get(KEY_FILE_VARIABLE)
        if not key_value:
            raise rc.ReleaseError(f"no signing key: pass --key or set {KEY_FILE_VARIABLE}")
        key = Path(key_value)
        if not key.is_file():
            raise rc.ReleaseError("the signing key file does not exist")
        version = check_manifest(args.manifest)
        if output.name != rc.signature_name(version):
            raise rc.ReleaseError(f"the signature must be named {rc.signature_name(version)}")
        openssl = find_openssl(args.openssl)

        # The temporary directory only ever holds public data (signatures and the public key).
        with tempfile.TemporaryDirectory(prefix="aether-sign-") as temp:
            temp_dir = Path(temp)
            der_path = temp_dir / "signature.der"
            run_openssl(openssl, ["dgst", "-sha256", "-sign", str(key), *passphrase_arguments(),
                                  "-out", str(der_path), str(args.manifest)], "sign the manifest")
            r, s = parse_der_signature(der_path.read_bytes())
            p1363 = r.to_bytes(32, "big") + s.to_bytes(32, "big")
            rc.write_atomic(output, base64.b64encode(p1363) + b"\n")
            wrote_output = True

            public_pem = temp_dir / "public.pem"
            public_der = temp_dir / "public.der"
            run_openssl(openssl, ["pkey", "-in", str(key), *passphrase_arguments(), "-pubout",
                                  "-out", str(public_pem)], "derive the public key")
            run_openssl(openssl, ["pkey", "-pubin", "-in", str(public_pem), "-outform", "DER",
                                  "-out", str(public_der)], "encode the public key")
            spki = public_der.read_bytes()
            if len(spki) != len(P256_SPKI_PREFIX) + 65 or not spki.startswith(P256_SPKI_PREFIX) \
                    or spki[len(P256_SPKI_PREFIX)] != 0x04:
                raise rc.ReleaseError("the signing key is not an ECDSA P-256 key")
            public_hex = spki[len(P256_SPKI_PREFIX) + 1:].hex()

            if not args.allow_untrusted_key:
                trusted = trusted_key_hexes(args.trusted_keys)
                if public_hex not in trusted:
                    raise rc.ReleaseError(
                        "the signing key's public key is not listed in the application trust anchors; "
                        "the application would reject this signature")

            # Verify what was actually written: decode the .sig file, rebuild DER, verify with OpenSSL.
            written = output.read_bytes().strip()
            decoded = base64.b64decode(written, validate=True)
            if len(decoded) != 64:
                raise rc.ReleaseError("the written signature does not decode to 64 bytes")
            check_der = temp_dir / "check.der"
            check_der.write_bytes(encode_der_signature(int.from_bytes(decoded[:32], "big"),
                                                       int.from_bytes(decoded[32:], "big")))
            verified = run_openssl(openssl, ["dgst", "-sha256", "-verify", str(public_pem),
                                             "-signature", str(check_der), str(args.manifest)],
                                   "verify the signature")
            if b"Verified OK" not in verified.stdout:
                raise rc.ReleaseError("OpenSSL did not confirm the signature")
    except (rc.ReleaseError, OSError, ValueError) as error:
        if wrote_output:  # never leave an unverified signature behind
            try:
                output.unlink()
            except OSError:
                pass
        reason = rc.safe_error(error)
        print(f"sign_manifest: error: {reason}", file=sys.stderr)
        return 1

    # Only a short fingerprint of the PUBLIC key is shown, to tell keys apart during a rotation.
    fingerprint = hashlib.sha256(bytes.fromhex(public_hex)).hexdigest()[:16]
    print(f"signature written: {output.name}")
    print(f"verified with the public key whose SHA-256 fingerprint starts with {fingerprint}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))

# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Ibrahim Bouillet
"""Tests for the release scripts (standard library unittest).

Run from the repository root:
    python -m unittest discover -s scripts/release -p "test_*.py" -v

Signing tests need the OpenSSL CLI (AETHER_OPENSSL, PATH, or Git for Windows'
copy) and are skipped without it. Every key is a throw-away TEST key created in
a temporary directory that is deleted afterwards.
"""

from __future__ import annotations

import base64
import hashlib
import json
import os
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))

import make_checksums  # noqa: E402
import make_manifest  # noqa: E402
import release_common as rc  # noqa: E402
import sign_manifest  # noqa: E402


def find_openssl() -> str | None:
    for candidate in (os.environ.get("AETHER_OPENSSL"), shutil.which("openssl"),
                      r"C:\Program Files\Git\usr\bin\openssl.exe"):
        if candidate and Path(candidate).is_file():
            return candidate
    return None


OPENSSL = find_openssl()


def run_script(name: str, *arguments: str, env: dict[str, str] | None = None) -> subprocess.CompletedProcess:
    return subprocess.run([sys.executable, str(HERE / name), *arguments], capture_output=True, text=True,
                          stdin=subprocess.DEVNULL, env=env, check=False)


def write_installer(folder: Path, version: str, architecture: str, size: int, seed: int) -> Path:
    path = folder / rc.installer_name(version, architecture)
    path.write_bytes(bytes(((i * 131 + seed * 17 + (i >> 8)) & 0xFF) for i in range(size)))
    return path


class TemporaryFolder(unittest.TestCase):
    def setUp(self) -> None:
        self._temp = tempfile.TemporaryDirectory(prefix="aether-release-tests-")
        self.folder = Path(self._temp.name)

    def tearDown(self) -> None:
        self._temp.cleanup()

    def assert_location_absent(self, result: subprocess.CompletedProcess, location: Path) -> None:
        output = result.stdout + result.stderr
        self.assertNotIn(str(location), output)
        self.assertNotIn(location.as_posix(), output)
        self.assertNotIn(location.name, output)


class DiagnosticsTests(TemporaryFolder):
    def test_checksum_encoding_errors_do_not_produce_a_path_bearing_traceback(self) -> None:
        path = self.folder / "synthetic-\u00e9.bin"
        path.write_bytes(b"synthetic public fixture")
        result = run_script("make_checksums.py", "--version", "1.2.0", str(path))
        self.assertNotEqual(result.returncode, 0)
        self.assert_location_absent(result, self.folder)
        self.assertNotIn("Traceback", result.stderr)
        self.assertIn("checksum format", result.stderr)

    def test_invalid_arguments_never_echo_private_values(self) -> None:
        location = self.folder / "SYNTHETIC_PRIVATE_ARGUMENT_LOCATION"
        cases = (
            ("make_manifest.py", "--version", "1.2.0", "--channel", str(location), "asset.exe"),
            ("make_checksums.py", "--version", "1.2.0", "--unknown=" + str(location), "asset.exe"),
            ("sign_manifest.py", "manifest.json", "--unknown=" + str(location)),
            ("make_checksums.py", "--version", str(location), "asset.exe"),
        )
        for name, *arguments in cases:
            with self.subTest(script=name, arguments=arguments[:2]):
                result = run_script(name, *arguments)
                self.assertNotEqual(result.returncode, 0)
                self.assert_location_absent(result, location)
                self.assertNotIn("Traceback", result.stderr)


class CommonTests(unittest.TestCase):
    def test_semver_matches_the_cpp_rules(self) -> None:
        for good in ("0.0.0", "1.2.3", "1.0.0-alpha.1", "1.0.0-0A.is.legal", "1.0.0+001", "2.0.0-rc.1+build.5"):
            rc.parse_semver(good)
        for bad in ("v1.2.3", "1.2", "01.2.3", "1.2.3-01", "1.2.3-", "1.2.3+", "1.2.3-a..b", " 1.2.3",
                    "1.2.3\n", "18446744073709551616.0.0", "<version>"):
            with self.assertRaises(rc.ReleaseError, msg=bad):
                rc.parse_semver(bad)
        self.assertEqual(rc.expected_channel("1.2.3"), "stable")
        self.assertEqual(rc.expected_channel("1.2.3-beta.1"), "prerelease")
        self.assertEqual(rc.expected_channel("1.2.3+build"), "stable")

    def test_placeholder_digests_mirror_the_cpp_rules(self) -> None:
        for placeholder in ("0" * 64, "f" * 64, "ab" * 32, "deadbeef" * 8, "0123456789abcdef" * 4,
                            hashlib.sha256(b"").hexdigest()):
            self.assertTrue(rc.is_placeholder_digest(placeholder), placeholder)
        for real in (hashlib.sha256(b"abc").hexdigest(), hashlib.sha256(b"AetherShell").hexdigest()):
            self.assertFalse(rc.is_placeholder_digest(real))
        self.assertTrue(rc.is_sha256_hex(hashlib.sha256(b"x").hexdigest()))
        self.assertFalse(rc.is_sha256_hex("A" * 64))
        self.assertFalse(rc.is_sha256_hex("<release-generated-digest>"))

    def test_release_notes_url_rule_mirrors_the_cpp_rule(self) -> None:
        # Same cases as "github: repository URL checks are exact" (GitHubReleaseClientTests.cpp).
        for good in ("https://github.com/ibrahimbouillet/AetherShell/releases/tag/v1.2.0",
                     "https://github.com/IbrahimBouillet/aethershell/releases/tag/v1.2.0",
                     "https://github.com/ibrahimbouillet/AetherShell/releases/tag/v1.2.0+b.1/",
                     "https://github.com/ibrahimbouillet/AetherShell/blob/main/CHANGELOG.md"):
            self.assertTrue(rc.is_repository_web_url(good, "ibrahimbouillet", "AetherShell"), good)
        for bad in ("https://github.com/ibrahimbouillet/AetherShell",
                    "https://github.com/ibrahimbouillet/AetherShell/",
                    "https://github.com/ibrahimbouillet/AetherShellX/releases",
                    "https://github.com/ibrahimbouillet/AetherShell/releases?x=1",
                    "https://github.com/ibrahimbouillet/AetherShell/releases/tag/v1.2.0#changes",
                    "https://github.com/ibrahimbouillet/AetherShell//releases",
                    "https://github.com/ibrahimbouillet/AetherShell/releases/../../x",
                    "https://github.com/ibrahimbouillet/AetherShell/releases/tag/v1 2",
                    "http://github.com/ibrahimbouillet/AetherShell/releases",
                    "https://github.com.evil.example/ibrahimbouillet/AetherShell/releases",
                    "https://github.com/someone-else/AetherShell/releases",
                    "https://github.com/ibrahimbouillet/AetherShell/releases/tag/v1.2.0\n"):
            self.assertFalse(rc.is_repository_web_url(bad, "ibrahimbouillet", "AetherShell"), bad)

    def test_length_limits_mirror_the_cpp_rules(self) -> None:
        with self.assertRaisesRegex(rc.ReleaseError, "longer than 256"):
            rc.parse_semver("1.0.0-" + "a" * 300)  # kMaxSemanticVersionLength
        # A valid but long version whose asset names exceed kMaxAssetFileNameLength (200).
        long_version = "1.2.0-" + "a" * 170
        with self.assertRaisesRegex(rc.ReleaseError, "too long"):
            make_manifest.build_manifest(long_version, "prerelease",
                                         [Path(rc.installer_name(long_version, "x64"))], 19041,
                                         f"https://github.com/ibrahimbouillet/AetherShell/releases/tag/v{long_version}",
                                         "ibrahimbouillet", "AetherShell")

    def test_asset_names_follow_the_convention(self) -> None:
        self.assertEqual(rc.installer_name("1.2.0", "arm64"), "AetherShell-1.2.0-win-arm64-setup.exe")
        self.assertEqual(rc.manifest_name("1.2.0"), "AetherShell-1.2.0-manifest.json")
        self.assertEqual(rc.signature_name("1.2.0"), "AetherShell-1.2.0-manifest.json.sig")
        self.assertEqual(rc.checksums_name("1.2.0"), "AetherShell-1.2.0-checksums.txt")


class MakeManifestTests(TemporaryFolder):
    def test_success_missing_inputs_and_file_errors_omit_private_locations(self) -> None:
        location = self.folder / "SYNTHETIC_MANIFEST_LOCATION"
        location.mkdir()
        asset = write_installer(location, "1.2.0", "x64", 100, 1)
        arguments = ("--version", "1.2.0", "--channel", "stable")
        successful = run_script("make_manifest.py", *arguments, str(asset))
        self.assertEqual(successful.returncode, 0, successful.stderr)
        self.assertIn("manifest written: AetherShell-1.2.0-manifest.json", successful.stdout)
        self.assert_location_absent(successful, location)
        missing = run_script("make_manifest.py", *arguments,
                             str(location / rc.installer_name("1.2.0", "arm64")))
        self.assertNotEqual(missing.returncode, 0)
        self.assert_location_absent(missing, location)
        blocker = location / "blocked-output"
        blocker.write_bytes(b"synthetic file prevents directory creation")
        failed = run_script("make_manifest.py", *arguments, "--output",
                            str(blocker / rc.manifest_name("1.2.0")), str(asset))
        self.assertNotEqual(failed.returncode, 0)
        self.assertIn("file operation failed", failed.stderr)
        self.assert_location_absent(failed, location)

    def test_builds_a_manifest_from_the_real_files(self) -> None:
        x64 = write_installer(self.folder, "1.2.0", "x64", 5000, 1)
        arm64 = write_installer(self.folder, "1.2.0", "arm64", 7000, 2)
        result = run_script("make_manifest.py", "--version", "1.2.0", "--channel", "stable",
                            "--minimum-windows-build", "22000", str(x64), str(arm64))
        self.assertEqual(result.returncode, 0, result.stderr)
        manifest_path = self.folder / "AetherShell-1.2.0-manifest.json"
        data = manifest_path.read_bytes()
        self.assertTrue(data.endswith(b"\n"))
        manifest = json.loads(data)
        self.assertEqual(list(manifest), ["schemaVersion", "product", "version", "channel", "minimumWindowsBuild",
                                          "releaseNotesUrl", "assets"])
        self.assertEqual(manifest["schemaVersion"], 1)
        self.assertEqual(manifest["product"], "AetherShell")
        self.assertEqual(manifest["version"], "1.2.0")
        self.assertEqual(manifest["channel"], "stable")
        self.assertEqual(manifest["minimumWindowsBuild"], 22000)
        self.assertEqual(manifest["releaseNotesUrl"],
                         "https://github.com/ibrahimbouillet/AetherShell/releases/tag/v1.2.0")
        names = [entry["filename"] for entry in manifest["assets"]]
        self.assertEqual(names, sorted(names))
        for entry, path in zip(manifest["assets"], (arm64, x64)):
            self.assertEqual(entry["filename"], path.name)
            self.assertEqual(entry["sha256"], hashlib.sha256(path.read_bytes()).hexdigest())
            self.assertEqual(entry["size"], path.stat().st_size)
            self.assertEqual(entry["format"], "exe")
        self.assertEqual(manifest["assets"][0]["architecture"], "arm64")

    def test_refuses_invalid_input(self) -> None:
        good = write_installer(self.folder, "1.2.0", "x64", 100, 1)
        beta = write_installer(self.folder, "2.0.0-beta.1", "x64", 100, 2)
        misnamed = self.folder / "setup.exe"
        misnamed.write_bytes(b"MZ" * 50)
        empty = write_installer(self.folder, "1.3.0", "x64", 0, 0)
        cases = [
            ("--version", "1.2.0", "--channel", "stable", str(misnamed)),
            ("--version", "1.2.0", "--channel", "stable", str(good), str(good)),
            ("--version", "1.3.0", "--channel", "stable", str(empty)),
            ("--version", "2.0.0-beta.1", "--channel", "stable", str(beta)),
            ("--version", "v1.2.0", "--channel", "stable", str(good)),
            ("--version", "<version>", "--channel", "stable", str(good)),
            ("--version", "1.2.0", "--channel", "stable", "--release-notes-url", "<release-notes-url>", str(good)),
            ("--version", "1.2.0", "--channel", "stable", "--release-notes-url",
             "https://evil.example/ibrahimbouillet/AetherShell/releases/tag/v1.2.0", str(good)),
            # The application would reject these (IsRepositoryWebUrl), so every client would refuse
            # the signed manifest.
            ("--version", "1.2.0", "--channel", "stable", "--release-notes-url",
             "https://github.com/ibrahimbouillet/AetherShell/releases/tag/v1.2.0#whats-new", str(good)),
            ("--version", "1.2.0", "--channel", "stable", "--release-notes-url",
             "https://github.com/ibrahimbouillet/AetherShell/releases?page=2", str(good)),
            ("--version", "1.2.0", "--channel", "stable", "--minimum-windows-build", "0", str(good)),
            ("--version", "1.2.0", "--channel", "stable", "--output", str(self.folder / "manifest.json"), str(good)),
            ("--version", "1.2.0", "--channel", "stable", str(self.folder / "AetherShell-1.2.0-win-x64-setup.exe.missing")),
        ]
        for arguments in cases:
            result = run_script("make_manifest.py", *arguments)
            self.assertNotEqual(result.returncode, 0, arguments)
            self.assertIn("make_manifest: error:", result.stderr)
        self.assertFalse((self.folder / "AetherShell-1.2.0-manifest.json").exists())

    def test_prerelease_manifest(self) -> None:
        beta = write_installer(self.folder, "2.0.0-beta.1", "x64", 100, 2)
        result = run_script("make_manifest.py", "--version", "2.0.0-beta.1", "--channel", "prerelease", str(beta))
        self.assertEqual(result.returncode, 0, result.stderr)
        manifest = json.loads((self.folder / "AetherShell-2.0.0-beta.1-manifest.json").read_text())
        self.assertEqual(manifest["channel"], "prerelease")

    def test_a_plain_version_may_ship_on_the_prerelease_channel(self) -> None:
        # Same rule as UpdateManifest::Parse: only a prerelease VERSION is barred from the stable channel.
        # The GitHub release must then be marked as a prerelease, or the application rejects the manifest.
        candidate = write_installer(self.folder, "1.2.0", "x64", 100, 3)
        result = run_script("make_manifest.py", "--version", "1.2.0", "--channel", "prerelease", str(candidate))
        self.assertEqual(result.returncode, 0, result.stderr)
        manifest = json.loads((self.folder / "AetherShell-1.2.0-manifest.json").read_text())
        self.assertEqual(manifest["channel"], "prerelease")


class MakeChecksumsTests(TemporaryFolder):
    def test_success_missing_inputs_and_file_errors_omit_private_locations(self) -> None:
        location = self.folder / "SYNTHETIC_CHECKSUM_LOCATION"
        location.mkdir()
        asset = write_installer(location, "1.2.0", "x64", 100, 1)
        successful = run_script("make_checksums.py", "--version", "1.2.0", str(asset))
        self.assertEqual(successful.returncode, 0, successful.stderr)
        self.assertIn("checksums written: AetherShell-1.2.0-checksums.txt", successful.stdout)
        self.assert_location_absent(successful, location)
        missing = run_script("make_checksums.py", "--version", "1.2.0", str(location / "missing.exe"))
        self.assertNotEqual(missing.returncode, 0)
        self.assert_location_absent(missing, location)
        blocker = location / "blocked-output"
        blocker.write_bytes(b"synthetic file prevents directory creation")
        failed = run_script("make_checksums.py", "--version", "1.2.0", "--output",
                            str(blocker / rc.checksums_name("1.2.0")), str(asset))
        self.assertNotEqual(failed.returncode, 0)
        self.assertIn("file operation failed", failed.stderr)
        self.assert_location_absent(failed, location)

    def test_writes_sha256sum_compatible_lines(self) -> None:
        a = write_installer(self.folder, "1.2.0", "x64", 300, 1)
        b = write_installer(self.folder, "1.2.0", "arm64", 400, 2)
        result = run_script("make_checksums.py", "--version", "1.2.0", str(a), str(b))
        self.assertEqual(result.returncode, 0, result.stderr)
        output = self.folder / "AetherShell-1.2.0-checksums.txt"
        lines = output.read_bytes().split(b"\n")
        self.assertEqual(lines[-1], b"")  # LF terminated, no CR
        self.assertNotIn(b"\r", output.read_bytes())
        expected = sorted([(b.name, hashlib.sha256(b.read_bytes()).hexdigest()),
                           (a.name, hashlib.sha256(a.read_bytes()).hexdigest())])
        self.assertEqual(lines[:-1], [f"{digest}  {name}".encode() for name, digest in expected])

        sha256sum = shutil.which("sha256sum") or r"C:\Program Files\Git\usr\bin\sha256sum.exe"
        if Path(sha256sum).is_file():
            check = subprocess.run([sha256sum, "-c", output.name], cwd=self.folder, capture_output=True, text=True,
                                   check=False)
            self.assertEqual(check.returncode, 0, check.stdout + check.stderr)

    def test_refuses_bad_input(self) -> None:
        spaced = self.folder / "a file.exe"
        spaced.write_bytes(b"x")
        empty = self.folder / "empty.bin"
        empty.write_bytes(b"")
        for arguments in (("--version", "1.2.0", str(spaced)), ("--version", "1.2.0", str(empty)),
                          ("--version", "nope", str(spaced)),
                          ("--version", "1.2.0", "--output", str(self.folder / "sums.txt"), str(spaced))):
            result = run_script("make_checksums.py", *arguments)
            self.assertNotEqual(result.returncode, 0, arguments)


class DerConversionTests(unittest.TestCase):
    def test_round_trip_and_strictness(self) -> None:
        for r, s in ((1, 1), (2**255, 2**200), (sign_manifest.P256_ORDER - 1, 0x7F)):
            der = sign_manifest.encode_der_signature(r, s)
            self.assertEqual(sign_manifest.parse_der_signature(der), (r, s))
        for bad in (b"", b"\x31\x06\x02\x01\x01\x02\x01\x01", b"\x30\x07\x02\x02\x00\x01\x02\x01\x01",
                    b"\x30\x06\x02\x01\x81\x02\x01\x01", b"\x30\x06\x02\x01\x00\x02\x01\x01",
                    b"\x30\x06\x02\x01\x01\x02\x01\x01\x00", sign_manifest.encode_der_signature(1, sign_manifest.P256_ORDER)):
            with self.assertRaises(rc.ReleaseError):
                sign_manifest.parse_der_signature(bad)


@unittest.skipIf(OPENSSL is None, "the OpenSSL CLI is not available")
class SignManifestTests(TemporaryFolder):
    def setUp(self) -> None:
        super().setUp()
        self.key = self.folder / "test-key.pem"
        subprocess.run([OPENSSL, "ecparam", "-name", "prime256v1", "-genkey", "-noout", "-out", str(self.key)],
                       check=True, capture_output=True)
        der = subprocess.run([OPENSSL, "ec", "-in", str(self.key), "-outform", "DER"], check=True,
                             capture_output=True).stdout
        # SEC1 ECPrivateKey: 30 77 02 01 01 04 20 <32-byte private scalar> ...
        self.private_scalar = der[7:39]
        installer = write_installer(self.folder, "1.2.0", "x64", 1000, 1)
        result = run_script("make_manifest.py", "--version", "1.2.0", "--channel", "stable", str(installer))
        self.assertEqual(result.returncode, 0, result.stderr)
        self.manifest = self.folder / "AetherShell-1.2.0-manifest.json"
        self.signature = self.folder / "AetherShell-1.2.0-manifest.json.sig"

    def public_pem(self) -> Path:
        path = self.folder / "public.pem"
        subprocess.run([OPENSSL, "pkey", "-in", str(self.key), "-pubout", "-out", str(path)], check=True,
                       capture_output=True)
        return path

    def assert_no_key_material(self, text: str) -> None:
        self.assertNotIn("PRIVATE KEY", text)
        self.assertNotIn(self.private_scalar.hex(), text.lower())
        self.assertNotIn(base64.b64encode(self.private_scalar).decode(), text)

    def test_signs_with_p1363_and_the_signature_verifies(self) -> None:
        result = run_script("sign_manifest.py", str(self.manifest), "--key", str(self.key), "--openssl", OPENSSL,
                            "--allow-untrusted-key")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assert_no_key_material(result.stdout + result.stderr)
        self.assert_location_absent(result, self.folder)
        text = self.signature.read_bytes()
        self.assertTrue(text.endswith(b"\n"))
        self.assertEqual(text.count(b"\n"), 1)
        raw = base64.b64decode(text.strip(), validate=True)
        self.assertEqual(len(raw), 64)

        # Independent check with OpenSSL: rebuild DER from r || s and verify the exact bytes.
        der = self.folder / "check.der"
        der.write_bytes(sign_manifest.encode_der_signature(int.from_bytes(raw[:32], "big"),
                                                           int.from_bytes(raw[32:], "big")))
        verify = subprocess.run([OPENSSL, "dgst", "-sha256", "-verify", str(self.public_pem()), "-signature",
                                 str(der), str(self.manifest)], capture_output=True, text=True, check=False)
        self.assertIn("Verified OK", verify.stdout)

        # One changed byte in the manifest breaks the signature.
        tampered = self.folder / "tampered.json"
        tampered.write_bytes(self.manifest.read_bytes().replace(b"1000", b"1001"))
        verify = subprocess.run([OPENSSL, "dgst", "-sha256", "-verify", str(self.public_pem()), "-signature",
                                 str(der), str(tampered)], capture_output=True, text=True, check=False)
        self.assertNotIn("Verified OK", verify.stdout)

    def test_key_path_from_the_environment(self) -> None:
        env = dict(os.environ, AETHER_RELEASE_SIGNING_KEY_FILE=str(self.key), AETHER_OPENSSL=OPENSSL)
        result = run_script("sign_manifest.py", str(self.manifest), "--allow-untrusted-key", env=env)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assert_no_key_material(result.stdout + result.stderr)
        self.assertTrue(self.signature.is_file())

    def test_refuses_a_key_the_application_does_not_trust(self) -> None:
        result = run_script("sign_manifest.py", str(self.manifest), "--key", str(self.key), "--openssl", OPENSSL)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("not listed", result.stderr)
        self.assert_no_key_material(result.stdout + result.stderr)
        self.assertFalse(self.signature.exists())  # no unverified signature is left behind

    def test_untrusted_header_and_file_errors_omit_private_locations(self) -> None:
        location = self.folder / "SYNTHETIC_SIGNING_LOCATION"
        location.mkdir()
        header = location / "SYNTHETIC_TRUST_HEADER.h"
        header.write_text(sign_manifest.DEFAULT_TRUSTED_KEYS.read_text(encoding="utf-8"), encoding="utf-8")
        untrusted = run_script("sign_manifest.py", str(self.manifest), "--key", str(self.key),
                               "--openssl", OPENSSL, "--trusted-keys", str(header))
        self.assertNotEqual(untrusted.returncode, 0)
        self.assertIn("not listed", untrusted.stderr)
        self.assert_location_absent(untrusted, location)
        self.assert_location_absent(untrusted, self.folder)
        self.assert_no_key_material(untrusted.stdout + untrusted.stderr)
        self.assertFalse(self.signature.exists())
        blocker = location / "blocked-output"
        blocker.write_bytes(b"synthetic file prevents directory creation")
        failed = run_script("sign_manifest.py", str(self.manifest), "--key", str(self.key),
                            "--openssl", OPENSSL, "--allow-untrusted-key", "--output",
                            str(blocker / rc.signature_name("1.2.0")))
        self.assertNotEqual(failed.returncode, 0)
        self.assertIn("file operation failed", failed.stderr)
        self.assert_location_absent(failed, location)
        self.assert_no_key_material(failed.stdout + failed.stderr)

    def test_refuses_missing_keys_and_wrong_files(self) -> None:
        env = {key: value for key, value in os.environ.items() if key != "AETHER_RELEASE_SIGNING_KEY_FILE"}
        no_key = run_script("sign_manifest.py", str(self.manifest), "--openssl", OPENSSL, env=env)
        self.assertNotEqual(no_key.returncode, 0)
        self.assertIn("no signing key", no_key.stderr)

        missing = run_script("sign_manifest.py", str(self.manifest), "--key", str(self.folder / "missing.pem"),
                             "--openssl", OPENSSL, "--allow-untrusted-key")
        self.assertNotEqual(missing.returncode, 0)

        renamed = self.folder / "manifest.json"
        renamed.write_bytes(self.manifest.read_bytes())
        wrong_name = run_script("sign_manifest.py", str(renamed), "--key", str(self.key), "--openssl", OPENSSL,
                                "--allow-untrusted-key")
        self.assertNotEqual(wrong_name.returncode, 0)

        not_manifest = self.folder / "AetherShell-9.9.9-manifest.json"
        not_manifest.write_text('{"hello": "world"}')
        refused = run_script("sign_manifest.py", str(not_manifest), "--key", str(self.key), "--openssl", OPENSSL,
                             "--allow-untrusted-key")
        self.assertNotEqual(refused.returncode, 0)
        self.assertFalse((self.folder / "AetherShell-9.9.9-manifest.json.sig").exists())

    def test_openssl_failure_does_not_print_private_key_location(self) -> None:
        key = self.folder / "SYNTHETIC_KEY_LOCATION_MARKER.pem"
        key.write_text("synthetic invalid key input")
        result = run_script("sign_manifest.py", str(self.manifest), "--key", str(key),
                            "--openssl", OPENSSL)
        self.assertNotEqual(result.returncode, 0)
        self.assertNotIn(key.name, result.stdout + result.stderr)
        self.assertNotIn(str(key.parent), result.stdout + result.stderr)

    def test_trusted_keys_header_is_read(self) -> None:
        keys = sign_manifest.trusted_key_hexes(sign_manifest.DEFAULT_TRUSTED_KEYS)
        self.assertIn("2b0310c62dd1d8ec4e6b16c9caf44fced9f8df43f3807bc9d3e425ca28f4e12f"
                      "66b5ed190dad7d96f79405a82e7f5e60e5caa622a379bc19edfb42c92cb0ce02", keys)


if __name__ == "__main__":
    unittest.main()

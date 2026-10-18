# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Ibrahim Bouillet
"""Synthetic binary/privacy and packaging refusal tests; no product executables are launched."""
from __future__ import annotations

import os
import shutil
import subprocess
import sys
import tempfile
import time
import unittest
from pathlib import Path

import audit_package

HERE = Path(__file__).resolve().parent
POWERSHELL = shutil.which("pwsh") or shutil.which("powershell")


def codeview(name: str) -> bytes:
    return b"RSDS" + bytes(16) + (1).to_bytes(4, "little") + name.encode("utf-8") + b"\0"


class PackagePrivacyTests(unittest.TestCase):
    def test_clean_codeview_basename_and_public_attribution_are_allowed(self) -> None:
        payload = codeview("AetherShellApp.pdb") + b"Copyright (c) 2026 Ibrahim Bouillet\0"
        payload += "https://github.com/ibrahimbouillet/AetherShell".encode("utf-16le")
        self.assertEqual(audit_package.audit_bytes(payload, machine="Ibrahim"), set())

    def test_codeview_paths_are_rejected_in_setup_or_embedded_payload(self) -> None:
        for name in (r"D:\synthetic-build\AetherShellApp.pdb", "/synthetic/build/AetherShellApp.pdb"):
            with self.subTest(style="Windows" if ":" in name else "POSIX"):
                # RCDATA stores these bytes verbatim; no executable parsing/loading is needed.
                payload = b"MZ synthetic setup\0RCDATA\0MZ synthetic app\0" + codeview(name)
                self.assertIn("codeview_path", audit_package.audit_bytes(payload))
        ansi = b"RSDS" + bytes(16) + (1).to_bytes(4, "little") + "D:\\synthetic-\u00e9\\app.pdb".encode("cp1252") + b"\0"
        self.assertIn("codeview_path", audit_package.audit_bytes(ansi))

    def test_home_paths_are_rejected_in_ascii_and_utf16(self) -> None:
        for value in (r"C:\Users\SYNTHETIC_ACCOUNT\source", "/home/SYNTHETIC_ACCOUNT/source"):
            for encoding in ("utf-8", "utf-16le"):
                with self.subTest(encoding=encoding):
                    self.assertIn("personal_path", audit_package.audit_bytes(value.encode(encoding)))

    def test_absolute_sources_are_rejected_without_confusing_public_urls(self) -> None:
        for value in (r"D:\synthetic-source\main.cpp", "/workspace/synthetic/main.hpp"):
            for encoding in ("utf-8", "utf-16le"):
                self.assertIn("absolute_source_path", audit_package.audit_bytes(value.encode(encoding)))
        self.assertEqual(audit_package.audit_bytes(b"https://github.com/public/project/blob/main/main.cpp"), set())

    def test_current_roots_and_machine_are_rejected_in_both_encodings(self) -> None:
        for encoding in ("utf-8", "utf-16le"):
            self.assertIn("source_root", audit_package.audit_bytes("R:/SyntheticSource/assets".encode(encoding), source_root=r"R:\SyntheticSource"))
            self.assertIn("personal_root", audit_package.audit_bytes("R:/SyntheticHome/cache".encode(encoding), home=r"R:\SyntheticHome"))
            self.assertIn("machine_name", audit_package.audit_bytes("SYNTHETIC-BUILD-HOST".encode(encoding), machine="SYNTHETIC-BUILD-HOST"))
            self.assertIn("machine_name", audit_package.audit_bytes("\u00c9-HOST-SYNTHETIC".encode(encoding), machine="\u00c9-HOST-SYNTHETIC"))

    def test_source_freshness_refuses_newer_product_inputs(self) -> None:
        with tempfile.TemporaryDirectory(prefix="aether-package-freshness-") as temporary:
            root = Path(temporary)
            source = root / "src"
            source.mkdir()
            file = source / "main.cpp"
            file.write_text("// synthetic product input")
            package = root / "setup.exe"
            package.write_bytes(codeview("AetherShell.pdb"))
            os.utime(source, ns=(1_000_000_000, 1_000_000_000))
            os.utime(file, ns=(1_000_000_000, 1_000_000_000))
            os.utime(package, ns=(2_000_000_000, 2_000_000_000))
            self.assertFalse(audit_package.source_is_newer(package, root))
            os.utime(file, ns=(3_000_000_000, 3_000_000_000))
            self.assertTrue(audit_package.source_is_newer(package, root))

    def test_audit_cli_never_prints_matched_private_values_or_input_locations(self) -> None:
        with tempfile.TemporaryDirectory(prefix="aether-package-diagnostics-") as temporary:
            root = Path(temporary)
            package = root / "SYNTHETIC_INPUT_LOCATION.exe"
            package.write_bytes(codeview(r"D:\SYNTHETIC_PRIVATE_LOCATION\product.pdb"))
            result = subprocess.run([sys.executable, str(HERE / "audit_package.py"), str(package)],
                                    capture_output=True, text=True, check=False)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("codeview_path", result.stderr)
            self.assertNotIn("SYNTHETIC_PRIVATE_LOCATION", result.stdout + result.stderr)
            self.assertNotIn("SYNTHETIC_INPUT_LOCATION", result.stdout + result.stderr)
            self.assertNotIn(str(root), result.stdout + result.stderr)

    @unittest.skipIf(POWERSHELL is None, "PowerShell is unavailable")
    def test_release_skip_build_audits_then_packages_only_unsigned_manual_install_artifacts(self) -> None:
        with tempfile.TemporaryDirectory(prefix="aether-package-release-") as temporary:
            root = Path(temporary)
            build, output = root / "SYNTHETIC_BUILD_LOCATION", root / "public-assets"
            (build / "generated" / "aether").mkdir(parents=True)
            (build / "modules" / "installer").mkdir(parents=True)
            (build / "CMakeCache.txt").write_text("CMAKE_BUILD_TYPE:STRING=Release\n")
            (build / "generated" / "aether" / "BuildInfo.h").write_text('kVersion = "1.2.0";\nkTargetArchitecture = "x64";\n')
            setup = build / "modules" / "installer" / "AetherShellSetup.exe"
            setup.write_bytes(b"MZ synthetic package for privacy only\0" + codeview("AetherShell.pdb"))
            # Deterministic freshness fixture while unrelated product development continues.
            future = time.time_ns() + 3_600_000_000_000
            os.utime(setup, ns=(future, future))
            environment = {key: value for key, value in os.environ.items()
                           if key not in ("AETHER_RELEASE_SIGNING_KEY_FILE", "AETHER_RELEASE_SIGNING_KEY_PASSPHRASE")}
            result = subprocess.run([POWERSHELL, "-NoProfile", "-NonInteractive", "-ExecutionPolicy", "Bypass",
                                     "-File", str(HERE.parent / "package.ps1"), "-SkipBuild",
                                     "-BuildDir", str(build), "-OutputDir", str(output)],
                                    capture_output=True, text=True, check=False, env=environment)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual((output / "AetherShell-1.2.0-win-x64-setup.exe").read_bytes(), setup.read_bytes())
            checksums = (output / "AetherShell-1.2.0-checksums.txt").read_text()
            self.assertIn("AetherShell-1.2.0-win-x64-setup.exe", checksums)
            self.assertNotIn("manifest", checksums)
            self.assertFalse((output / "AetherShell-1.2.0-manifest.json.sig").exists())
            self.assertNotIn(str(root), result.stdout + result.stderr)

    @unittest.skipIf(POWERSHELL is None, "PowerShell is unavailable")
    def test_skip_build_rejects_debug_before_creating_public_assets(self) -> None:
        with tempfile.TemporaryDirectory(prefix="aether-package-debug-") as temporary:
            root = Path(temporary)
            build, output = root / "SYNTHETIC_BUILD_LOCATION", root / "public-assets"
            build.mkdir()
            (build / "CMakeCache.txt").write_text("CMAKE_BUILD_TYPE:STRING=Debug\n")
            environment = {key: value for key, value in os.environ.items()
                           if key not in ("AETHER_RELEASE_SIGNING_KEY_FILE", "AETHER_RELEASE_SIGNING_KEY_PASSPHRASE")}
            result = subprocess.run([POWERSHELL, "-NoProfile", "-NonInteractive", "-ExecutionPolicy", "Bypass",
                                     "-File", str(HERE.parent / "package.ps1"), "-SkipBuild",
                                     "-BuildDir", str(build), "-OutputDir", str(output)],
                                    capture_output=True, text=True, check=False, env=environment)
            self.assertNotEqual(result.returncode, 0)
            self.assertFalse(output.exists())
            self.assertNotIn(str(root), result.stdout + result.stderr)
            self.assertNotIn("SYNTHETIC_BUILD_LOCATION", result.stdout + result.stderr)


if __name__ == "__main__":
    unittest.main()

"""Prüft BSSID-Filter, gespeicherte Einstellungen und die Übernahme alter NVS-Daten."""

from pathlib import Path
import subprocess
import tempfile
import unittest


class WifiPolicyTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory()
        cls.exe = str(Path(cls.temp.name) / "wifi-policy-test")
        root = Path(__file__).resolve().parents[1]
        subprocess.run(
            [
                "g++",
                "-std=c++17",
                "-Wall",
                "-Wextra",
                "-Werror",
                "-I",
                str(root / "src"),
                str(root / "tests/test_wifi_policy.cpp"),
                "-o",
                cls.exe,
            ],
            check=True,
        )

    @classmethod
    def tearDownClass(cls):
        cls.temp.cleanup()

    def check_case(self, case):
        subprocess.run([self.exe, case], check=True)

    def test_bssid_format_and_invalid_addresses(self):
        self.check_case("bssid")

    def test_legacy_credentials_migrate_to_automatic(self):
        self.check_case("legacy")

    def test_fixed_binding_survives_storage_reboot_and_retries(self):
        self.check_case("persist")

    def test_automatic_clears_previous_binding(self):
        self.check_case("automatic")

    def test_damaged_binding_fails_closed(self):
        self.check_case("reject")

    def test_maximum_lengths_and_fallback_hotspot(self):
        self.check_case("bounds")

"""Prüft die neuen Projektwerkzeuge ohne Bus und ohne Zugriff auf GitHub."""

import importlib.util
import json
import os
from pathlib import Path
import re
import sys
import tempfile
import unittest
from unittest.mock import patch
import zipfile

ROOT = Path(__file__).resolve().parents[1]


def load(name):
    spec = importlib.util.spec_from_file_location(name, ROOT / "tools" / f"{name}.py")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


class RepositoryTests(unittest.TestCase):
    def test_local_git_preparation_uses_configured_identity_and_excludes_secrets(self):
        module = load("github_vorbereiten")
        with tempfile.TemporaryDirectory() as temp:
            module.ROOT = Path(temp)
            manifest = module.ROOT / "home_assistant/custom_components/probusx/manifest.json"
            manifest.parent.mkdir(parents=True)
            manifest.write_text('{"domain":"probusx"}')
            (module.ROOT / "README.md").write_text("Lokales Prüfprojekt")
            (module.ROOT / ".gitignore").write_text(".env\n*.pbxs\n")
            (module.ROOT / ".env").write_text("TEST_PASSWORT=nicht_einchecken")
            (module.ROOT / "privat.pbxs").write_bytes(b"Private Testaufnahme")
            env = {
                "GIT_CONFIG_COUNT": "2",
                "GIT_CONFIG_KEY_0": "user.name",
                "GIT_CONFIG_VALUE_0": "ProBusX-Test",
                "GIT_CONFIG_KEY_1": "user.email",
                "GIT_CONFIG_VALUE_1": "test@example.invalid",
            }
            url = "https://github.com/beispiel/probusx.git"
            with (
                patch.dict(os.environ, env),
                patch.object(sys, "argv", ["github_vorbereiten.py", url]),
            ):
                self.assertEqual(module.main(), 0)
                self.assertEqual(module.git("branch", "--show-current").stdout.strip(), "main")
                self.assertEqual(module.git("remote", "get-url", "origin").stdout.strip(), url)
                self.assertEqual(module.git("status", "--porcelain").stdout, "")
                tracked = module.git("ls-files").stdout
                self.assertNotIn(".env", tracked)
                self.assertNotIn("privat.pbxs", tracked)
                self.assertEqual(
                    json.loads(manifest.read_text())["documentation"],
                    "https://github.com/beispiel/probusx#readme",
                )
                with self.assertRaises(ValueError):
                    module.main()

    def test_local_documentation_links_exist(self):
        for document in ROOT.rglob("*.md"):
            if any(part in {"node_modules", ".pio", "dist", ".git"} for part in document.parts):
                continue
            for target in re.findall(
                r"\[[^\]]*\]\(([^)]+)\)", document.read_text(encoding="utf-8")
            ):
                if "://" in target or target.startswith("#"):
                    continue
                target = target.split("#", 1)[0]
                self.assertTrue((document.parent / target).exists(), f"{document}: {target}")

    def test_github_url_accepts_https_and_ssh_without_shell(self):
        module = load("github_vorbereiten")
        for url in (
            "https://github.com/beispiel/probusx.git",
            "git@github.com:beispiel/probusx.git",
        ):
            self.assertEqual(module.repository_url(url)[1], "https://github.com/beispiel/probusx")
        for url in (
            "https://anderer-server.test/user/projekt",
            "--upload-pack=programm",
            "https://github.com/a/../x",
        ):
            with self.assertRaises(ValueError):
                module.repository_url(url)

    def test_package_excludes_credentials_captures_and_build_data(self):
        module = load("paket")
        with tempfile.TemporaryDirectory() as temp:
            module.ROOT = Path(temp)
            files = [
                "README.md",
                ".env",
                "privat.pbxs",
                ".git/config",
                "firmware/.pio/build/datei",
                "firmware/src/main.cpp",
                "tests/fixtures/hauptwerte.bin",
                "aufnahme01.bin",
                "firmware/abbilder/firmware.bin",
                "firmware/abbilder/README.md",
            ]
            for name in files:
                path = module.ROOT / name
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_bytes(b"Pruefdaten")
            selected = [name for _, name in module.source_files()]
            self.assertEqual(
                set(selected),
                {
                    "README.md",
                    "firmware/src/main.cpp",
                    "tests/fixtures/hauptwerte.bin",
                    "firmware/abbilder/README.md",
                },
            )
            first = module.ROOT / "eins.zip"
            second = module.ROOT / "zwei.zip"
            entries = [(module.ROOT / name, name) for name in selected]
            module.archive(first, entries)
            module.archive(second, entries)
            self.assertEqual(first.read_bytes(), second.read_bytes())
            with zipfile.ZipFile(first) as archive:
                self.assertEqual(set(archive.namelist()), set(selected))

    def test_version_and_german_strings(self):
        manifest = json.loads(
            (ROOT / "home_assistant/custom_components/probusx/manifest.json").read_text()
        )
        self.assertEqual(manifest["version"], "0.3.1")
        self.assertEqual((ROOT / "firmware/version.txt").read_text().strip(), "3.2.1")
        base = ROOT / "home_assistant/custom_components/probusx"
        self.assertEqual(
            json.loads((base / "strings.json").read_text()),
            json.loads((base / "translations/de.json").read_text()),
        )


if __name__ == "__main__":
    unittest.main()

"""Erstellt Quellpaket, HA-Installationspaket und Firmwarepaket mit Prüfsummen."""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import zipfile

ROOT = Path(__file__).resolve().parents[1]
SKIP = {
    ".git",
    ".pio",
    "node_modules",
    "__pycache__",
    "dist",
    "test-ergebnisse",
    ".vscode",
    ".idea",
}


def source_files():
    """Schließt lokale Zugangsdaten, Buildreste und private Aufnahmen aus."""
    for path in sorted(ROOT.rglob("*")):
        if not path.is_file():
            continue
        rel = path.relative_to(ROOT)
        if any(p in SKIP or p.startswith(".venv") for p in rel.parts):
            continue
        if path.name.startswith(".env") or path.suffix in {
            ".pbxs",
            ".jsonl",
            ".log",
            ".pyc",
            ".timed",
        }:
            continue
        if rel.as_posix().startswith("firmware/abbilder/") and path.name != "README.md":
            continue
        if path.name in {
            "sdkconfig.waveshare",
            "sdkconfig.waveshare.old",
            "web_assets.h",
            "Bedienfeld_Vorschau.png",
        }:
            continue
        if path.suffix == ".bin" and rel.parts[:2] != ("tests", "fixtures"):
            continue
        yield path, rel.as_posix()


def archive(path: Path, entries) -> None:
    """Verwendet feste ZIP-Zeitstempel und eine sortierte Reihenfolge."""
    with zipfile.ZipFile(path, "w", compression=zipfile.ZIP_DEFLATED, compresslevel=9) as out:
        for file, name in sorted(entries, key=lambda entry: entry[1]):
            info = zipfile.ZipInfo(name, date_time=(2026, 1, 1, 0, 0, 0))
            info.compress_type = zipfile.ZIP_DEFLATED
            info.external_attr = 0o100644 << 16
            out.writestr(info, file.read_bytes())
    print(f"Paket erstellt: {path.name}")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--nur-quellen", action="store_true", help="Ohne Firmwareabbilder verpacken"
    )
    args = parser.parse_args()
    ha_version = json.loads(
        (ROOT / "home_assistant/custom_components/probusx/manifest.json").read_text()
    )["version"]
    version = (ROOT / "VERSION").read_text(encoding="utf-8").strip()
    dist = ROOT / "dist"
    dist.mkdir(exist_ok=True)
    binaries = ROOT / "firmware/abbilder"
    required = [
        "probusx-bridge-waveshare-ota.bin",
        "probusx-bridge-waveshare-usb.bin",
        "SHA256SUMS",
        "sdkconfig.used",
    ]
    if not args.nur_quellen:
        for name in required:
            if not (binaries / name).is_file():
                parser.error(f"Firmwaredatei fehlt: {name}. Zuerst die Firmware bauen und prüfen.")
    paths = []
    sources = list(source_files())
    path = dist / f"probusx-bridge-quellen-{version}.zip"
    archive(path, [(file, "probusx-bridge/" + name) for file, name in sources])
    paths.append(path)
    entries = []
    for file, name in sources:
        if name.startswith("home_assistant/"):
            entries.append((file, name.removeprefix("home_assistant/")))
    entries.extend(
        [
            (ROOT / "docs/Home-Assistant.md", "INSTALLATION.md"),
            (ROOT / "docs/Diagnose.md", "Diagnose.md"),
            (ROOT / "docs/MQTT.md", "MQTT.md"),
            (ROOT / "LIZENZ.md", "LIZENZ.md"),
        ]
    )
    path = dist / f"probusx-home-assistant-legacy-{ha_version}.zip"
    archive(path, entries)
    paths.append(path)
    if not args.nur_quellen:
        firmware_version = (ROOT / "firmware/version.txt").read_text().strip()
        path = dist / f"probusx-bridge-firmware-{firmware_version}.zip"
        archive(path, [(binaries / name, name) for name in required])
        paths.append(path)
    (dist / "SHA256SUMS").write_text(
        "".join(
            f"{hashlib.sha256(path.read_bytes()).hexdigest()}  {path.name}\n" for path in paths
        ),
        encoding="utf-8",
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

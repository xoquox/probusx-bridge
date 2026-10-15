"""Verhindert eine Veröffentlichung mit einem falschen Versionstag."""

import json
import os
from pathlib import Path

root = Path(__file__).resolve().parents[1]
version = json.loads((root / "home_assistant/custom_components/probusx/manifest.json").read_text())[
    "version"
]
tag = os.environ.get("VERSION_TAG", "")
if tag != "v" + version:
    raise SystemExit(f"Versionstag stimmt nicht: erwartet v{version}, erhalten {tag!r}")
print(f"Versionstag geprüft: {tag}")

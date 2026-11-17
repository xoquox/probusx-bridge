"""Verhindert eine Veröffentlichung mit einem falschen Versionstag."""

import os
from pathlib import Path

root = Path(__file__).resolve().parents[1]
version = (root / "VERSION").read_text(encoding="utf-8").strip()
tag = os.environ.get("VERSION_TAG", "")
if tag != "v" + version:
    raise SystemExit(f"Versionstag stimmt nicht: erwartet v{version}, erhalten {tag!r}")
print(f"Versionstag geprüft: {tag}")

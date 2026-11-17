"""Bettet HTML, JavaScript und CSS vor dem Firmware-Build ein."""

from pathlib import Path

if "__file__" in globals():
    root = Path(__file__).resolve().parents[1]
else:
    Import("env")  # PlatformIO stellt diese Funktion über SCons bereit.
    root = Path(env.subst("$PROJECT_DIR"))
parts = [
    "// Automatisch erzeugt. HTML, JavaScript und CSS in den jeweiligen Quelldateien ändern.\n#pragma once\n"
]
for name, symbol in [
    ("index.html", "index_start"),
    ("app.js", "js_start"),
    ("schema.js", "schema_js_start"),
    ("history.js", "history_js_start"),
    ("style.css", "css_start"),
]:
    data = (root / "src" / name).read_text(encoding="utf-8")
    assert ')PBXASSET"' not in data
    parts.append(f'inline constexpr char {symbol}[] = R"PBXASSET({data})PBXASSET";\n')
# Binärdateien erhalten eine feste Länge. Kein strlen() für PNG oder ICO verwenden.
for name, symbol in [("logo.png", "logo_data"), ("favicon.png", "favicon_png_data"),
                     ("favicon.ico", "favicon_ico_data")]:
    data = (root / "src/assets" / name).read_bytes()
    content = ",".join(str(byte) for byte in data)
    parts.append(f"inline constexpr unsigned char {symbol}[] = {{{content}}};\n")
output = root / "src/web_assets.h"
text = "".join(parts)
if not output.exists() or output.read_text(encoding="utf-8") != text:
    output.write_text(text, encoding="utf-8")

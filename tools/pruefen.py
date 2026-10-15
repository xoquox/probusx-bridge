"""Führt die Projektprüfungen ohne angeschlossene Busgeräte aus."""

from __future__ import annotations

import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def run(command: list[str], *, env: dict | None = None) -> None:
    """Bricht beim ersten fehlgeschlagenen Prüfschritt ab."""
    print("Prüfschritt: " + " ".join(map(str, command)), flush=True)
    subprocess.run(command, cwd=ROOT, env=env, check=True)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--browser", action="store_true", help="Zusätzlich beide Browserprüfungen ausführen"
    )
    parser.add_argument("--ha", action="store_true", help="Zusätzlich die echte HA-Laufzeit prüfen")
    parser.add_argument(
        "--replay",
        nargs=2,
        metavar=("GRUNDAUFNAHME", "TASTENAUFNAHME"),
        help="Zusätzlich die beiden ursprünglichen Rohströme prüfen",
    )
    parser.add_argument(
        "--timed",
        nargs=2,
        metavar=("GRUNDZEITEN", "TASTENZEITEN"),
        help="Zusätzlich zeitgestempelte Aufnahmen prüfen",
    )
    args = parser.parse_args()
    compiler = shutil.which("g++")
    if not compiler:
        parser.error("g++ fehlt. Einen C++17-Compiler installieren.")

    env = os.environ.copy()
    env["PYTHONPATH"] = str(ROOT / "firmware") + os.pathsep + env.get("PYTHONPATH", "")
    run([sys.executable, "-m", "unittest", "discover", "-s", "firmware/tests", "-v"], env=env)
    run([sys.executable, "tests/test_codec.py"])
    run([sys.executable, "tests/test_repository.py"])

    with tempfile.TemporaryDirectory(prefix="probusx-tests-") as temp:

        def native(source: str, *arguments: str) -> None:
            exe = str(Path(temp) / Path(source).stem)
            run(
                [
                    compiler,
                    "-std=c++17",
                    "-Wall",
                    "-Wextra",
                    "-Werror",
                    "-I",
                    str(ROOT / "firmware/src"),
                    source,
                    "-o",
                    exe,
                ]
            )
            run([exe, *map(str, arguments)])

        fixture = str(ROOT / "tests/fixtures/hauptwerte.bin")
        native("tests/protocol_unit.cpp", fixture)
        native("tests/quality_test.cpp", fixture)
        native("tests/options_test.cpp", fixture)
        if args.replay:
            native("tests/protocol_test.cpp", *[str(Path(p).resolve()) for p in args.replay])
        if args.timed:
            native("tests/timing_test.cpp", *[str(Path(p).resolve()) for p in args.timed])

    if args.browser:
        run(["node", "tests/test_panel.cjs"])
        run(["node", "firmware/tests/test_browser.cjs"])
    if args.ha:
        run([sys.executable, "tests/test_ha_runtime.py"])
    print(
        json.dumps(
            {
                "ergebnis": "bestanden",
                "browser": args.browser,
                "home_assistant": args.ha,
                "originalaufnahmen": bool(args.replay),
            },
            ensure_ascii=False,
            indent=2,
        )
    )
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except subprocess.CalledProcessError as exc:
        print(f"Prüfung fehlgeschlagen. Rückgabewert: {exc.returncode}", file=sys.stderr)
        raise SystemExit(exc.returncode)

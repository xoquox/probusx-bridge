"""Bereitet den ersten lokalen Git-Commit vor. Führt keinen Push aus."""

from __future__ import annotations

import argparse
import json
from pathlib import Path
import re
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[1]


def repository_url(value: str) -> tuple[str, str]:
    """Akzeptiert GitHub-HTTPS oder GitHub-SSH und liefert die öffentliche Webadresse."""
    match = re.fullmatch(
        r"(?:https://github\.com/|git@github\.com:)([A-Za-z0-9_.-]+)/([A-Za-z0-9_.-]+?)(?:\.git)?/?",
        value,
    )
    if not match:
        raise ValueError("Eine GitHub-Repository-URL mit Kontoname und Projektname eingeben.")
    owner, name = match.groups()
    if owner in {".", ".."} or name in {".", ".."}:
        raise ValueError("Ungültige Repository-Adresse.")
    return value, f"https://github.com/{owner}/{name}"


def git(*args: str, check: bool = True) -> subprocess.CompletedProcess:
    """Übergibt alle Argumente direkt an Git. Verwendet keine Shell."""
    return subprocess.run(["git", *args], cwd=ROOT, text=True, capture_output=True, check=check)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("url", help="Adresse des leeren GitHub-Repositorys")
    parser.add_argument(
        "--nur-pruefen", action="store_true", help="Nur URL prüfen; keine Datei ändern"
    )
    args = parser.parse_args()
    remote, web = repository_url(args.url)
    if args.nur_pruefen:
        print(f"Repository-Adresse gültig: {web}")
        return 0
    if not shutil.which("git"):
        raise ValueError("Git fehlt. Zuerst Git installieren.")
    if (ROOT / ".git").exists():
        raise ValueError(
            "Ein Git-Repository ist bereits vorhanden. Bestehende Git-Einstellungen selbst weiterverwenden."
        )
    for key in ("user.name", "user.email"):
        if not git("config", "--get", key, check=False).stdout.strip():
            raise ValueError(
                f"Git-Einstellung {key} fehlt. Zuerst die eigene Git-Identität einrichten."
            )

    # URLs erst nach allen Vorprüfungen ändern. Anmeldedaten werden nicht angefasst.
    path = ROOT / "home_assistant/custom_components/probusx/manifest.json"
    manifest = json.loads(path.read_text(encoding="utf-8"))
    manifest["documentation"] = web + "#readme"
    manifest["issue_tracker"] = web + "/issues"
    path.write_text(json.dumps(manifest, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    git("init", "--initial-branch=main")
    git("remote", "add", "origin", remote)
    git("add", "--all")
    git("commit", "-m", "ProBusX Bridge: vollständigen Projektstand anlegen")
    print("Projekt vorbereitet. Jetzt ausführen: git push -u origin main")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (ValueError, subprocess.CalledProcessError) as exc:
        raise SystemExit(f"Vorbereitung abgebrochen: {exc}") from exc

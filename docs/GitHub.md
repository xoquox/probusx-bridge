# Auf GitHub laden

## Neues Repository vorbereiten

1. Auf GitHub ein leeres Repository erstellen.
2. Den Namen zum Beispiel auf `probusx-bridge` setzen.
3. Auf GitHub keine zusätzliche README- oder Lizenzdatei erzeugen.
4. Das Projektpaket auf dem PC entpacken.
5. Im entpackten Projektverzeichnis ein Terminal öffnen.

Git muss installiert und die eigene Git-Identität eingerichtet sein.
Falls sie noch fehlt:

```sh
git config --global user.name "DEIN NAME"
git config --global user.email "DEINE GIT-E-MAIL-ADRESSE"
```

Die eigene GitHub-URL an den folgenden Befehl übergeben:

```sh
python tools/github_vorbereiten.py https://github.com/DEIN-KONTO/probusx-bridge.git
```

Das Programm erledigt diese lokalen Schritte:

- Die Dokumentations-URL und Fehlerbericht-URL im HA-Manifest setzen.
- Das Git-Repository mit Hauptzweig `main` anlegen.
- Die Remote-Adresse `origin` setzen.
- Die Projektdateien für den ersten Commit vormerken.
- Den ersten Commit mit der eingerichteten Git-Identität erstellen.

Es überträgt keine Daten an GitHub.
Es speichert keine Zugangsdaten.
Es ersetzt keine vorhandene Git-Konfiguration.

## Hochladen

```sh
git push -u origin main
```

GitHub fordert gegebenenfalls die Anmeldung des PCs an.
Die automatische Prüfung startet nach dem Push.
Unter **Actions** lassen sich Status und Build-Ergebnisse öffnen.

## Veröffentlichung erstellen

Nach erfolgreichen Prüfungen einen Versionstag setzen:

```sh
git tag v0.4.1
git push origin v0.4.1
```

Der Veröffentlichungsworkflow prüft und baut diesen Quellstand erneut.
Er erstellt daraus die Firmware und die Installationspakete.
Die Prüfsummen gehören zu genau diesen Dateien.

## Lizenzstatus

Eine technische Veröffentlichung legt keine freie Nutzungslizenz fest.
Das Paket enthält daher keine stillschweigend gewählte Open-Source-Lizenz.
Der aktuelle Status steht in `LIZENZ.md`.
Vor einer freien Freigabe kann der Rechteinhaber diese Datei durch seine Lizenz ersetzen.

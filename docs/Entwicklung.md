# Entwicklung

## Werkzeuge

| Werkzeug | Festgelegte Fassung |
| --- | --- |
| Python für Firmwarewerkzeuge | Ab 3.10 |
| PlatformIO Core | 6.2.0 |
| PlatformIO-Plattform espressif32 | 6.12.0 |
| ESP-IDF der Plattform | 5.5.0 |
| esptool für die Bilderstellung | 4.9.0 aus PlatformIO |
| C++-Tests | C++17, g++ mit Warnungen als Fehler |
| HA-Prüfumgebung | Python 3.13, Home Assistant 2025.8.3 |
| Browserprüfung | Node.js 22, Playwright 1.51.0 |

Die konkreten Pakete stehen in den Konfigurationsdateien.
Ein Build benötigt beim ersten Aufruf Internetzugriff für die Werkzeuge.

## Firmware unter Linux bauen

Im Projektwurzelverzeichnis ausführen:

```sh
python3 -m venv .venv
.venv/bin/python -m pip install -r tools/requirements-build.txt
.venv/bin/python -m platformio run --project-dir firmware
.venv/bin/python firmware/tools/make_firmware.py
.venv/bin/python firmware/tools/verify_firmware.py
```

Die Abbilder liegen danach unter `firmware/abbilder/`.
Der erste Build lädt Compiler, SDK und Plattformpakete.

## Firmware unter Windows bauen

PowerShell im Projektwurzelverzeichnis öffnen:

```powershell
py -m venv .venv
.venv\Scripts\python.exe -m pip install -r tools/requirements-build.txt
.venv\Scripts\python.exe -m platformio run --project-dir firmware
.venv\Scripts\python.exe firmware/tools/make_firmware.py
.venv\Scripts\python.exe firmware/tools/verify_firmware.py
```

Die Bilderstellung sucht esptool zuerst im Paketverzeichnis von PlatformIO.
Alternativ kann `make_firmware.py --esptool PFAD` einen ausdrücklichen Pfad verwenden.

## Buildkonfiguration

- `platformio.ini` legt Platine und Plattformversion fest.
- `sdkconfig.defaults` enthält PSRAM, USB-Konsole und OTA-Rückfall.
- `partitions.csv` definiert zwei OTA-Partitionen mit je 3 MiB.
- `version.txt` enthält die Firmwareversion.
- `CMakeLists.txt` behält den Projektnamen `pbx_waveshare_ota_v1` bei.

Die Projektkennung gehört zur OTA-Kompatibilitätsprüfung.
Sie darf nicht allein wegen einer Umbenennung des Repositorys geändert werden.
Ebenso muss ein vorhandenes Gerät zum Partitionslayout passen.

`sdkconfig.waveshare` wird beim Build erzeugt.
Für einen sauberen Vergleich dieser erzeugten Datei nicht manuell Einstellungen entnehmen.
Neue Vorgaben stattdessen in `sdkconfig.defaults` eintragen.

## Weboberfläche ändern

1. Die Quelldatei in `firmware/src/` ändern.
2. Die Webdateien einbetten:

```sh
python firmware/tools/embed_assets.py
```

3. Die Browserprüfung ausführen.
4. Die Firmware neu bauen.

PlatformIO führt Schritt 2 auch als Build-Vorlauf aus.
Das generierte `web_assets.h` bleibt außerhalb von Git.

## HA-Integration ändern

Die Python-Dateien liegen unter `home_assistant/custom_components/probusx/`.
Maschinenlesbare API-Felder und vorhandene Entity-IDs nicht für Sprachänderungen umbenennen.
Eine Änderung des Datenschemas muss ausdrücklich versioniert werden.

Die Dialogtexte stehen in `strings.json` und `translations/de.json`.
Beide Dateien enthalten deutsches technisches Vokabular.
Ohne weitere Sprachdatei verwendet HA den Standardtext.

## Versionen ändern

1. Die Firmwareversion in `firmware/version.txt` ändern.
2. Die Integrationsversion in `manifest.json` ändern.
3. Den Versionsabschnitt im README aktualisieren.
4. Die Änderung in `AENDERUNGEN.md` beschreiben.
5. Alle betroffenen Prüfungen ausführen.
6. Die Abbilder aus genau diesem Quellstand erzeugen.

## GitHub-Abläufe

Der Workflow **Prüfungen und Firmware** läuft bei Push und Pull Request.
Er prüft Basistests, HA-Laufzeit, Browser und den Firmware-Build.
Ein erfolgreicher Firmwarejob stellt die Abbilder als Build-Ergebnis bereit.

Der Workflow **Veröffentlichung** wird durch einen Tag wie `v0.3.1` gestartet.
Er führt dieselben Prüfungen aus und baut danach die Veröffentlichungspakete.
Er lädt Quellpaket, HA-Paket, Firmware und Prüfsummen in eine GitHub-Veröffentlichung.
Ein manuell gestarteter Build erstellt keine Veröffentlichung.

## Codeformat

Python nutzt Black mit einer Zeilenlänge von 100 Zeichen.
C++ nutzt die enthaltene `.clang-format`-Datei.
Für Kommentare und Dokumentation gelten die [Schreibregeln](Schreibregeln.md).

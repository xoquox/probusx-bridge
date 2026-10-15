# Prüfungen

## Ohne Hardware prüfen

Linux mit Python und g++ verwenden.
Im Projektwurzelverzeichnis ausführen:

```sh
python tools/pruefen.py
```

Die Basisprüfung umfasst:

- 21 bestehende Aufnahme-, Web- und OTA-Prüfungen.
- PNG-Größe, Bildrichtung und vier Graustufen.
- Temperatur- und Ausgangsskalierung.
- Ungültige Eingangswerte.
- Feste CRC-Referenzen aus den Tastenantworten.
- Telegramme mit wechselnden Eingabeblockgrößen.
- Tastenzustände und Verfall eines noch nicht gesendeten Auftrags.
- 1864 einzelne Bitfehler im Hauptdatenbereich.
- Rückkehr zu gültigen Daten nach einem Fehler.
- Separate Prüfung der AA-Endemarkierung.
- Zeitüberschreitung unvollständiger Telegramme.
- Einmalige Pausenzählung und Wiederaufnahme.
- 60-Sekunden-Fenster und begrenzten Ereignisspeicher.
- Einstellgrenzen und CRC-Prüfung bei ausgeschalteter Analyse.
- Sichere Paketfilter und wiederholbare ZIP-Inhalte.
- GitHub-URL-Prüfung ohne Netzwerkzugriff.

`tests/fixtures/hauptwerte.bin` ist eine synthetische Testdatei.
Sie enthält keine WLAN-Zugangsdaten oder echten Gerätekennungen.
`tests/display.raw` und `tests/display.png` stammen aus der bisherigen Displayprüfung.
Die vollständigen privaten Busaufnahmen gehören nicht zum Git-Repository.

## Ursprüngliche Aufnahmen wiedergeben

Zuerst PBXS-Dateien in Rohdaten umwandeln:

```sh
python firmware/capture.py decode probusx-09d0e9bb.pbxs grund --hex
python firmware/capture.py decode probusx-7b024fb4.pbxs tasten --hex
python tools/pruefen.py --replay grund.bin tasten.bin
```

Die Wiedergabe prüft 1-, 7- und 1024-Byte-Blöcke.
Die Aufnahmen enthalten zusammen:

| Familie | Anzahl |
| --- | ---: |
| Hauptwerte | 360 |
| F1-Werte | 720 |
| Display | 720 |
| Kurzstatus | 2879 |
| Reglerabfragen | 5028 |
| Weitere Kurztelegramme | 720 |
| Tastenantworten | 106 |

4065 Telegramme gehören zu Familien mit bestätigter CRC-Regel.
Alle 4065 bestehen die Prüfung in den ursprünglichen Aufnahmen.

Zeitgestempelte Wiedergabe:

```sh
python tests/make_timed_fixture.py grund.jsonl grund.timed
python tests/make_timed_fixture.py tasten.jsonl tasten.timed
python tools/pruefen.py --timed grund.timed tasten.timed
```

Diese Wiedergabe erkennt auch falsche Pausenalarme durch Empfangsblockgrenzen.

## Browser prüfen

```sh
npm ci
npx playwright install chromium --only-shell
npm test
```

Unter einem frischen Linux benötigt Chromium gegebenenfalls Systembibliotheken.
Der GitHub-Workflow installiert sie mit `--with-deps`.

Die Tests verwenden lokale HTTP-Simulatoren.
Sie ändern keine echten WLAN-Einstellungen.
Sie prüfen Tasten, Kamera, Seitenwechsel, Optionen, Aufnahme und OTA-Fehlerwege.

## Echte HA-Laufzeit prüfen

Eine eigene Umgebung mit Python 3.13 verwenden:

```sh
python3.13 -m venv .venv-ha
.venv-ha/bin/python -m pip install -r tests/requirements-test.txt
.venv-ha/bin/python tests/test_ha_runtime.py
```

Der Test startet Home Assistant 2025.8.3 gegen einen lokalen Gerätesimulator.
Er lädt alle 61 Entities und prüft Kamera, Tasten und Verfügbarkeit.
Er prüft außerdem Token-Maskierung und getrennte Bild- und Statusfehler.
Er setzt keine reale Heizungsanlage voraus.

## Firmware prüfen

```sh
python -m platformio run --project-dir firmware
python firmware/tools/make_firmware.py
python firmware/tools/verify_firmware.py
```

Der Abbildtest prüft unter anderem:

- ESP32-S3-Chipkennung.
- ESP-Segmentprüfsummen und SHA-256.
- Projektkennung und Version.
- Partitionslayout.
- Identische Anwendung im USB- und OTA-Abbild.
- Rücksetzen der NVS- und OTA-Auswahl im USB-Abbild.

Die Abbildprüfung ersetzt keinen Flashversuch am Gerät.

## Ergebnis dieser Fassung

Die erstellten Prüfergebnisse stehen in [PRUEFERGEBNISSE.md](../PRUEFERGEBNISSE.md).
Die GitHub-Abläufe erzeugen bei späteren Änderungen neue Protokolle.
Ein vorhandener Ergebnisbericht gilt nur für den darin genannten Stand.

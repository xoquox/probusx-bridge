# Prüfungen

## Ohne Hardware prüfen

Linux mit Python und g++ verwenden.
Im Projektwurzelverzeichnis ausführen:

```sh
python tools/pruefen.py
```

Die Basisprüfung umfasst:

- 21 bestehende Aufnahme-, Web- und OTA-Prüfungen.
- Sitzungsbereinigung mit den echten HTTP-Callbacks und einer SDK-Testumgebung.
- Neustartsperren, Timerfehler und doppelte Neustartaufträge.
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
- BSSID-Format und ungültige Zieladressen.
- Übernahme alter WLAN-Datensätze ohne Verlust der Zugangsdaten.
- Feste Bindung nach Speichern, Neustart und Wiederverbindung.
- Rückkehr zur automatischen Wahl ohne verbliebenen BSSID-Filter.
- Beschädigte Bindungsdaten ohne automatische Verbindung zu einem anderen Zugangspunkt.

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
Die WLAN-Prüfung zeigt zusätzlich 44 Zugangspunkte ohne SSID-Zusammenfassung.
Sie prüft Auswahl, Speicherung, Neuladen, Zielausfall und aktuelle Verbindungsdaten.

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

## Bedienpause und Neustart ab Firmware 3.3.1

Die Browserprüfung verwendet eine kontrollierte Uhr. Sie überschreitet eine Stunde
auf Reglerseite, Analyseseite und Optionenseite. Danach darf keine automatische
HTTP-Anfrage mehr entstehen. Ein echter Klick setzt die Anzeige wieder fort.
Ein unabhängiger API-Client bleibt erreichbar. Eine laufende Aufnahme verhindert die
Browserpause bis zu ihrem Abschluss. Die Rückfrage des Neustartknopfs, die Sperre bei
Aufnahme oder Startprüfung und die Bestätigung über einen neuen Starttoken werden geprüft.

`test_http_connections.py` kompiliert die produktive Sitzungsbereinigung gegen eine
SDK-Testumgebung. Geprüft werden Zeitgrenze, frische Requests, Schutz während einer
Vorgangsreservierung und ein neuer Zeitstempel bei erneut verwendeter Socketnummer.
Diese Prüfung verwendet keinen echten TCP-Stack.

Die Freigabe von LRU-Sitzungen und der Schutz asynchroner Requests sind zusätzlich im
Quellcode der festgelegten ESP-IDF-Fassung 5.5.0 geprüft. Tests mit echtem VPN, WLAN-Ausfall
und mehreren Browsern auf der Waveshare-Hardware bleiben erforderlich.

## Heizungsschema prüfen

Die native Speicherprüfung unterbricht Lösch- und Schreibschritte gezielt. Sie prüft
Bankwechsel, Wiederanlauf, CRC-Fehler, Größenlimits und getrennte Bild-/Layoutbereiche.
Die Browserprüfung `test_schema_browser.cjs` prüft Upload, Mausbewegung, Beschriftung,
Sichtbarkeit, Skalierung, Live-Werte, erneutes Laden, zweiten Browser, Konflikte und Bedienpause.

Die zusätzliche JSON-Prüfung nutzt cJSON aus derselben SDK-Fassung:

```sh
PROBUSX_IDF_PATH=/pfad/zu/framework-espidf python tools/pruefen.py
```

Ohne diese Variable wird nur diese zusätzliche SDK-Prüfung übersprungen. Die übrigen
Prüfungen bleiben aktiv. Die SDK-Prüfung kompiliert den produktiven Validator mit cJSON.
Ein echter Flash-/WLAN-Dauertest ersetzt die simulierten Tests nicht.

## Native MQTT-Anbindung prüfen

Die ursprüngliche HTTP-Laufzeitprüfung bleibt für den beiliegenden Altbestand erhalten.
Die neue MQTT-Prüfung verwendet HA 2026.11.1 mit Python 3.14.
Unter Debian oder Ubuntu zuerst `mosquitto`, `libcjson-dev`, `libturbojpeg0` und `pkg-config` installieren.

```sh
python3.14 -m venv .venv-mqtt
.venv-mqtt/bin/pip install -r tests/requirements-mqtt-test.txt
.venv-mqtt/bin/python tools/pruefen_mqtt.py --ha
```

Ohne HA genügt `python tools/pruefen_mqtt.py` mit Pillow, g++, gcc und cJSON.
Ist `PROBUSX_IDF_PATH` gesetzt, wird das cJSON des ESP-IDF statt des Systempakets verwendet.
Die Prüfung baut Discovery und PNG aus denselben C++-Headern wie die Firmware.
Sie vergleicht alle 20.480 Bildpixel und prüft NVS-Datensatzregeln, Frischegrenzen,
Verbindungskennungen, Teilnachrichten, gespeicherte Befehle und doppelte Nachrichten.
Die HA-Prüfung verwendet einen echten TCP-Testbroker. Sie prüft 61 Entities, fünf Tasten,
Kameranutzdaten, Haupt- und F1-Verfügbarkeit, Verbindungsausfall und erneutes Laden.
Der Testbroker ist nur lokal unter Port 18883 erreichbar und speichert nichts dauerhaft.
Die Prüfung der Betriebssystem-Netzwerkadapter ist in der HA-Testumgebung ersetzt.
Der MQTT-Client und die MQTT-Plattformen sind unveränderte HA-Komponenten.
Das echte WLAN, der UART und die elektrische RS485-Umschaltung sind nicht Teil dieses Tests.

## Neue F1-Gegenprobe

`tests/flex_values_test.cpp` läuft in der Basisprüfung. Sie prüft die echte Messwertübernahme
für aktive und ausgeschaltete Werte, das Überschreiben durch F0, CRC-Verwerfung und Frische.
`tests/flex_replay_test.cpp` kann zusätzlich mit dem privaten Rohmitschnitt ausgeführt werden:

```sh
g++ -std=c++17 -Wall -Wextra -Werror -I firmware/src tests/flex_replay_test.cpp -o /tmp/probusx-flex-replay
/tmp/probusx-flex-replay /pfad/zum/dekodierten/rohstrom.bin
```

Diese Gegenprobe vergleicht Hauptwerte mit dem vorherigen Kurzstatus bei Blockgrößen von 1, 7 und 1024 Byte.
Die private Aufnahme gehört nicht zum Quellpaket.

## Lokaler Verlauf ab Version 0.6.0

`tests/history_store_test.cpp` prüft die echte Speicherklasse: ungültige Einstellungen,
leerer Speicher, Blöcke freigeben und ergänzen, Ersetzen der ältesten Daten, Min/Max eines
kurzen Ausgangsimpulses, ungültige Temperaturwerte und Zeitraumgrenzen.
`tests/history_fixture.cpp` erzeugt binäre Browserantworten mit derselben Klasse.
`firmware/tests/test_history_browser.cjs` prüft getrennte Live-Tabellen, beide Diagramme,
Kanalwahl mit Browser-Speicherung, Zeitvorgaben, Mauszoom und Rücknahme, überholte HTTP-Antworten,
beschädigte Binärantworten sowie gespeicherte Intervall- und FlexBox-Auswahl.

Die Browserprüfung läuft mit `npm test` oder `python tools/pruefen.py --browser`.
Das Gerät wird dabei nicht angesprochen. Tatsächliche PSRAM-Fragmentierung, Aufnahmestart und
-abbruch sowie ein Neustart auf dem Board brauchen den Gerätetest aus [Verlauf](Verlauf.md).

## Blockierte Verlaufsabrufe ab Version 0.6.1

`firmware/tests/test_history_async.py` übersetzt die tatsächlichen HTTP-Handler gegen eine
SDK-Testumgebung. Die Prüfung zeigt: Der Datenhandler sendet selbst keine Messdaten und
reserviert dort keinen Antwortpuffer. Ein zweiter Abruf erhält sofort HTTP 429. Der Hintergrundtask
hält beim Senden keine Verlaufssperre. Sendeabbruch und Speichermangel geben den Auftrag wieder frei.
Der nächste Abruf funktioniert wieder. Die Sitzungsprüfung schützt zusätzlich einen asynchronen
Verlaufsabruf, auch wenn keine globale Vorgangsreservierung besteht.

`tests/history_transfer_test.cpp` prüft die produktive Kompaktierung und Sendelogik. Ein leerer
Verlauf benötigt nur 32 Byte. Ein Impuls und eine Gültigkeitslücke bleiben bei der Kompaktierung
bestehen. Langsame Sender erreichen die Abbruchfrist. Ein fehlgeschlagener Sendeaufruf beendet
den Versand. Die Messdaten überschreiten 64 KiB nicht.

Die Browserprüfung liest jetzt das kompakte Format 2 aus der tatsächlichen C++-Speicherklasse.
Sie prüft auch einen leeren Verlauf und die Begrenzung jeder Antwort.
Ein Dauertest mit schwachem WLAN muss weiterhin am Board erfolgen.

Zusätzlich wird der tatsächliche begrenzte Socket-Sendeweg geprüft: nach Fristablauf keine
weitere Sendung, Nullfortschritt als Fehler und nachfolgend wieder eine erfolgreiche Sendung.
Die Zeitgrenzenprüfung umfasst auch den Überlauf des 32-Bit-Millisekundenzählers.

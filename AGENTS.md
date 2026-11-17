# Arbeitsanweisungen für ProBusX Bridge

Diese Datei gilt für das gesamte Repository.

## Ziel und Sprache

- MQTT ist die Hauptschnittstelle der Bridge zu Home Assistant.
- Die Weboberfläche ersetzt weitere Conexio-Funktionen: Displayspiegelung, Reglerbedienung, Heizungsschema, Live-Werte und Verlaufsgraphen.
- Beide Bereiche vollständig dokumentieren. Die Webfunktionen müssen auch ohne MQTT-Broker funktionieren.
- Texte, Dokumentation und neue Kommentare in einfachem technischem Deutsch schreiben.
- Kurze Sätze und eindeutige Begriffe verwenden. Siehe `docs/Schreibregeln.md`.
- Funktionen, Hardware und Unterstützung anhand des Quellcodes und der Prüfdaten beschreiben.
- Fehlende Nachweise als offen kennzeichnen. Keine Gerätetypen oder Telegrammfelder ergänzen, die nicht geprüft wurden.
- Die README kurz halten. Funktionen und Unterstützung tabellarisch aufführen. Details gehören nach `docs/`.

## Einstieg in den Code

| Datei oder Bereich | Aufgabe |
| --- | --- |
| `firmware/src/main.cpp` | UART-Empfang, Rohaufnahme und Vorgangsreservierung |
| `firmware/src/settings.h` | Busparameter und GPIO-Zuordnung |
| `firmware/src/protocol.h` | Telegrammgrenzen, CRC, Zahlenformat und Tastencodes |
| `firmware/src/telemetry.h` | Übernahme von Haupt- und FlexBox-Werten |
| `firmware/src/quality.h` | Kommunikationsdiagnose |
| `firmware/src/live.cpp` | Gemeinsamer Live-Zustand und Tastenvorgänge |
| `firmware/src/mqtt_bridge.cpp`, `mqtt_discovery.h`, `mqtt_policy.h` | MQTT, Home Assistant und PNG-Ausgabe |
| `firmware/src/web.cpp`, `app.js`, `index.html`, `style.css` | Weboberfläche, WLAN und Optionen |
| `firmware/src/schema*` | Heizungsschema und gespeicherte Positionen |
| `firmware/src/history*` | RAM-Verlauf, Aggregation und asynchrone Übertragung |
| `firmware/src/http_connections*` | HTTP-Sitzungen und Leerlaufbereinigung |
| `firmware/src/ota*` | Update, Startprüfung und Rückfall |
| `firmware/capture.py` | Rohaufnahmen und PBXS-Auswertung |
| `tests/`, `firmware/tests/` | Protokoll-, Firmware-, MQTT- und Browserprüfungen |
| `home_assistant/custom_components/probusx/` | Frühere HTTP-Integration |
| `home_assistant/www/probusx/` | Dashboard-Bedienfeld |

Vor Protokolländerungen `docs/Protokoll.md` und `docs/Offene-Punkte.md` lesen.
Vor Änderungen an Nebenläufigkeit `docs/Architektur.md` und `docs/Verlauf.md` lesen.

## Erweiterungen umsetzen

1. Die betreffende Funktion und ihre bestehenden Tests bestimmen.
2. Empfang, Datenübernahme und Darstellung getrennt ändern.
3. Neue Messwerte bis zu einem gültigen Telegramm als unbekannt behandeln.
4. Vorzeichen, Skalierung, Fehlerwerte und Alter der Daten übernehmen.
5. Bei neuen Kanälen MQTT, Webansicht, Heizungsschema und Verlauf gemeinsam prüfen.
6. Bestehende MQTT-Topics, Discovery-Kennungen und API-Felder erhalten. Änderungen mit Migrationshinweis dokumentieren.
7. Änderungen an NVS-Datensätzen versionieren und bestehende Einstellungen übernehmen.
8. Tests für das neue Verhalten und seine Fehlerfälle ergänzen.
9. Dokumentation und Prüfergebnisse an den tatsächlich erreichten Stand anpassen.

### Empfang, Speicher und Netzwerk

- Nur der UART-Task liest den Bus. Netzwerkzugriffe dürfen ihn nicht blockieren.
- Unter der Live-Sperre nur kurze Datenzugriffe ausführen. Keine Netzwerkübertragung oder Flashspeicherung darunter starten.
- Umfangreiche Antworten in einem Hintergrundtask übertragen. Speicher und Laufzeit begrenzen.
- Asynchrone HTTP-Anfragen auch nach Fehler und Abbruch abschließen. Sitzungen bis dahin schützen.
- Beim Senden eine Gesamtfrist prüfen. Nullfortschritt beenden.
- Den Rohaufnahmepuffer nur für laufende Aufnahmen reservieren.
- Bei Platzbedarf zunächst ungenutzte, dann die ältesten Verlaufsblöcke freigeben.
- Vor dem Freigeben des Rohpuffers auf `RX_IDLE` und die Rückgabe aller ausgeliehenen Einträge warten.
- Die RAM-Historie nicht als dauerhafte Speicherung dokumentieren.

### Tasten und Buszugriff

- Web- und MQTT-Tasten über den vorhandenen gemeinsamen Sendepfad führen.
- Tasten nur im bestätigten Antwortfenster des jeweiligen Reglers senden.
- Bustreiber nach der Übertragung abschalten. Verfallene Befehle verwerfen.
- Die Sendesperre während Rohaufnahmen erhalten.
- Unbekannte Befehle nicht aus vermuteten Adressen oder ähnlichen Tastencodes erzeugen.

## Auf andere ESP32-Platinen übertragen

- Die vorhandene Konfiguration verwendet ESP32-S3, 16 MiB Flash und 8 MiB Octal-PSRAM.
- Die Netzwerkanbindung kann über WLAN und/oder LAN erfolgen. Netzwerktreiber und Buildkonfiguration an die Platine anpassen.
- Bei anderen Varianten Buildziel, Speicher, Partitionen, PSRAM-Modus und Pinbelegung anpassen und prüfen.
- Hardware-UART und gesamte RS485-Strecke müssen 1.000.000 Baud unterstützen.
- Halbduplex, 3,3-V-kompatible Logik und eine direkt steuerbare Senderfreigabe verwenden.
- Isolation, Pegelwandler und Richtungssteuerung bei der erreichbaren Datenrate mitprüfen.
- Empfang und Senden getrennt testen. Weitere Boards erst nach Gerätetest als unterstützt aufführen.

## Einen weiteren Regler analysieren

### 1. Anlage erfassen

Hersteller, genaue Typbezeichnung, OEM-Bezeichnung, Firmwarestand und angeschlossene Module notieren.
Busmodus, Modulnummern und DIP-Stellungen dokumentieren. Anschlüsse und elektrische Schnittstelle anhand der Geräteunterlagen prüfen.
Baudrate und Zeichenformat messen oder mit fehlerfrei dekodierten Aufnahmen bestätigen. Die aktuelle Einstellung ist 1.000.000 Baud, 8N1.

### 2. Passiv aufzeichnen

1. Die Bridge ohne eigene Busbefehle betreiben.
2. Eine Aufnahme im unveränderten Grundzustand erstellen.
3. Gleichzeitig Regleranzeige, Messwerte, Ausgangszustände und Uhrzeit festhalten.
4. Anschließend jeweils einen Zustand ändern und den Zeitpunkt notieren.
5. Ein- und Ausschalten desselben Ausgangs getrennt aufzeichnen. Zwischenwerte aufnehmen, wenn der Ausgang sie liefert.
6. Tasten einzeln über das vorhandene Bediengerät auslösen. Den Dreitastendruck gesondert erfassen.

Während einer Bridge-Rohaufnahme direkt am Regler oder über ein anderes bereits funktionierendes Bediengerät umschalten.
Die Web- und MQTT-Tasten der Bridge sind währenddessen gesperrt.

### 3. Aufnahmen prüfen

```sh
python firmware/capture.py decode aufnahme.pbxs auswertung --hex
```

- Originaldatei und SHA-256-Prüfsumme erhalten. Transportverluste und UART-Fehler auswerten.
- TCP-, UART- und PBXS-Blockgrenzen nicht als Telegrammgrenzen behandeln.
- Bei unvollständigen Aufnahmen Lücken dokumentieren und erneut aufzeichnen.
- Private Aufnahmen und Zugangsdaten nicht in das öffentliche Repository übernehmen.

### 4. Telegramme zuordnen

| Merkmal | Vorgehen |
| --- | --- |
| Kopf und Kennung | Wiederkehrende Folgen über mehrere Aufnahmen vergleichen |
| Länge | Vollständige Telegramme bestimmen und auf geteilte Eingabeblöcke prüfen |
| Adressen | Geräte oder DIP-Stellungen einzeln ändern und die veränderten Kopfbytes vergleichen |
| Prüfsumme | Polynom, Startwert, Bereich, Bytefolge und Ausnahmen getrennt prüfen |
| Temperaturen | Mehrere bekannte Werte, negative Werte und unbenutzte Eingänge vergleichen |
| Ausgänge | Aus, Ein und vorhandene Zwischenwerte gegen die Regleranzeige prüfen |
| Display | Größe, Graustufen, Reihenfolge und vollständige oder teilweise Bildübertragung bestimmen |
| Tasten | Abfrage, Antwortcode, Prüfsumme, Zeitfenster und Wiederholungen bestimmen |

Unbekannte Bytes als unbekannt stehen lassen. Offsets ab dem ersten Telegrammbyte mit Null zählen.
Rx und Tx aus Sicht der Bridge dokumentieren.

Die bestätigte CRC dieses Projekts verwendet das reflektierte Polynom `0x8408` und Startwert `0xFFFF`.
Für weitere Telegrammfamilien die CRC gesondert bestimmen. Displaytelegramme besitzen bisher keine bestätigte CRC.

### 5. Decoder und Nachweise ergänzen

1. Neue Zuordnungen getrennt vom vorhandenen Decoder ergänzen. Bei mehreren Varianten Modellprofile einführen.
2. Feste Längen, Strukturmerkmale und bestätigte Prüfsummen vor der Datenübernahme prüfen.
3. Referenzwerte für Temperaturen, Ausgänge, Display und Tasten mit der Aufnahme abgleichen.
4. Wiedergabe mit kleinen, großen und geteilten Eingabeblöcken prüfen.
5. Beschädigte, verkürzte, verspätete und unbekannte Telegramme testen.
6. Den bisherigen Regler mit seinen vorhandenen Tests weiter prüfen.
7. Unterstützung erst nach einem dokumentierten Gerätetest in der README eintragen.

FlexBox 2 und 3 nicht aus aufeinanderfolgenden Adressen ableiten.
Bei FlexBox 1 stammen die Temperaturen aus `40:F0`, die Ausgangswerte aus dem Haupttelegramm `36:61` bei Byte 114 bis 121.
F1R1 und F1R3 sind mit aktiven Werten geprüft. Für F1R2 und F1HE fehlen aktive Vergleichswerte.

## Prüfungen und Build

| Änderung | Prüfung |
| --- | --- |
| Protokoll oder Firmwarelogik | `python tools/pruefen.py` |
| MQTT | `python tools/pruefen_mqtt.py`; bei Integrationsänderungen zusätzlich die HA-Laufzeitprüfung nach `docs/Pruefungen.md` |
| Weboberfläche | `npm ci`, `npx playwright install chromium --only-shell`, `npm test` |
| Originalaufnahmen | `python tools/pruefen.py --replay grund.bin tasten.bin` |
| Zeitverhalten | `python tools/pruefen.py --timed grund.timed tasten.timed` |
| Firmware | Build und Abbildprüfung nach `docs/Entwicklung.md` |

Originalaufnahmen sind nicht enthalten. Nur tatsächlich ausgeführte Prüfungen als bestanden nennen.
Tests am Simulator und Tests am Gerät getrennt dokumentieren.

Bei Firmwareänderungen Projekt- und Firmwareversion aktualisieren.
Nach einem Versionswechsel den alten Build bereinigen. Anschließend USB- und OTA-Abbild aus demselben Build erstellen und prüfen.
OTA-Projektkennung `pbx_waveshare_ota_v1` und vorhandene Partitionen nicht nebenbei ändern.
Generierte Dateien wie `.pio/`, `web_assets.h` und Zugangsdaten nicht einchecken.

## Vorgemerkte Änderung

Die Optionsseite soll bei kurzen Status-Timeouts bearbeitbar bleiben. Eingaben erhalten und veralteten Verbindungsstatus anzeigen.
Bearbeiten und Speichern getrennt behandeln. Laufende Vorgänge weiterhin mit verständlicher Begründung absichern.
Diese Änderung ist im Ausgangsstand 0.6.1 noch nicht umgesetzt.

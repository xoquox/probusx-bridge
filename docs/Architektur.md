# Projektaufbau

## Verzeichnisse

| Pfad | Aufgabe |
| --- | --- |
| `firmware/src/` | ESP-IDF-Anwendung und Weboberfläche |
| `firmware/tools/` | Webdateien einbetten und Firmwareabbilder erstellen |
| `firmware/capture.py` | PBXS-Dateien aufnehmen und auswerten |
| `firmware/tests/` | Aufnahme-, WLAN-, Web- und OTA-Tests |
| `home_assistant/custom_components/probusx/` | Optionale HTTP-Integration für HA ohne MQTT |
| `home_assistant/www/probusx/` | Dashboard-Karte |
| `tests/` | Protokoll-, Bild-, HA- und Browserprüfungen |
| `tools/` | Gemeinsame Prüf- und Paketprogramme |
| `docs/` | Deutsche Dokumentation |
| `.github/workflows/` | Automatische Prüfungen auf GitHub |

## Datenfluss

1. Der UART-Empfangstask liest die Busbytes auf Kern 1.
2. Der Parser sammelt ein Telegramm anhand seiner festen Länge.
3. Bekannte CRC-Familien werden vor der Übernahme geprüft.
4. Der Live-Speicher übernimmt gültige Werte und vollständige Bilder.
5. Der MQTT-Task kopiert einen Schnappschuss unter kurzer Live-Sperre.
6. Nach Freigabe der Sperre sendet er JSON, PNG und Discovery zum Broker.
7. Die native MQTT-Integration in HA erzeugt Sensoren, Kamera und Tasten.
8. HTTP stellt dieselben Busdaten für die direkte Weboberfläche bereit.

Die TCP- und UART-Blockgrenzen können mitten in einem Telegramm liegen.
Ein Block ist daher kein Busframe.
Der Parser sammelt über mehrere Blöcke hinweg weiter.

## Empfang und Netzwerk trennen

Der UART-Puffer umfasst 32768 Byte.
Eine Rohaufnahme verwendet zusätzlich einen Ringpuffer mit 2 MiB PSRAM.
Der Netzwerktask holt PBXS-Datensätze aus diesem Puffer.
Ein langsamer PC darf die UART-Leseschleife nicht anhalten.
Bei vollem Aufnahmepuffer werden Verluste gezählt.

Die Live-Dekodierung läuft auch ohne Aufnahme und ohne Browser.
Die Displaydaten belegen 5120 Byte im Live-Speicher.
Eine HTTP-Antwort verwendet eine eigene Bildkopie.
Die Übertragung hält dadurch die Empfangssperre nicht fest.

## Sperren und Reservierungen

Die Live-Sperre schützt Parser, Messwerte, Optionen und Tastenzustand.
Die Vorgangsreservierung schützt Aufnahme, WLAN-Aktion, OTA und Tastenvorgang gegeneinander.
Ein längerer Vorgang gibt seine Reservierung an den zuständigen Hintergrundtask weiter.

NVS-Schreibvorgänge laufen außerhalb der Live-Sperre.
Erst nach erfolgreicher Speicherung werden neue Optionen im RAM übernommen.
So hält ein Flashzugriff den Busparser nicht unnötig an.

## Tasten senden

Ein MQTT-Befehl oder HTTP-Aufruf merkt über dieselbe Funktion einen Tastenvorgang vor.
Die Firmware wartet auf `A5 13 36 01 34 50 C3`.
Sie prüft zusätzlich das Ende des Leseblocks und den UART-Empfangspuffer.
Dann schaltet sie den Bustreiber für genau die Antwort ein.
Nach Ende der UART-Sendung schaltet sie den Treiber wieder aus.

Diese Prüfung ersetzt keine Messung der elektrischen Umschaltzeit.
Die Anwendung legt auch keine GPIO-Pegel während Bootloader und Reset fest.

## Home Assistant

Die neue Verbindung verwendet die [native MQTT-Anbindung](MQTT.md).
`mqtt_bridge.cpp` enthält Client, NVS-Einstellungen und Hintergrundtask.
`mqtt_discovery.h` erzeugt die 61 Discovery-Datensätze.
`mqtt_policy.h` enthält die prüfbaren Regeln und den PNG-Encoder.
Die Weboberfläche erhält keine MQTT-Zugangsdaten aus dem Status.
Der Ausfall des Brokers ändert die WLAN-Bindung und den Ersatzzugangspunkt nicht.

Die folgenden Absätze beschreiben die frühere HTTP-Integration:

`Coordinator` bündelt die Statusabrufe aller Entities.
Temperaturen und Ausgänge haben getrennte Übernahmeintervalle.
Ein eigener Bildcache bündelt gleichzeitig angeforderte Kamerabilder.
Unveränderte Pixel verwenden das vorhandene PNG weiter.

Ein Bildfehler wird getrennt von einem Statusfehler gezählt.
Der Sitzungstoken wird nach einem Neustart neu abgerufen.
Ein möglicherweise ausgeführter Tastendruck wird nicht wiederholt.

## Webdateien

`wifi_policy.h` enthält die gemeinsamen Regeln für BSSID und WLAN-Speicherung.
Die WLAN-Aufträge verwenden denselben Vorgangsschutz wie bisher.
Der NVS-Datensatz der Version 2 ist 112 Byte lang.
Der frühere 104-Byte-Datensatz wird als automatische Wahl übernommen.
Unlesbare Bindungsdaten erlauben keine automatische Verbindung.

Die Suchliste liegt dynamisch im Heap.
Die Zahl der Ergebnisse kommt aus `esp_wifi_scan_get_ap_num`.
`esp_wifi_scan_get_ap_records` liest die vollständige Liste.
Die WLAN-Statusantwort erzeugt JSON unter der Modellsperre.
Der UART-Empfang verwendet diese Sperre nicht.

`index.html`, `app.js` und `style.css` sind die bearbeitbaren Quellen.
`embed_assets.py` erzeugt daraus `web_assets.h`.
Diese Datei wird in die Firmware eingebettet.
Sie darf nicht von Hand bearbeitet werden.
Der PlatformIO-Vorlauf erzeugt sie bei jedem erforderlichen Build neu.

## Schema-Anzeige

`schema.js` verwaltet die Bildfläche und die Bearbeitung im Browser. Live-Werte stammen
aus derselben API wie die übrigen Anzeigen. `schema.cpp` prüft Uploads, serialisiert
Aufträge und bedient den Schema-Speicher. `schema_validation.h` prüft Layout und Bildköpfe.
`schema_store.h` enthält den atomaren Bankwechsel und die CRC-Prüfung.
Die Speicherkarte steht unter [Heizungsschema](Heizungsschema.md).

## Lokaler Verlauf

Ab Firmware 3.6.0: Siehe [Verlauf](Verlauf.md) für Bedienung, Speicheraufteilung und Grenzen.

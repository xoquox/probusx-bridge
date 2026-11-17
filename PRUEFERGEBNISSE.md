# Prüfergebnisse für Projekt 0.6.1

Prüfdatum: 14.11.2026. Ausgangsstand: Projekt 0.6.0 / Firmware 3.6.0.

## Fehlerpfad und Korrektur

Der Datenhandler der 3.6.0 berechnete und sendete die gesamte Verlaufsantwort im zentralen
HTTP-Task. Bei 1024 Zeitkörben waren das 262.176 Byte, auch bei überwiegend leeren Körben.
Für die gesamte Antwort fehlte eine gemeinsame Frist. Ein langsamer Abruf konnte dadurch
andere Webseiten und den Neustart-Endpunkt verzögern. Der Betreiber meldete den wiederholbaren
Auslöser „Verlauf öffnen“. Ob sämtliche beobachteten MQTT-Verbindungsabbrüche dieselbe Ursache
haben, ist durch die Offlineprüfung nicht nachgewiesen.

Die 3.6.1 legt die Arbeit in einen eigenen Task mit Priorität unter MQTT. Der HTTP-Handler
nimmt den Auftrag an und kehrt zurück. Höchstens ein Abruf läuft. Eine zweite Anfrage erhält
HTTP 429. Leere Körbe werden entfernt; maximal 256 belegte Körbe ergeben 65.568 Byte einschließlich
Kopf. Speicher, asynchrone Anfrage und Sitzungsbesitz werden nach Erfolg oder Fehler freigegeben.
Die Sendelogik prüft eine Acht-Sekunden-Frist vor jedem Block. Für einzelne Socket-Schreibaufrufe
gilt eine Sekunde Wartezeit. Während des Sendens wird keine Verlaufssperre gehalten.

## Prüfungen

- Gesamte Basis-, Protokoll- und Browserprüfung bestanden.
- Echte neue HTTP-Handler in einer SDK-Testumgebung: keine synchrone Messdatenübertragung,
  kein Antwortpuffer im HTTP-Handler, zweiter Abruf sofort abgewiesen, keine Sperre beim Senden.
- Sendeabbruch und Speichermangel: Auftrag und Antwortpuffer wieder frei, nächster Abruf erfolgreich.
- Echter Sitzungsbereiniger: asynchronen Verlauf schützen, danach normal bereinigen.
- Produktive Sendelogik: langsamer Empfänger, Zeitgrenze, Abbruch, leere Antwort und Größenlimit.
- Produktive Kompaktierung: Reihenfolge, Ausgangsimpuls und Gültigkeitslücke bleiben erhalten.
- Chromium mit Binärdaten der tatsächlichen Speicherklasse: beide Diagramme, gemeinsamer Zoom,
  Kanalwahl, Zeitvorgaben, Datumsgrenzen, leere Daten und beschädigte Antworten bestanden.

## Firmwareabbilder

Build mit ESP-IDF 5.5.0 und PlatformIO 6.2.0 bestanden.
Linkergröße: 1.297.073 Byte. OTA-Datei: 1.297.472 Byte. USB-Datei: 6.365.184 Byte.
Statisch belegter RAM: 55.964 Byte. Dynamische Task-Stacks sind darin nicht enthalten.
Der neue Abruf-Task besitzt 8192 Byte Stack. Ein Antwortpuffer belegt höchstens 64 KiB PSRAM.
USB und OTA bestehen SHA-256, Versions-, Projekt-, Partitions- und Gleichheitsprüfung.
Die finale Basisprüfung umfasst 30 Firmwaretests ohne übersprungene SDK-Prüfung,
drei Codec-Tests und fünf Projektprüfungen. Die native MQTT-Prüfung ist bestanden.
Der Quellpatch wird vor Auslieferung angewendet und byteweise mit dem Quellordner verglichen.

## Grenze

Diese Prüfungen sprechen kein physisches Waveshare an. Ein tatsächlicher WLAN-Dauertest,
mehrere parallele Browser und der Vergleich der MQTT-Zustellung müssen am Gerät folgen.
Die Busdekodierung und MQTT-Entity-IDs wurden nicht geändert. Die RAM-Historie bleibt flüchtig.

---

# Prüfergebnisse für Projekt 0.6.0

Prüfdatum: 14.11.2026. Firmware 3.6.0. Ausgangsstand: Projekt 0.5.2 und Firmware 3.5.2.

## Ergebnis

| Prüfung | Ergebnis |
| --- | --- |
| Firmware-Basis | 29 Tests; der zunächst übersprungene SDK-Test separat mit ESP-IDF ausgeführt und bestanden |
| Codec | 3 Tests bestanden |
| Projekt und Paketwerkzeuge | 5 Tests bestanden |
| Native C++-Prüfungen | Verlauf, Neustart, Schema, Protokoll, CRC, Optionen und FlexBox bestanden |
| Browser | Bedienfeld, gesamte Firmwareoberfläche, Schema und neue Verlaufsseite bestanden |
| MQTT | Produktive C++-Discovery-/PNG-Hilfen und JSON-Prüfung bestanden |
| ESP32-S3 | Build mit ESP-IDF 5.5.0 und PlatformIO 6.2.0 bestanden |
| USB und OTA | Prüfsummen, Version, Projektkennung, Partitionen und gleiche Anwendung bestanden |

Die Verlaufsprüfung nutzt die tatsächliche Speicherklasse und erzeugt damit die binären
Antworten für Chromium. Geprüft sind getrennte Tabellen, 14 Temperatur- und 15 Ausgangskanäle,
gespeicherte Kanalwahl, Stunde/Woche, freie Datumsgrenzen, umgekehrte Grenzen, Mauszoom,
Zoom-Rücknahme, verspätete und beschädigte Antworten, Optionen, Vorgangssperre und FlexBox-Auswahl.
Die bestehende Webprüfung umfasst weiterhin WLAN/BSSID, OTA, Aufnahmedownload, Tasten und Bedienpause.

## Speicher und Firmware

- Linkergröße der Anwendung: 1.295.225 Byte von 3.145.728 Byte OTA-Partition.
- OTA-Datei: 1.295.632 Byte.
- USB-Datei: 6.365.184 Byte.
- Statisch belegter interner RAM: 55.940 Byte von 327.680 Byte.
- Dynamische Task-, Netzwerk- und PSRAM-Belegung ist darin nicht enthalten.
- Verlaufsblock: 65.536 Byte; Messpunkt: 72 Byte; Zeitkorb: 256 Byte.
- Kein dauerhafter 2-MiB-Rohpuffer im Leerlauf. Mindestens 512 KiB PSRAM bleiben als Betriebsreserve.

Der Speichertest prüft freie und belegte Blöcke, das Löschen ausschließlich alter Blöcke,
erneutes Ergänzen von Kapazität und fortlaufenden Ringüberlauf. Min/Max-Prüfungen erhalten einen
kurzen Ausgangsimpuls, obwohl Anfang und Ende denselben Wert haben. Ungültige Temperaturwerte
setzen die Lückenmaske. Die SDK-Implementierung von `vRingbufferDelete` wurde geprüft:
Sie gibt die statisch bereitgestellten Rohpufferdaten nicht selbst frei. Die Anwendung tut dies
erst nach RX_IDLE und Rückgabe aller geliehenen Einträge.

## Grenzen dieser Prüfung

Kein reales Waveshare wurde hier geflasht. Tatsächliche Speicherfragmentierung, mehrere
Aufnahmestarts und Abbrüche am laufenden Heizungsbus sowie ein Dauertest sind noch am Gerät
zu prüfen. Die neue Verlaufsfunktion wurde nicht in einer echten HA-Laufzeit getestet;
die MQTT-Entity-IDs und Discovery-Nachrichten sind unverändert. Private Aufnahmen gehören
nicht zum Paket. Die früheren Ergebnisse folgen als Nachweis der Ausgangsstände.

---

# Prüfergebnisse für Projekt 0.5.2

Prüfdatum: 14.11.2026. Firmware 3.5.2. Ausgangsstand: Projekt 0.5.1 und Firmware 3.5.1.

## Neue Aufnahme und Fehlerursache

Die PBXS-Aufnahme dauert 120 Sekunden und enthält 1.292.760 Rohbytes.
Sie ist vollständig; keine Transportlücken, UART-Fehler oder Aufnahmepufferverluste gemeldet.
Rohstrom-SHA-256: `0aff072b9f2b0e602e672478ac5c9fcb36065da055510b66a4cb8efabe40b6bc`.

Der Parser nimmt alle 1320 Telegramme mit bekannter CRC an. Es gibt keinen CRC-Fehler.
Die 240 Displaytelegramme haben weiterhin keine bestätigte Bus-CRC.
Die Aufnahme ist privat und wird nicht in das Quellpaket übernommen.

| Prüfung | Ergebnis |
| --- | --- |
| Hauptdaten `36:61` | 120 gültige Telegramme |
| F1-Temperaturdaten `40:F0` | 240 gültige Telegramme |
| F1-Kurzstatus `36:70` | 240 gültige Telegramme |
| Bisher gelesene F0-Ausgangswörter | In allen 240 Fällen null |
| Neue Hauptdaten-Ausgangswörter 114..121 | Stimmen in allen 120 Fällen mit dem vorherigen Kurzstatus überein |
| F1R3 | In allen 120 Haupttelegrammen 100 %; passt zum beigefügten Reglerbild |
| F1R1 | Vier kurze aktive Hauptdatenstände; acht aktive Kurzstatusstände |
| Blockgrößen 1, 7 und 1024 Byte | Gleiche Ergebnisse ohne Parser-Verwerfung |

F1R2 und F1HE wurden nur mit Nullwerten beobachtet. Ihre aktive Zuordnung bleibt vorläufig.
F1R3 wurde in dieser Aufnahme nicht ausgeschaltet. Die neue Zuordnung stützt sich auf den aktiven
Anzeigezustand, den unabhängigen Kurzstatus und die parallel beobachteten R1-Wechsel.

## Neue Prüfungen

`flex_values_test.cpp` prüft die tatsächlich verwendeten Übernahmefunktionen:
aktive R3-Werte, synthetische Ein-/Aus-Wechsel für R1 und R3, F0 ohne Überschreiben der Ausgänge,
CRC-Verwerfung, geteilte Empfangsblöcke und gemeinsame Frische beider F1-Quellen.
`flex_replay_test.cpp` vergleicht den neuen Decoder gegen den unabhängigen Kurzstatus der privaten Aufnahme.
Die MQTT-Discovery-Prüfung kontrolliert die neuen Zuordnungsattribute und die unveränderten Entity-IDs.
Die Basisprüfung mit 29 Firmwaretests, drei Codec-Tests, fünf Projektprüfungen und den nativen Prüfungen ist bestanden.

## Tastensperre

Der Aufnahmekopf bestätigt `sender_enabled: false`.
Die vorhandene Firmware sperrt Web- und MQTT-Tasten während der Aufnahme absichtlich.
Diese Funktion bleibt unverändert. Für eine gezielte Umschaltung im Mitschnitt direkt die Regler-Tasten verwenden.

## Firmware 3.5.2

Vollständiger ESP32-S3-Build mit ESP-IDF 5.5.0 und PlatformIO 6.2.0 bestanden.
Linkergröße: 1.258.021 Byte. OTA-Anwendungsdatei: 1.258.432 Byte.
Statisch belegter RAM: 55.812 Byte. Die Partitionstabelle ist unverändert.
USB und OTA bestehen die Prüfungen auf Version, Projektkennung, SHA-256,
Partitionstabelle, identische Anwendung und OTA-Startdaten.
Die unveränderte Browseroberfläche wurde für diese Decoderkorrektur nicht erneut als Browserlauf ausgeführt.

## Grenze

Der neue Decoder wurde offline gegen die tatsächliche Aufnahme geprüft.
Kein reales Waveshare wurde hier geflasht und keine Pumpe wurde hier geschaltet.
Die folgenden Prüfergebnisse dokumentieren die älteren Ausgangsstände.

---

# Prüfergebnisse für Projekt 0.5.1

Prüfdatum: 13.11.2026. Firmware 3.5.1. Ausgangsstand: Projekt 0.5.0 mit Firmware 3.5.0.

## Oberfläche

Alle drei Browserprüfungen mit Playwright 1.51.0 sind bestanden.
Die Schema-Prüfung kontrolliert zusätzlich die linke Navigation und das vollständige Bild
bei 1366 × 768, 1920 × 1080 und 1250 × 1000 Pixeln.
Das Bild beginnt weniger als 120 Pixel unter dem oberen Fensterrand und endet innerhalb des Fensters.
Es gibt in diesen Ansichten keinen horizontalen Seitenüberlauf.
Bildverhältnis und relative Markerpositionen bleiben erhalten.

Bearbeitung, Speichern, Verschieben, Bezeichnungen, Sichtbarkeit, Upload und Wiederherstellung
sind weiterhin geprüft. Verschieben bleibt auf den Bearbeitungsmodus begrenzt.
Die Prüfungen für WLAN, BSSID, MQTT-Formular, Passworterhaltung, Aufnahme, Diagnose,
Neustart, Bedienpause und OTA-Fehlerwege bestehen ebenfalls.
Die bisherige HA-Karte besteht ihre Browserprüfung.

Die Browser verwenden ein lokales Gerätemodell. Kein reales Gerät wird dabei verändert.
Der visuelle Vergleich verwendet ein synthetisches Heizungsschema.
Der unveränderte MQTT- und Buscode wurde für diese Layoutänderung nicht erneut als HA- oder Hardwaretest ausgeführt.
Die bisherigen Prüfergebnisse stehen nachfolgend als Nachweis des Ausgangsstands.

## Firmware 3.5.1

Der vollständige ESP32-S3-Build ist bestanden. ESP-IDF 5.5.0, PlatformIO 6.2.0,
CMake 3.30.2 und Ninja 1.11.1 wurden verwendet.
Die Linkergröße beträgt 1.257.633 Byte, 40,0 Prozent des 3-MiB-OTA-Bereichs.
Der statisch belegte RAM bleibt bei 55.812 Byte.
USB- und OTA-Abbild bestehen die Prüfung auf SHA-256, Version, Projektkennung,
Partitionstabelle, identische Anwendung und OTA-Startdaten.
Fünf Projektprüfungen einschließlich Dokumentationslinks sind bestanden.
Die Partitionstabelle sowie Bus-, MQTT-, HTTP-API-, OTA- und Speichercode sind
bytegleich zur Ausgangsversion. Ein reales Waveshare wurde nicht geflasht.


---

# Prüfergebnisse für Projekt 0.5.0

Prüfdatum: 10.11.2026.
Quellstand: Projekt 0.5.0, Firmware 3.5.0.
Ausgangsstand: Projekt 0.4.1, Firmware 3.4.1.
Die bisherige HTTP-Integration bleibt unverändert bei 0.3.1.

## MQTT

- ESP-IDF verwendet seinen mitgelieferten ESP-MQTT-Client.
- Die Discovery-Datensätze entstehen im geprüften C++-Produktionscode.
- 61 eindeutige Entities mit der nativen MQTT-Integration von HA 2026.11.1 geprüft.
- Der Test verwendet Python 3.14.10 und einen echten lokalen TCP-Broker mit amqtt 0.11.3.
- Temperaturen, Temperaturwert `null`, Prozentwerte und binäre Ausgangszustände geprüft.
- Alle fünf Tasten einschließlich Dreitastendruck ergeben den passenden MQTT-Befehl.
- Verbindungskennung, gespeicherte Nachrichten, DUP-Markierung, Teilnachrichten und Längengrenzen in den tatsächlich verwendeten C++-Regeln geprüft.
- Hauptgerät und F1 werden getrennt nicht verfügbar. Bridge-Ausfall betrifft auch die Kamera.
- Wiederverbindung und HA-Neuladen übernehmen die im Broker gespeicherten Daten.
- Die Kameranutzdaten sind bytegleich zum gesendeten PNG. Das Intervallattribut wird übernommen.
- Alle 20.480 PNG-Pixel stimmen mit der bisherigen Rohbilddarstellung überein.
- Die bestehende Bedienfeld-Karte zeigt PNG auch mit dem MIME-Typ des nativen HA-Kamera-Proxys an.
- Es wird keine benutzerdefinierte ProBusX-HTTP-Integration für diesen Test eingerichtet.

Die HA-Prüfumgebung ersetzt nur die Betriebssystem-Abfrage der Netzwerkadapter.
Der MQTT-Client und die MQTT-Plattformen von HA laufen unverändert.
Eine optionale JPEG-Beschleunigungsbibliothek fehlt in dieser Umgebung. HA meldet das beim Start.
Das PNG wird dabei vollständig übernommen. Der Browser zeigt es korrekt an.
Der neue GitHub-Prüfauftrag verwendet Mosquitto statt amqtt und installiert die JPEG-Systembibliothek.
Dieser GitHub-Auftrag wurde lokal nicht als GitHub-Aktion ausgeführt.

## Bestehende Funktionen

- 29 Firmwaretests, drei Codec-Tests und fünf Projektprüfungen bestanden.
- Native Prüfungen für HTTP-Sitzungen, Schema-Speicher, Neustartsperren und Protokoll bestanden.
- Hauptdaten mit 1864 einzelnen Bitfehlern, Parsererholung und CRC-Prüfung bei ausgeschalteter Analyse bestanden.
- Einstellgrenzen, Tastenzustände und Verfall eines Auftrags bestanden.
- Drei Browserprüfungen mit Playwright 1.51.0 bestanden.
- MQTT-Formular: TLS-Wahl, Passwort behalten/löschen, Fehlerfall, Neuladen, unveränderte Eingaben beim Statusabruf und Vorgangssperre bestanden.
- WLAN- und BSSID-Wahl, Schema-Bearbeitung, Display, Aufnahme, Diagnose, OTA, Neustart und Bedienpause bleiben geprüft.

## Firmware

- Vollständiger ESP32-S3-Build mit PlatformIO 6.2.0, espressif32 6.12.0 und ESP-IDF 5.5.0 bestanden.
- Xtensa GCC 14.2.0+20241119, CMake 3.30.2 und Ninja 1.11.1 verwendet.
- Statisch belegter RAM: 55.812 Byte. Das sind 2.312 Byte mehr als in Firmware 3.4.1.
- Die Anwendung belegt rund 1,26 MB von 3 MiB je OTA-Bereich. Die genaue Binärgröße steht im Firmware-Prüfbericht und in den Prüfsummen.
- Zusätzlich entstehen zur Laufzeit MQTT-Taskstapel, Puffer und gegebenenfalls TLS-Speicher. Die Linker-RAM-Zahl ist keine Angabe des freien Heaps zur Laufzeit.
- USB- und OTA-Abbild stammen aus demselben Build. Projektkennung, Anwendungshash, Partitionen und OTA-Startdaten werden mit `verify_firmware.py` geprüft.
- Die Partitionstabelle bleibt bytegleich zur Ausgangsversion. Vorhandene WLAN-, BSSID-, Options- und Schema-Daten werden durch das OTA-Abbild nicht überschrieben.

## Grenze des Nachweises

Kein reales Waveshare wurde geflasht. UART-Empfang, elektrische RS485-Umschaltung,
WLAN-Ausfall, NVS-Speicherung auf dem Board und TLS-Verbindung zum späteren Broker müssen am Gerät geprüft werden.
Die Strukturprüfung des NVS-Datensatzes ersetzt keinen Stromausfalltest beim Schreiben.
Die neue Firmware verändert den Busparser und die elektrische Ansteuerung nicht.
Die Freigabe einer Taste verwendet dieselbe Buswarteschlange wie die Weboberfläche.

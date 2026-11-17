# Lokale Schnittstellen

## MQTT

Die neue HA-Verbindung verwendet MQTT. Topics und Nachrichten stehen in der [MQTT-Dokumentation](MQTT.md).

## HTTP

Die Bridge verwendet Port 80 und HTTP ohne TLS.
Die Antworten verwenden `Cache-Control: no-store`.
Die Zustandsdaten enthalten das Datenschema `1`.

| Methode | Pfad | Aufgabe |
| --- | --- | --- |
| GET | `/api/status` | WLAN, Aufnahmestatus, OTA-Status und Sitzungstoken lesen |
| GET | `/api/live` | Messwerte, Frische, Optionen und Modulplätze lesen |
| GET | `/api/display` | Genau 5120 Pixelbytes lesen |
| GET | `/api/diagnostics` | Kommunikationsdiagnose lesen |
| GET | `/api/health` | Lokale Startprüfung beantworten |
| POST | `/api/key` | Einen Tastenvorgang vormerken |
| POST | `/api/transmit` | Tastensenden freigeben oder sperren |
| POST | `/api/settings` | Alle fünf Anzeige- und Freigabeeinstellungen speichern |
| POST | `/api/mqtt` | Broker und MQTT-Zugangsdaten dauerhaft speichern |
| POST | `/api/wifi` | WLAN-Zugangsdaten speichern |
| POST | `/api/scan` | WLAN-Suche starten |
| POST | `/api/forget` | Gespeichertes WLAN vergessen |
| POST | `/api/stop` | Rohaufnahme beenden |
| POST | `/capture` | Rohaufnahme beginnen und PBXS-Datei herunterladen |
| POST | `/api/update` | OTA-Anwendungsdatei übertragen |

Ändernde Formularaufrufe benötigen den aktuellen Sitzungstoken im Feld `token`.
Der Inhaltstyp lautet `application/x-www-form-urlencoded`.
Ein Geräteneustart erzeugt einen neuen Token.
Die Oberfläche und der HA-Client lesen ihn erneut ein.

Der OTA-Aufruf verwendet einen anderen Übertragungsweg:

- Inhaltstyp: `application/octet-stream`.
- Inhalt: rohe OTA-Datei ohne Multipart-Hülle.
- Header: `X-CSRF-Token` mit dem aktuellen Sitzungstoken.

Der Token verhindert keine Zugriffe durch einen berechtigten Leser des lokalen Statusendpunkts.
Er ist kein Benutzerpasswort und ersetzt keine Netzwerkzugriffskontrolle.

## Messwerte

### WLAN-Status ab Firmware 3.3.0

`GET /api/status` ergänzt das Objekt `wifi` um diese Felder:

| Feld | Inhalt |
| --- | --- |
| `ap_mode` | `auto` oder `fixed`; gespeicherte Wahl |
| `selected_bssid` | Gespeichertes Ziel; bei automatischer Wahl leer |
| `connected_bssid` | BSSID der aktuellen Verbindung; sonst leer |
| `connected_channel` | Aktueller Kanal; ohne Verbindungsdaten `null` |
| `connected_rssi` | Aktuelle Signalstärke in dBm; sonst `null` |
| `networks[].bssid` | BSSID dieses Suchtreffers |
| `networks[].channel` | Kanal dieses Suchtreffers |
| `networks[].ssid` | WLAN-Name; bei verborgenem Namen leer |
| `networks[].rssi` | Signalstärke dieses Suchtreffers in dBm |

Die Suchliste wird nicht nach SSID zusammengefasst und nicht auf 32 Einträge gekürzt.
Bei Speichermangel meldet die Bridge einen Fehler statt einer unvollständigen Liste.

`POST /api/wifi` erwartet weiterhin `token`, `ssid` und `password`.
Zusätzlich werden `ap_mode=auto|fixed` und `bssid` unterstützt.
Bei `fixed` muss `bssid` eine gültige Unicast-MAC-Adresse enthalten.
Bei `auto` wird die BSSID geleert.
Fehlt `ap_mode`, gilt für ältere Clients die automatische Wahl.
Eine ungültige Einstellung wird vor dem Speicherauftrag abgelehnt.
Das API-Schema der HA-Messwerte bleibt bei `1`.

### Busmesswerte

`GET /api/live` liefert unter anderem:

| Feld | Inhalt |
| --- | --- |
| `schema` | Datenschemaversion, aktuell `1` |
| `device_id` | MAC-basierte Kennung dieser Bridge |
| `version` | Firmwareversion |
| `temperatures_raw` | 14 vorzeichenbehaftete Rohwerte |
| `outputs_raw` | 15 vorzeichenbehaftete Rohwerte |
| `main_age_ms` | Alter der Hauptwerte in ms |
| `ext_age_ms` | Alter der F1-Werte in ms |
| `display_age_ms` | Alter des vollständigen Bildes in ms |
| `poll_age_ms` | Alter der passenden Reglerabfrage in ms |
| `tx_enabled` | Aktuelle Sendefreigabe |
| `key_pending` | Tastenvorgang läuft oder wartet |
| `busy` | Ein reservierter Vorgang läuft |
| `settings` | Gespeicherte Intervalle und Freigaben |
| `extensions` | Plätze F1 bis F3 mit Zuordnungsstatus |
| `diagnostics` | Gekürzte Kommunikationsdiagnose |

Ein Datenalter von `-1` bedeutet: Noch kein gültiger Datensatz vorhanden.
Die Frischegrenze für HA beträgt drei Sekunden.
Rohwertarrays können trotzdem ihre alten Speicherwerte enthalten.
Die Frische daher immer zusammen mit den Rohwerten prüfen.

Reihenfolge der Temperaturen:

```text
S1 S2 S3 S4 S5 S6 S7 S8 S9 S10 F1S1 F1S2 F1S3 F1S4
```

Reihenfolge der Ausgangswerte:

```text
R1 R2 R3 R4 R5 R6 R7 R0 HE1 HE2 HE3 F1R1 F1R2 F1R3 F1HE
```

Ein Bildabruf liefert binäre Nutzdaten ohne ProBusX-Kopf.
Bei einem fehlenden oder veralteten Bild antwortet die Bridge mit HTTP 503.
Die HA-Integration wandelt die Nutzdaten in PNG um.

## Tasten und Einstellungen

`POST /api/key` erwartet zusätzlich `key`:

```text
up | right | down | left | special
```

HTTP 200 mit `accepted: true` bestätigt nur die Annahme.
Es ist keine bestätigte Antwort des Heizungsreglers.

`POST /api/transmit` erwartet zusätzlich `enable=0` oder `enable=1`.
`POST /api/settings` erwartet alle fünf Felder:

```text
display_ms
temperature_ms
output_ms
analysis_enabled
transmit_enabled
```

Die Freigabefelder enthalten `0` oder `1`.
Die Intervalle enthalten ganze Millisekundenwerte.
Die erlaubten Bereiche stehen unter [Bedienung](Bedienung.md).
Bei ungültigen Werten wird kein Teil der Einstellung übernommen.

## PBXS-Aufnahmeformat

PBXS ist die Transporthülle der Aufnahme.
PBXS gehört nicht zum ProBusX-Busprotokoll.
Alle mehrbyteigen Kopffelder verwenden Little Endian.

| Byteoffset | Länge | Feld |
| --- | --- | --- |
| 0 | 4 | ASCII-Kennung `PBXS` |
| 4 | 1 | Formatversion `2` |
| 5 | 1 | Datensatztyp |
| 6 | 2 | Flags, aktuell `0` |
| 8 | 4 | Fortlaufende Sequenznummer |
| 12 | 8 | ESP-Zeit in µs seit Start |
| 20 | 8 | Absoluter Offset der gelesenen UART-Bytes |
| 28 | 4 | Nutzdatenlänge, höchstens 1024 Byte |
| 32 | 4 | IEEE-CRC32 über Kopfbytes 0 bis 31 und Nutzdaten |
| 36 | variabel | Nutzdaten |

Datensatztypen:

| Typ | Technischer Name | Inhalt |
| --- | --- | --- |
| 1 | `HELLO` | JSON mit Sitzung und UART-Einstellungen |
| 2 | `DATA` | Unveränderte UART-Bytes |
| 3 | `STATUS` | JSON mit Aufnahmezählern |
| 4 | `END` | JSON mit abschließenden Aufnahmezählern |

Der Zeitstempel entsteht nach einem UART-Lesevorgang.
Er ist kein genauer Bit- oder Bytezeitstempel.
Die Aufnahme ordnet empfangene Bytes keinem elektrischen Sender zu.

Das Auswerteprogramm prüft CRC32, Sequenzen, Byteoffsets und Abschlusszähler.
Es prüft nicht die ProBusX-CRC16 der enthaltenen Busframes.
Diese zweite Prüfung übernimmt der Busparser.

## TCP-Aufnahmedienst

Der TCP-Dienst verwendet Port `4196`.
Der PC sendet eine ASCII-Zeile:

```text
START 60000\n
```

Die Zahl ist die Dauer in Millisekunden.
Erlaubt sind 1000 bis 3600000 ms.
`\n` bezeichnet ein tatsächliches LF-Zeichen.
Danach sendet die Bridge den PBXS-Datenstrom.
Der Befehl wird nicht auf den RS485-Bus geschrieben.

## Manueller Neustart ab Firmware 3.3.1

`POST /api/restart` erwartet Formulardaten mit `token` aus `/api/status`.
Ein gültiger Aufruf antwortet mit HTTP 202 und `{"accepted":true}`.
Der Neustart folgt nach etwa 1,2 Sekunden. Die Vorgangsreservierung bleibt bis dahin bestehen.
Ein falsches Token ergibt HTTP 403. Ein laufender Vorgang oder die Startprüfung ergibt
HTTP 409. Kann der Neustart nicht geplant werden, folgt HTTP 503.
`/api/status` enthält das zusätzliche boolesche Feld `restart_pending`.
Gespeicherte WLAN- und Laufzeiteinstellungen bleiben erhalten.

## Heizungsschema ab Firmware 3.4.0

| Methode und Pfad | Aufgabe |
| --- | --- |
| GET `/schema` | Schema-Seite öffnen |
| GET `/api/schema` | Speicherstatus, Revisionen, Bildmaße und Anordnung lesen |
| POST `/api/schema` | Anordnung mit erwarteter Revision speichern |
| GET `/api/schema/image` | Gespeichertes PNG oder JPEG lesen |
| POST `/api/schema/image` | Normalisiertes PNG oder JPEG speichern |

Schreibende Aufrufe benötigen `X-CSRF-Token` aus `/api/status`. Der Layout-Upload
verwendet `Content-Type: application/json`, der Bild-Upload `application/octet-stream`.
Layout: `version` ist 1, `revision` ist die zuletzt gelesene Layoutrevision und `items`
ist eine Liste mit höchstens 45 Einträgen. Jeder Eintrag enthält `id`, `x`, `y`,
`visible` und `label`. X und Y sind Zahlen von 0 bis 100. IDs dürfen nicht doppelt sein.
Das Layout darf höchstens 16.384 Byte groß sein. Das Bildlimit steht in `max_image_bytes`.

Die Antwort auf erfolgreiches Speichern enthält `accepted: true` und die neue `revision`.
GET `/api/schema` enthält außerdem `storage_ready`, `image_revision`, `image_width`,
`image_height` und `items`. Die Bildrevision ist unabhängig von der Layoutrevision.
HTTP 409 bezeichnet einen belegten Vorgang oder eine inzwischen geänderte Anordnung.
HTTP 403 bezeichnet ein falsches Token. Ungültige Größe ergibt 413, falscher Inhaltstyp
415 und ungültiger Inhalt 400. Bei fehlendem Schema-Speicher folgt 503.

## Logo und Favicon ab Firmware 3.4.1

| Pfad | Inhalt |
| --- | --- |
| GET `/logo.png` | PX-Seitenlogo, PNG mit 192 × 192 Pixeln |
| GET `/favicon.png` | Favicon, PNG mit 32 × 32 Pixeln |
| GET `/favicon.ico` | ICO mit 16, 32, 48 und 64 Pixeln |

Die Binärdateien werden mit ihrer tatsächlichen Länge direkt aus der Firmware geliefert.
Sie verwenden `Cache-Control: no-store` und `X-Content-Type-Options: nosniff`.
Sie belegen keinen zusätzlichen Bereich für Benutzerdaten.

### MQTT-Einstellungen über HTTP

`/api/status.mqtt` liefert `enabled`, `connected`, `tls`, `host`, `port`, `username`,
`password_set`, `root`, `message`, `connections`, `publish_errors` und `rejected_keys`.
Es liefert weder das Passwort noch die MQTT-Verbindungskennung der Tasten.
`POST /api/mqtt` erwartet `token`, `enabled=0|1`, `host`, `port`, `tls=0|1`,
`username`, `password` und `clear_password=0|1` als Formulardaten.
Ein leeres Passwort behält den bisherigen Wert. Die Löschoption hat Vorrang.
Die Antwort bestätigt den angenommenen Auftrag. Der Hintergrundtask meldet das Speicherergebnis im Status.

## Lokaler Verlauf ab Firmware 3.6.0

| Methode | Pfad | Zweck |
| --- | --- | --- |
| GET | `/history` | Verlaufsseite |
| GET | `/api/history/status` | Objekt `history` mit Kapazität, Belegung, Grenzen und Einstellungen |
| GET | `/api/history/data?from=0&to=10000&limit=768` | Binäre Min/Max-Zeitkörbe |
| POST | `/api/history/settings` | `token`, `enabled`, `interval_s`, `flex1`, `flex2=0`, `flex3=0` |

`from` und `to` sind Millisekunden seit dem aktuellen Gerätestart. `from < to` ist erforderlich.
`to` darf höchstens 60 Sekunden nach der aktuellen Gerätezeit liegen. `limit`: 16 bis 1024.
Bei POST gilt `application/x-www-form-urlencoded`. Intervall: 1 bis 3600 Sekunden.
Ein belegter Vorgang führt zu HTTP 409; ungültige Felder zu 400 und ein falsches Token zu 403.

Das Binärformat ist Little Endian. Header: `PBH1`, Version uint32, Anzahl uint32,
Schrittweite uint32 (256), `from` uint64, `to` uint64. Headergröße: 32 Byte.
Danach folgen 256 Byte je Korb: erste/letzte Messzeit als uint64, dann vier Arrays mit
je 29 int16 für Minimum, Maximum, ersten und letzten gültigen Wert. Es folgen uint32
Lückenmaske und uint32 Messpunktzahl. Der ungültige Rohwert ist -32768.
Kanalfolge: zehn Haupttemperaturen, vier F1-Temperaturen, acht R-Ausgänge (R1..R7,R0),
drei HE-Ausgänge, vier F1-Ausgänge. Temperaturen durch 10 teilen; Haupt-HE durch 100;
übrige Ausgänge durch 2. Siehe `history_store.h` und `history.js`.

Statusfelder `removed_samples` und `dropped_events` zählen ersetzte Messpunkte bzw.
verlorene Übergaben vom Empfang an den Verlauf. `capture_reserved` meldet den vorübergehenden
Rohpuffer. `persistent` ist false. Weitere Erläuterungen stehen in [Verlauf](Verlauf.md).

### Kompaktes Verlaufsformat ab Firmware 3.6.1

Der Binärkopf verwendet Version 2. Die Schrittweite bleibt 256 Byte.
`count` zählt jetzt die tatsächlich übertragenen belegten Zeitkörbe; null ist zulässig.
Die Körbe sind chronologisch, aber nicht gleichmäßig verteilt. Ihre ersten und letzten
Messzeiten bestimmen ihre Position auf der Zeitachse. Der Browser unterstützt beide Versionen.
`limit` akzeptiert weiterhin 16 bis 1024; die Antwort wird auf höchstens 256 Körbe begrenzt.
Der Datenhandler startet einen eigenen Leseauftrag und kehrt sofort zurück.
Ein gleichzeitiger zweiter Leseauftrag erhält HTTP 429. Bei Speichermangel oder belegter
Verlaufssperre antwortet der Hintergrundauftrag mit HTTP 503.

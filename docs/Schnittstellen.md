# Lokale Schnittstellen

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

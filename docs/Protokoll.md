# ProBusX-Protokoll dieser Anlage

Diese Beschreibung beruht auf den beiden genannten Aufnahmen.
Sie ist keine Herstellerspezifikation.
Alle Offsets zählen ab dem ersten `A5` und beginnen bei Null.
Alle Zahlen in Kopffolgen sind hexadezimal.
Rx und Tx gelten aus Sicht der Bridge.
Die passive Aufnahme enthält keine elektrische Senderkennung.

## Telegramme

| Familie | Kopf | Parserlänge | Prüfung |
| --- | --- | ---: | --- |
| Hauptwerte | `A5 13 36 01 61` | 241 Byte | CRC16 und separate AA-Markierung |
| FlexBox 1 | `A5 13 40 01 F0` | 26 Byte | CRC16 |
| Display | `A5 13 36 01 55` | 5125 Byte | Feste Bildgröße; CRC offen |
| Reglerabfrage | `A5 13 36 01 34 50 C3` | 7 Byte | Exakte Bytefolge |
| Kurzstatus | `A5 13 36 01 70` bis `73` | 13 Byte | CRC16 |
| Weitere Kurztelegramme | `A5 13 36 01 80` bis `83` | 7 Byte | Bedeutung und CRC offen |
| Tastenantwort | `A5 B3 CODE CRC_HI CRC_LO` | 5 Byte | CRC16 |

Nach Hauptwerten und Display wurde jeweils ein zusätzliches `00` beobachtet.
Dieses Byte liegt außerhalb der genannten Parserlänge.
Auch zwischen anderen Telegrammen kommen Nullbytes vor.
Ihre genaue Zuordnung ist nicht bestätigt.

## Kopf und Adressen

| Offset | Beobachtung | Stand |
| --- | --- | --- |
| 0 | Startbyte `A5` | Belegt |
| 1 | `13` bei Datenfamilien | Formale Feldbedeutung offen |
| 2 | `36` beim Hauptregler; `40` bei F1 | Gerätezuordnung beobachtet; formale Adresse offen |
| 3 | `01` | Funktion offen |
| 4 | Familienkennung | Durch Mitschnitt zugeordnet |

Der Parser akzeptiert zusätzlich Kennung `35`.
Diese Kennung kommt in den beiden Aufnahmen nicht vor.
Sie ist keine bestätigte weitere Geräteadresse.

Die FlexBoxen sind laut Betreiber baugleich.
Ein DIP-Schalter legt die Modulnummer fest.
Für F1 ist die Buskennung `40` belegt.
Die Umrechnung von Modulnummer zu Buskennung ist noch offen.
F2 und F3 dürfen nicht aus aufeinanderfolgenden Kopfwerten abgeleitet werden.
Das gleiche Nutzdatenformat wird erwartet und muss bestätigt werden.

## CRC16

| Eigenschaft | Wert |
| --- | --- |
| Reflektiertes Polynom | `8408` |
| Anfangswert | `FFFF` |
| Abschließendes XOR | `0000` |
| Ergebnis im Telegramm | High-Byte vor Low-Byte |

Die CRC verarbeitet die Bits jedes Bytes vom niederwertigen Bit aus.

| Familie | CRC-Abdeckung, einschließlich Grenzen | CRC-High | CRC-Low |
| --- | --- | ---: | ---: |
| Hauptwerte | 0 bis 237 | 239 | 240 |
| FlexBox 1 | 0 bis 23 | 24 | 25 |
| Kurzstatus | 0 bis 10 | 11 | 12 |
| Tasten | 0 bis 2 | 3 | 4 |

Hauptdatenbyte 238 muss `AA` enthalten.
Es gehört nicht zur CRC-Abdeckung.
Eine falsche AA-Markierung zählt als eigener Strukturfehler.
Ein CRC-Fehler verhindert die Übernahme des Telegramms.
Das gilt auch bei ausgeschalteter Analyse.

Alle 360 aufgezeichneten Hauptwerttelegramme bestehen diese Regel.
Insgesamt bestehen 4065 CRC-geprüfte Telegramme beider Aufnahmen.
Die Displayfamilie gehört nicht zu dieser Zahl.

## Zahlen und Feldpositionen

Die folgenden Felder verwenden vorzeichenbehaftete 16-Bit-Wörter in Big Endian.
Der angegebene Offset bezeichnet das High-Byte.
Das Low-Byte folgt direkt danach.

```text
u = 256 × High + Low
Bei u ≥ 32768: Rohwert = u − 65536
Sonst: Rohwert = u
```

| Familie und Felder | High-Byte-Offsets | Umrechnung | Stand |
| --- | --- | --- | --- |
| Hauptwerte S1 bis S10 | 10, 12, 14, 16, 18, 20, 22, 24, 26, 28 | /10 °C | Positive Werte mit Anzeige verglichen |
| Hauptwerte R1 bis R7, R0 | 44, 46, 48, 50, 52, 54, 56, 58 | /2 % | Stellwerte zugeordnet |
| Hauptwerte HE1 bis HE3 | 60, 62, 64 | /100 % | HE1 = 15 % verglichen; andere null |
| F1S1 bis F1S4 | 5, 7, 9, 11 | /10 °C | Temperaturgruppe zugeordnet |
| F1R1 bis F1R3, F1HE im Haupttelegramm `36:61` | 114, 116, 118, 120 | /2 % | R1 und R3 mit aktiven Werten gegen Kurzstatus geprüft; R2 und HE bisher null |

Vorzeichen und Skalierungen werden zusätzlich durch den ursprünglichen Conexio-Webcode gestützt.
Negative Temperaturen kommen in den beiden Aufnahmen nicht vor.
Rohwert `2500`, hexadezimal `09 C4`, wird als unbenutzt oder ungültig behandelt.
Ein vollständiger Katalog aller Sensorfehler ist noch offen.
Ein Ausgangswert ist ein Stellwert und keine gemessene Drehzahl.
Ausgangswerte außerhalb 0 bis 100 % werden als unbekannt behandelt.

Die Hauptnutzdaten bei 5 bis 9, 30 bis 43 und 66 bis 237 sind noch nicht vollständig dekodiert.
Diese Lücken sind nicht als leere oder reservierte Felder bestätigt.
Bei F1 sind die Nutzbytes 21 bis 23 unbekannt.
Q1, Q2 und Leistung haben keine bestätigten Offsets.

## Display

Ein vollständiges Bild hat 160 × 128 Pixel mit vier Graustufen.
Jedes Pixel benötigt zwei Bit.
Daraus ergeben sich 5120 Nutzbytes nach dem fünf Byte langen Kopf.
Ein Byte enthält vier senkrecht angeordnete Pixel.
Die Spalten laufen von rechts nach links.

```python
index = (159 - x) * 32 + y // 4
wert = (payload[index] >> (2 * (y % 4))) & 3
grau = 255 - 85 * wert
```

Die Grauwerte lauten 255, 170, 85 und 0.
Wert 0 bezeichnet Weiß. Wert 3 bezeichnet Schwarz.
Die beiden Aufnahmen enthalten 720 vollständige Bilder.
Der beobachtete Takt beträgt etwa zwei Bilder je Sekunde.
Es wurden keine bestätigten Teilbildtelegramme gefunden.
Ein unveränderter Bildinhalt ist kein Empfangsausfall.

Die letzten beiden Nutzbytes sind in diesen Bildern `55 55`.
Sie sind Pixelinhalt und keine nachgewiesene CRC.
Kopfähnliche Bytes im Bildpuffer dürfen kein neues Telegramm starten.
Der Parser verwendet deshalb die feste Bildlänge.

## Tastenantworten

| Funktion | Code | Vollständige Antwort |
| --- | --- | --- |
| Loslassen | `00` | `A5 B3 00 1F 5F` |
| Hoch | `10` | `A5 B3 10 0F DE` |
| Rechts | `20` | `A5 B3 20 3E 5D` |
| Runter | `40` | `A5 B3 40 5D 5B` |
| Dreitastendruck | `70` | `A5 B3 70 6C D8` |
| Links | `80` | `A5 B3 80 9B 57` |
| Beobachtete Folgemeldung | `FF` | `A5 B3 FF 10 27` |

Alle 106 Tastenantworten folgen direkt auf eine passende `34`-Abfrage.
Die interne Bedeutung von `FF` ist noch offen.
`FF` ist keine belegte Reglerquittung.

Die normale Folge lautet `Taste → FF → 00 → FF`.
Jeder Code benötigt eine neue Reglerabfrage.
`00` folgt frühestens 500 ms nach dem Tastencode.
Für den Dreitastendruck gilt `70 → 00 → FF` ohne diese Haltezeit.
Die Sondertastenzuordnung beruht auf Aufnahme, Menüwechsel und Betreiberangabe.

## Nachweise

- Grundaufnahme: `probusx-09d0e9bb.pbxs`, etwa 300 Sekunden.
- Tastenaufnahme: `probusx-7b024fb4.pbxs`, etwa 60 Sekunden.
- CRC-Nachweis mit Rohstrom-Hashwerten: `tests/CRC_BELEG.json`.
- Parser: `firmware/src/protocol.h`.
- Feldübernahme und Sendelogik: `firmware/src/live.cpp`.
- PNG und Umrechnung: `home_assistant/custom_components/probusx/codec.py`.

Die vollständigen Aufnahmen sind nicht im Repository enthalten.
Weitere Fragen stehen unter [Offene Punkte](Offene-Punkte.md).

## Korrektur der F1-Ausgänge, Aufnahme vom 14.11.2026

Der Rohstrom mit SHA-256 `0aff072b9f2b0e602e672478ac5c9fcb36065da055510b66a4cb8efabe40b6bc`
enthält 120 Haupttelegramme, 240 F0-Telegramme und 240 Kurzstatusmeldungen vom Typ `70`.
Die PBXS-Aufnahme ist vollständig und meldet keine UART- oder Aufnahmepufferverluste.
1320 ProBusX-Telegramme tragen eine bekannte CRC. Alle bestehen die Prüfung.

Im F0-Telegramm sind die Bytes 13 bis 20 in allen 240 Fällen null.
Diese früher vermuteten Ausgangswörter sind keine bestätigte Istwertquelle.
Die tatsächlichen Ausgangswerte werden aus den Hauptdaten gelesen:

| Ausgang | Byte-Offsets, nullbasiert | Beispiel | Umrechnung | Gegenprobe |
| --- | --- | --- | --- | --- |
| F1R1 | 114–115 | `00 00` oder `00 C8` | /2 → 0 oder 100 % | Kurzstatus `70`, Byte 5; 4 aktive Haupttelegramme |
| F1R2 | 116–117 | `00 00` | vorläufig /2 | Kurzstatus `70`, Byte 6; nur null beobachtet |
| F1R3 | 118–119 | `00 C8` | /2 → 100 % | Kurzstatus `70`, Byte 7 = `64`; alle 120 Haupttelegramme |
| F1HE | 120–121 | `00 00` | vorläufig /2 | Kurzstatus `70`, Byte 8; nur null beobachtet |

Alle 120 Haupttelegramme stimmen in diesen vier Prozentwerten mit dem jeweils vorherigen Kurzstatus überein.
F1R1 ist etwa bei Sekunde 24, 54, 85 und 115 kurz aktiv. Diese Wechsel erscheinen unabhängig in beiden Telegrammtypen.
F1R3 bleibt in der gesamten Aufnahme aktiv. Ein echter Aus-Zustand von F1R3 wurde darin nicht aufgezeichnet.
Die Skalierung bei Zwischenwerten und ein aktiver HE-Ausgang bleiben gezielt zu prüfen.
Die Bedeutung der F0-Bytes 13 bis 23 wird mit dieser Korrektur nicht festgelegt.

Die Firmware nutzt die bestätigte CRC der Hauptdaten, bevor sie F1-Ausgänge übernimmt.
Ein nachfolgendes F0-Telegramm aktualisiert nur Temperaturen und überschreibt keine Ausgänge.
Die gemeinsame F1-Verfügbarkeit verwendet den älteren Zeitstempel aus F0-Temperaturen und Hauptdaten-Ausgängen.
Fällt eine Quelle aus, wird die F1-Gruppe nach drei Sekunden nicht verfügbar.
Die API zeigt beide Einzelalter zusätzlich als `ext_temperatures_age_ms` und `ext_outputs_age_ms`.

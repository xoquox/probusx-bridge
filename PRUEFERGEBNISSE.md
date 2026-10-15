# Prüfergebnisse für Projekt 0.3.1

Prüfdatum: 15.10.2026.
Quellstand: HA-Integration 0.3.1 und Firmwarequellen 3.2.1.
Ausgangsbasis: Integration 0.3.0 und Firmware 3.2.0.

## Bestandene Prüfungen

| Prüfung | Ergebnis |
| --- | --- |
| Aufnahme, Weblogik und OTA-Fehlerwege | 21 Tests bestanden |
| Bilddekodierung und Skalierung | 3 Tests bestanden |
| Projektwerkzeuge und Paketfilter | 5 Tests bestanden; einschließlich echtem lokalen Git-Commit |
| Native Protokollprüfung | Referenz-CRC, Vorzeichen, Blockgrenzen und Tastenfolge bestanden |
| Datenfehler | 1864 einzelne Nutzdaten-Bitfehler verworfen |
| Erholung und Diagnose | AA-Markierung, Teiltelegramm-Zeitgrenze, Pausen und Zeitfenster bestanden |
| Optionen | Grenzen und CRC-Prüfung bei ausgeschalteter Analyse bestanden |
| Originalaufnahmen | Beide Ströme mit 1-, 7- und 1024-Byte-Blöcken bestanden |
| Hauptwerte | Alle 360 aufgezeichneten Haupttelegramme CRC-gültig |
| CRC-Familien insgesamt | Alle 4065 aufgezeichneten geprüften Telegramme CRC-gültig |
| HA-Laufzeit | Home Assistant 2025.8.3 unter Python 3.13.16 bestanden |
| HA-Entities | Alle 61 Entities geladen und geprüft |
| Browser | Beide Prüfungen mit Playwright 1.51.0 bestanden |

Die 15 C++-Quelldateien behalten außerhalb von Kommentaren und Textmeldungen dieselben Programmtokens.
Der Vergleich berücksichtigt die Zusammenführung benachbarter C++-Textliterale.
Die Bus- und Sendelogik wurde für diese Projektaufbereitung nicht erweitert.

Die Browserprüfung umfasst Reglerseite, Optionen, WLAN, Aufnahme und OTA-Fehlerwege.
Die HA-Prüfung umfasst Bildcache, Aktualisierungsintervalle, Tasten und Verfügbarkeit.
Sie prüft außerdem getrennte HTTP-Fehler und die Maskierung des Sitzungstokens.

## Nicht ausgeführter ESP32-Neubau

Der vollständige ESP32-Build wurde in dieser Arbeitsumgebung nicht abgeschlossen.
Der benötigte Compilerdownload erhielt keine Netzwerkfreigabe.
Ein Firmwareabbild der Version 3.2.1 ist deshalb nicht Teil dieses Quellpakets.
Die vorhandenen 3.2.0-Abbilder wurden nicht als neuer Build ausgegeben.

Die ESP-IDF-Quellen basieren auf der zuvor gebauten Version 3.2.0.
Der native Parser und die Kernlogik wurden lokal geprüft.
Der GitHub-Workflow enthält den vollständigen ESP32-Build und die Abbildprüfung.
Sein erster erfolgreicher Lauf liefert die neuen Firmwaredateien.

## Grenzen

Die Prüfungen nutzen Originaldaten, synthetische Testdateien und lokale Simulatoren.
Es bestand kein Zugriff auf den laufenden Heizungsregler.
Ein realer Flash-, OTA- oder elektrischer Sendetest dieser Fassung wurde nicht ausgeführt.
FlexBox 2 und FlexBox 3 bleiben ohne bestätigte Buskennung.
Die Displayfamilie bleibt ohne nachgewiesene CRC-Regel.

Spätere Änderungen benötigen einen neuen Prüflauf.

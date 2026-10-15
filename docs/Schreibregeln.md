# Regeln für vereinfachtes technisches Deutsch

Dieses Projekt nutzt eigene Regeln für vereinfachtes technisches Deutsch.
Die Regeln orientieren sich am Grundgedanken von ASD-STE100.
Sie sind keine offizielle deutsche Übersetzung des Standards.
Eine vollständige Normkonformität wird nicht zugesichert.

## Satzbau

1. Einen Gedanken je Satz schreiben.
2. Möglichst höchstens 20 Wörter je Satz verwenden.
3. Das handelnde Bauteil oder Programm nennen.
4. Aktiv schreiben.
5. Unnötige Abkürzungen vermeiden.
6. Bedingungen vor die zugehörige Handlung setzen.

Beispiel:

> Bei einer falschen CRC verwirft der Parser das Telegramm.

## Anweisungen

1. Jede Handlung in einen eigenen nummerierten Schritt schreiben.
2. Die benötigte Datei oder Taste genau nennen.
3. Das erwartete Ergebnis nach der Handlung nennen.
4. Eine Einheit bei jeder einstellbaren Größe angeben.

Beispiel:

> Die Datei `probusx-bridge-waveshare-ota.bin` auswählen.
> Nach dem Neustart die Versionsnummer prüfen.

## Aussagen kennzeichnen

| Begriff | Bedeutung |
| --- | --- |
| Belegt | An den genannten Daten oder durch einen Test bestätigt |
| Beobachtet | Im Mitschnitt vorhanden; die Bedeutung kann noch offen sein |
| Vorläufig | Zuordnung benötigt einen weiteren Test |
| Offen | Noch nicht bestimmt |
| Angenommen | Technisch plausible Erwartung ohne Bestätigung |

Ein fehlender Nachweis darf nicht als bestätigte Gerätefunktion beschrieben werden.
Ein Test mit simulierten Daten muss als Simulation erkennbar sein.

## Einheitliche Begriffe

| Begriff | Verwendung im Projekt |
| --- | --- |
| Bridge | ESP32-Gerät zwischen ProBusX und lokalem Netzwerk |
| Telegramm | Vollständige ProBusX-Nachricht |
| Datensatz | Ein Transportblock im PBXS-Aufnahmeformat |
| Kopf | Führende Kennbytes eines Telegramms oder Datensatzes |
| Nutzdaten | Daten nach dem Kopf |
| Rohwert | Noch nicht skalierter Zahlenwert |
| Ausgangsstellwert | Vom Regler gemeldete Ansteuerung in Prozent |
| Zugangspunkt | Von der Bridge bereitgestelltes WLAN |
| Ersatzzugangspunkt | Zugangspunkt bei fehlender Heimnetzverbindung |
| OTA | Firmware-Update über das Netzwerk |
| CRC | Prüfsumme zur Erkennung von Datenfehlern |
| Rückfall | Start der vorherigen Firmware nach fehlgeschlagener Startprüfung |
| Modulnummer | Am DIP-Schalter eingestellte FlexBox-Nummer |
| Buskennung | Beobachteter Kennwert im Telegrammkopf |

## Quellcode

Kommentare erklären Zweck, Randbedingungen oder bekannte Grenzen.
Kommentare wiederholen nicht nur die nächste Programmzeile.
Öffentliche Schnittstellen und wichtige Nebenläufigkeit werden beschrieben.

Bezeichner aus Bibliotheken, API-Felder, JSON-Schlüssel und Entity-IDs bleiben unverändert.
Beispiele dafür sind `display_ms`, `async_setup_entry` und `camera.probusx_display`.
Compiler- und Bibliotheksmeldungen stammen von den jeweiligen Abhängigkeiten.
Sie können englisch sein.

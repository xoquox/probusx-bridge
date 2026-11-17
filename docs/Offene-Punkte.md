# Offene Punkte

## FlexBox 2 und FlexBox 3

Die Module sind laut Betreiber baugleich mit FlexBox 1.
Der DIP-Schalter setzt die Modulnummer.
Die Buskennung darf daraus noch nicht als `41`, `42`, `02` oder `03` abgeleitet werden.

Für die Bestätigung:

1. Das betreffende Modul mit dokumentierter DIP-Stellung betreiben.
2. Eine Rohaufnahme erstellen.
3. Den neuen Telegrammkopf bestimmen.
4. Die vier Temperaturen mit bekannten Messwerten vergleichen.
5. Ausgänge einzeln ändern und ihre Rohwerte vergleichen.
6. Erst danach Parserzuordnung und HA-Entities erweitern.

Der Parser verwirft noch unbekannte Kopffolgen.
Die Rohaufnahme erhält diese Bytes trotzdem.
Ein Zähler für unbekannte Köpfe beweist daher keinen elektrischen Fehler.

## F1-Ausgänge

F1R1 und F1R3 sind im Haupttelegramm bei 114 und 118 mit aktiven Werten geprüft.
F1R2 bei 116 und F1HE bei 120 brauchen aktive Vergleichswerte.
Für F1R3 fehlt noch eine gezielt aufgezeichnete Ein-/Aus-Umschaltung.
Zwischenwerte und die HE-Skalierung brauchen weitere Nachweise.
Die frühere Annahme, dass F0-Bytes 13 bis 20 die Istwerte enthalten, ist verworfen.

## Displayprüfung

Alle beobachteten Displaytelegramme enthalten vollständige Bilder.
Die Nutzdatenlänge beträgt 5120 Byte.
Eine CRC für diese Familie ist noch nicht nachgewiesen.
Die letzten beiden Pixelbytes `55 55` sind keine bestätigte Prüfsumme.

## Weitere Daten

- Bedeutung der übrigen Hauptdatenbytes.
- Formale Quell- und Zieladressierung.
- Zuordnung von Wärmemengen und Leistung.
- Vollständiger Katalog der Sensorfehler.
- Bedeutung der Folgemeldung `FF`.
- Bedeutung der Kurztelegramme `80` bis `83`.
- Echte Bus-Umschaltzeiten bei Tastenantworten.
- Längerer Betrieb mit weiteren Reglermodellen.

Diese Punkte sind keine Zusage für die aktuelle Version.

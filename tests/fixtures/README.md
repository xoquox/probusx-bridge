# Synthetische Testdaten

`hauptwerte.bin` ist ein künstliches 241-Byte-Telegramm.
Es verwendet den bestätigten Hauptdatenkopf und die bestätigte CRC-Regel.
Unbekannte Nutzdatenfelder stehen auf Null.
Die Temperatur- und Ausgangsbeispiele sind frei eingesetzte Prüfwerte.
Die AA-Markierung steht an Offset 238.
Die CRC steht an Offset 239 und 240.

Die Datei prüft das Parserverhalten.
Sie bestätigt nicht die Bedeutung unbekannter Felder.
Sie ersetzt nicht die zusätzliche Prüfung der echten Busaufnahmen.

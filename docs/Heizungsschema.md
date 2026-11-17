# Heizungsschema

Ab Firmware 3.5.1 stehen Logo und Navigation links. Das gespeicherte Schema beginnt direkt rechts oben.
Die Bildfläche wird an Fensterbreite und Bildschirmhöhe angepasst. Die relativen Markerpositionen bleiben erhalten.
Normale Hinweistexte entfallen. Hinweise für Bearbeitung und Fehler werden weiter angezeigt.


Die Seite **Heizungsschema** zeigt ein eigenes Bild mit Live-Werten des Reglers.
Bild und Anordnung liegen auf der Bridge. Ein anderer PC sieht denselben Stand.
Ein Neustart und ein normales OTA-Update behalten die gespeicherten Daten.

## Bild hochladen

1. Heizungsschema öffnen und auf **Schema bearbeiten** klicken.
2. Unter Hintergrundbild eine PNG-, JPEG-, WebP- oder BMP-Datei wählen.
3. Auf **Bild speichern** klicken. Bei einem vorhandenen Bild den Ersatz bestätigen.
4. Die Erfolgsmeldung abwarten.

Die Quelldatei darf höchstens 20 MiB groß sein. Der Browser verkleinert sie unter
Beibehaltung des Seitenverhältnisses auf höchstens 2048 Pixel je Seite.
Er speichert das Ergebnis als PNG oder, falls nötig, als JPEG.
Auf der Bridge sind je Bild höchstens 1.044.480 Byte vorgesehen.
SVG wird nicht übernommen. Die Bridge dekodiert das Bild nicht selbst.

Ein anderes Bild ersetzt nur den Hintergrund. Die bisher gesetzten Positionen und
Bezeichnungen bleiben bestehen. Bild und Anordnung werden getrennt gespeichert.
Das neue Bild ist sofort gespeichert, auch wenn die Bearbeitung danach beendet wird.

## Werte einrichten

1. Im Bearbeitungsmodus unter **Wert hinzufügen** einen Kanal wählen.
2. Auf **Hinzufügen** klicken. Der Wert erscheint im Bild und in der Liste.
3. Den Wert mit der Maus im Bild verschieben. Alternativ X und Y in Prozent eingeben.
4. In der Liste den Wert auswählen und bei Bedarf eine Bezeichnung eingeben.
5. Mit dem Haken neben dem Kanal den Wert ausblenden oder wieder einblenden.
6. Auf **Anordnung speichern** klicken und die Erfolgsmeldung abwarten.
7. Die Bearbeitung beenden.

Die Bezeichnung ist optional. Ohne Bezeichnung wird der Kanalname angezeigt.
Die Bezeichnung darf höchstens 40 Zeichen und höchstens 80 UTF-8-Bytes lang sein.
Ein Kanal kann einmal im Schema stehen. **Wert entfernen** löscht ihn aus der Anordnung.
Ein ausgeblendeter Wert behält seine Position und Bezeichnung.

**Bearbeitung beenden** fragt bei ungespeicherten Änderungen nach, ob diese verworfen
werden sollen. Auch das Verlassen der Seite warnt bei ungespeicherten Änderungen.
Verschieben und Live-Abrufe schreiben nicht selbstständig in den Flash.

Positionen beziehen sich auf die Bildmaße. Sie bleiben bei anderer Fenstergröße an
derselben Stelle des Bildes. Beim Ändern des Seitenverhältnisses des Hintergrunds
kann eine neue Anordnung nötig sein.

## Verfügbare Werte

- Hauptregler: S1 bis S10, R1 bis R7, R0 sowie HE1 bis HE3.
- Erste FlexBox: F1S1 bis F1S4, F1R1 bis F1R3 sowie F1HE.
- FlexBox 2 und 3 sind als Plätze vorbereitet. Ihre Werte erscheinen als „–“,
  solange die Busdekodierung dafür nicht bestätigt und ergänzt ist.

Temperaturen werden in Grad Celsius angezeigt. Ausgänge zeigen ihren Stellwert in Prozent.
Ein aktiver Ausgang erhält eine grüne Markierung. Für die Skalierung gelten dieselben
Regeln wie in der bestehenden Integration. Werte außerhalb von 0 bis 100 Prozent werden
nicht als gültige Ausgangswerte angezeigt.

„–“ steht für einen unbenutzten Temperatureingang, einen fehlenden Kanal oder veraltete
Daten. Veraltete Werte erhalten zusätzlich eine gestrichelte Markierung.
Die Anzeigeintervalle für Temperaturen und Ausgänge gelten auch für das Schema.
Die Bedienpause nach einer Stunde ohne Aktivität gilt ebenfalls.

## Gleichzeitige Zugriffe und Fehler

Jede gespeicherte Anordnung besitzt eine Revision. Wenn ein anderer Browser inzwischen
eine neue Anordnung gespeichert hat, wird der alte Stand beim Speichern abgelehnt.
Die Bearbeitung beenden, Änderungen bei Bedarf verwerfen und den aktuellen Stand neu laden.
So überschreibt ein alter Browser keine neue Anordnung.

Aufnahme, OTA, Neustart und Schemaübertragungen verwenden dieselbe Vorgangsreservierung.
Während eines solchen Vorgangs kann kein weiterer schreibender Auftrag beginnen.
Auch die Bildübertragung ist reserviert. Bereits laufende asynchrone Übertragungen
werden durch die HTTP-Sitzungsbereinigung nicht gelöscht.

Flash-Schreibvorgänge beim ausdrücklichen Speichern können, wie ein OTA-Update,
kurze Lücken im Bus-Empfang verursachen. Der Parser und die CRC-Prüfung bleiben aktiv
und erfassen Fehler. Die normale Schema-Anzeige schreibt nicht in den Flash.
Ein Dauertest auf der Waveshare-Hardware bleibt erforderlich.

## Datenspeicherung

Das vorhandene Partitionslayout und die beiden OTA-Bereiche bleiben unverändert.
Die Firmware registriert auf dem Haupt-Flash einen bisher freien Datenbereich:

| Bereich | Adresse ab | Größe |
| --- | --- | --- |
| Bildbank 0 | 0x620000 | 1 MiB |
| Bildbank 1 | 0x720000 | 1 MiB |
| Anordnungsbank 0 | 0x820000 | 32 KiB |
| Anordnungsbank 1 | 0x828000 | 32 KiB |

Das Ende ist 0x830000 exklusiv. Der Bereich benötigt insgesamt 2 MiB plus 64 KiB.
Die Firmware prüft die Flash-Größe. Die Partitions-API verhindert eine Überlappung mit
vorhandenen Partitionen. Die bestehende NVS mit WLAN-Daten wird nicht dafür verwendet.

Jeder Datentyp verwendet zwei Bänke. Nutzdaten und CRC werden zuerst geschrieben.
Erst der abschließende Kopf bestätigt den neuen Stand. Beim Start werden Köpfe und
Nutzdaten beider Bänke geprüft. Die jüngste gültige Bank wird verwendet.
Ein abgebrochener Upload schreibt noch keine neue Bank. Ein abgebrochenes Speichern
lässt die vorige Bank bestehen. Nach Stromausfall ist ein alter oder ein vollständig
bestätigter neuer Stand verfügbar.

Der Bereich muss bei künftigen Änderungen am Partitionslayout frei bleiben.
Ein vollständiges Löschen des Flash löscht auch das Schema. Eine ältere Firmware ohne
Schemafunktion zeigt die Daten nicht an, löscht diesen Bereich aber nicht von selbst.

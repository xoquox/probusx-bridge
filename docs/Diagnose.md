# Kommunikationsfehler suchen

Die Diagnose trennt Busfehler von HTTP- und WLAN-Fehlern.
Ein fehlerfreier Mitschnitt erklärt nicht automatisch einen späteren Aussetzer.
Seine Ursache muss anhand der Zähler und Zeiten eingegrenzt werden.

## Anzeigen

Die Seite **Analyse & Aufnahme** zeigt:

- Empfangene Bytes je Sekunde im Fenster der letzten bis zu 60 Sekunden.
- Fehlerquote der tatsächlich CRC-geprüften Telegramme.
- UART-Überläufe, Rahmenfehler, Paritätsfehler und Break-Ereignisse.
- Alter und Anzahl gültiger Telegramme je Familie.
- Längste Empfangspause und Anzahl erkannter Unterbrechungen.
- Die letzten 64 Diagnoseereignisse.
- Antwortzeit und Fehler der Browserverbindung.

Ungeprüfte Displaytelegramme zählen nicht als CRC-Erfolg.
Ohne geprüfte Telegramme gibt es keine CRC-Fehlerquote.
Eine falsche AA-Endemarkierung wird getrennt von einem CRC-Fehler gezählt.

## Pausengrenzen

| Familie | Unterbrechung nach mehr als |
| --- | ---: |
| Hauptwerte | 2,5 s |
| F1 | 1,5 s |
| Display | 1,5 s |
| Reglerabfragen | 1,5 s |

Eine länger andauernde Pause zählt nur einmal.
Ein gültiges Telegramm schließt die Pause.
Vor dem allerersten Telegramm wird keine verlorene Übertragung gezählt.

Die Werte sind keine genaue Paketverlustquote.
Für die Busfamilien ist keine zuverlässige Sequenznummer bekannt.
Der Zeitstempel entsteht beim UART-Lesen durch die Software.
Ein verzögerter Empfangstask kann deshalb ebenfalls als Pause erscheinen.

## Bei einem Aussetzer

1. Die Analyseseite öffnen.
2. Prüfen, ob die Telegrammzähler noch steigen.
3. Prüfen, welche Familie veraltet ist.
4. Die CRC- und UART-Zähler prüfen.
5. Den Diagnosebericht vor einem Neustart herunterladen.
6. Uhrzeit und betroffene Anzeige notieren.
7. Bei Parser- oder CRC-Fehlern eine kurze Rohaufnahme erstellen.

Der Browserbericht enthält bis zu 300 Messpunkte aus der geöffneten Analyseseite.
Das entspricht bei normalem Abruf ungefähr zehn Minuten.
Ein Neuladen des Browsers löscht diesen Browserverlauf.
Ein ESP-Neustart oder Analysewechsel setzt die ESP-Analysezähler zurück.

## Befunde einordnen

| Beobachtung | Nächster Prüfschritt |
| --- | --- |
| CRC-Fehler steigen, UART bleibt fehlerfrei | Rohtelegramme und erwartetes Format vergleichen |
| UART-FIFO oder Empfangspuffer läuft über | Empfangslast und Tasklaufzeiten prüfen |
| UART-Rahmenfehler steigen | Busparameter, Leitung und Empfangspfad prüfen |
| Nur Displaydaten werden alt | Bildfamilie und Parserzustand prüfen |
| ESP-Zähler steigen, HA-Daten fehlen | HTTP-Verbindung zwischen HA und ESP prüfen |
| Nur Browserabrufe scheitern | Browser- und WLAN-Verbindung prüfen |
| Bildtelegramme kommen an, Bild bleibt gleich | Kann ein unverändertes Reglermenü sein |
| Fremde Tastenantworten werden empfangen | Weiteren Sender oder Echo prüfen |

Unbekannte Kopffolgen beweisen keinen elektrischen Fehler.
Es können noch nicht unterstützte Telegramme sein.
Das betrifft auch zusätzliche FlexBoxen.
Die Rohaufnahme erhält diese Bytes unabhängig vom Live-Parser.

## Home Assistant

Die Integration bietet elf Diagnosesensoren.
Buszähler gelten seit dem ESP-Analysestart.
HA-Abruffehler gelten seit dem Laden der Integration.
Das Attribut `scope` nennt den Bezug.

Bei ausgeschalteter Analyse sind Busdiagnose-Entities nicht verfügbar.
Die HA-Verbindungsdiagnose bleibt aktiv.
Ein Bildabruf-Fehler lässt aktuelle Temperatursensoren weiterarbeiten.

Der HA-Diagnoseexport entfernt Sitzungstoken und Gerätekennung.
Der direkte ESP-Bericht enthält zur Zuordnung die MAC-basierte Gerätekennung.
Vor einer Veröffentlichung den Bericht auf eigene Anlagenangaben prüfen.

## Grenzen

Die Displayfamilie besitzt keine bestätigte CRC-Regel.
Eine einzelne Pixelverfälschung wird deshalb nicht sicher erkannt.
CRC schützt gegen Übertragungsfehler und ist keine Absenderauthentifizierung.
Die Zähler und Ereignisse liegen im RAM.
Sie werden nicht laufend in den Flash geschrieben.

# Lokaler Verlauf

## Bedienung

Die Seite **Regler** zeigt unter dem türkisfarbenen Bedienfeld zwei Wertkästen:
Hauptregler und FlexBox 1. Jeder Kasten enthält eine Tabelle für Eingänge und eine Tabelle
für Ausgänge. Aktive Ausgänge sind grün markiert. Ungültige oder veraltete Werte erscheinen als Strich.

Unter **Optionen → Verlauf und FlexBoxen** lassen sich diese Einstellungen ändern:

- die Verlaufsaufzeichnung einschalten oder pausieren;
- das Speicherintervall für Temperaturen zwischen 1 und 3600 Sekunden einstellen;
- FlexBox 1 ein- oder ausschalten.

Die Einstellungen bleiben nach einem Neustart erhalten. Die Auswahl schaltet die Auswertung und
Anzeige der FlexBox ab. Sie schaltet keine elektrische Versorgung und keinen Reglerausgang.
Bei ausgeschalteter FlexBox 1 werden deren MQTT-Werte als nicht verfügbar gemeldet.
FlexBox 2 und 3 sind ausgegraut. Ihre Buszuordnung ist noch nicht bestätigt.
Für sie werden keine angenommenen Messwerte erzeugt.

Die Seite **Verlauf** hat zwei Diagramme:

1. Temperaturen: S1 bis S10 und F1S1 bis F1S4.
2. Ausgänge: R1 bis R7, R0, HE1 bis HE3 und F1R1 bis F1R3 sowie F1HE.

Ein Klick auf einen Kanalnamen blendet die Kurve ein oder aus. Die Auswahl wird im jeweiligen
Browser gespeichert. Mit **Alle ein** und **Alle aus** werden alle Kanäle eines Diagramms geschaltet.
Historische FlexBox-Kurven bleiben auswählbar, auch wenn die aktuelle FlexBox-Auswertung abgeschaltet ist.

Die Zeitwahl bietet letzte Stunde, letzte sechs Stunden, letzten Tag, letzte Woche und alle
vorhandenen Werte. **Von** und **Bis** erlauben einen eigenen Zeitraum. Mit gedrückter linker
Maustaste einen Bereich im Diagramm markieren. Beide Diagramme übernehmen diesen Zoom.
**Zoom zurücksetzen** stellt den Zeitraum vor dem Zoomen wieder her.
Die Maus zeigt an der Zeitposition die Werte bzw. Minima und Maxima an.
Die Ansicht ruft den Verlauf alle 15 Sekunden neu ab. **Aktualisieren** lädt ihn sofort.
Eine Stunde ohne Bedienung pausiert wie bisher die automatischen Webabrufe.

## Welche Werte gespeichert werden

Ein Messpunkt enthält alle 29 Rohwerte und die Zeit seit dem Gerätestart. Seine Größe beträgt
72 Byte. Temperaturmesspunkte werden im gewählten Intervall gespeichert. Ausgangsänderungen
und Änderungen der Messwertgültigkeit erzeugen zusätzliche Messpunkte.
So bleibt ein kurzer beobachteter Pumpenlauf auch zwischen zwei Temperaturintervallen erhalten.
Nicht im Bus empfangene Zustände können nicht nachträglich ergänzt werden.

Der Bus-Empfang übergibt die Zustände ohne Warten an eine Warteschlange mit 64 Einträgen.
Der Verlauf verarbeitet sie in einem eigenen Task. Falls diese Schlange überläuft, steigt
`dropped_events`. Der Verlauf setzt dann eine sichtbare Messlücke, statt eine durchgehende
Aufzeichnung vorzutäuschen. Ab drei Sekunden ohne passende Busdaten sind die Werte ungültig.
Sensorrohwert 2500 wird wie in der Live-Anzeige nicht als reale Temperatur gezeichnet.

Der Browser erhält höchstens 256 Zeitkörbe pro Abruf. Jeder Korb enthält Anfang, Ende, Minimum,
Maximum und Gültigkeitslücken je Kanal. Damit bleiben Ausschläge auch bei langen Zeiträumen
sichtbar. Die genaue Reihenfolge mehrerer Wechsel innerhalb eines Korbs ist in dieser Ansicht
nicht aufgelöst. Für mehr Detail den Zeitraum verkleinern. Leere Zeitkörbe allein sind keine
Messlücke. Ungültige Messwerte unterbrechen die Kurve.

## Speicher

Der Verlauf liegt ausschließlich im PSRAM. **Neustart, Stromausfall und OTA-Neustart löschen ihn.**
Die Firmware schreibt diesen Verlauf nicht in den Flash. WLAN, Schema und Optionen bleiben
weiterhin dauerhaft gespeichert. Home Assistant kann unabhängig davon seine Historie führen.

Der Verlauf wächst in Blöcken mit je 64 KiB. Ein Block enthält 910 Messpunkte. WLAN und MQTT
starten vor dem Wachstum. Zusätzlich bleiben mindestens 512 KiB PSRAM für laufende Vorgänge
frei, soweit der vorherige Systembedarf dies zulässt. Interner RAM wird nicht mit Verlaufsdaten
gefüllt. Die tatsächliche Kapazität wird auf der Verlaufsseite angezeigt.

Es gibt keine feste Reservierung von 2 MiB für eine ungenutzte Rohaufnahme. Beim Start einer
Rohaufnahme fordert deren Ringpuffer 2 MiB am Stück an. Zuerst werden ungenutzte Verlaufsblöcke
freigegeben. Reicht dies nicht, entfallen die ältesten belegten Blöcke, bis der Puffer und die
Betriebsreserve Platz haben. Wegen Blockgrößen und Speicherfragmentierung kann mehr als 2 MiB
Verlaufskapazität entfallen. Die neueren Daten bleiben erhalten, soweit die Reservierung es erlaubt.
Reicht der Speicher auch nach Freigabe nicht, startet die Aufnahme nicht.

Nach Ende, Abbruch oder Verbindungsverlust wartet die Firmware auf den ruhenden Bus-Empfang
und gibt alle ausgeliehenen Ringpuffereinträge zurück. Erst dann wird der Rohpuffer freigegeben.
Der Verlauf übernimmt den freien Speicher wieder. Bereits gelöschte Messpunkte kehren nicht zurück.
Auch ohne Rohaufnahme ersetzt ein voller Verlauf selbsttätig seine ältesten Datenblöcke.

Beispiel: 7 MiB reichen für etwa 101920 Messpunkte. Ohne zusätzliche Ausgangswechsel sind das
bei zehn Sekunden Intervall rund 11,8 Tage. Häufige Wechsel verkürzen diese Dauer.
Die Wochenwahl zeigt nur tatsächlich vorhandene Daten. Sie garantiert keine Woche Speicherdauer.

## Zeitangaben

Gespeichert wird die Zeit seit dem Gerätestart. Der Browser ordnet sie beim Abruf seiner aktuellen
Uhrzeit zu und zeigt lokale Datumsangaben an. Eine geänderte Browseruhr kann diese Zuordnung
verschieben. Es ist kein manipulationssicheres Messarchiv. Nach einem Neustart beginnt ein neuer Verlauf.

## Prüfen am Gerät

1. Optionen speichern und die Seite neu laden. Intervall und FlexBox-Auswahl müssen erhalten bleiben.
2. Einen Ausgang am Regler umschalten und beide Diagramme prüfen.
3. Den Beginn und die Zahl der Messpunkte notieren.
4. Eine Rohaufnahme starten und stoppen. Die Kapazität muss währenddessen sinken und danach wachsen.
5. Einmal einen Download abbrechen. Danach müssen eine neue Aufnahme und der Verlauf funktionieren.
6. Mit wieder eingeschalteter FlexBox 1 deren Live-Werte und MQTT-Verfügbarkeit prüfen.

Die automatischen Prüfungen ersetzen diesen Test am tatsächlichen Board nicht.

## Übertragung ab Firmware 3.6.1

Die Abfrage läuft in einem eigenen Task mit niedrigerer Priorität als MQTT.
Der zentrale Webserver wartet weder auf die Berechnung noch auf die Übertragung.
Es läuft höchstens ein Verlaufsabruf. Weitere Abrufe erhalten HTTP 429.
Leere Zeitkörbe werden nicht übertragen. Ihre Zeitspanne bleibt auf der Achse erhalten.
Eine ganz leere Antwort enthält nur den 32-Byte-Kopf.
Ein voller Abruf enthält höchstens 64 KiB Messdaten plus 32 Byte Kopf.
Bei hoher Belegung sind weniger Zeitkörbe möglich. Zum Auflösen enger Wechsel hineinzoomen.

Die Übertragung prüft vor jedem Block eine Gesamtfrist von acht Sekunden.
Ein einzelner Socket-Sendeaufruf hat zusätzlich eine Sekunde Wartefrist. Eine bereits laufende Sendung kann die Acht-Sekunden-Frist deshalb noch um diese Wartezeit überschreiten. Nach Fehler oder
Abbruch wird die asynchrone Anfrage abgeschlossen und der Antwortspeicher freigegeben.
Der Leerlaufbereiniger schützt die Sitzung während dieses Auftrags.
Während der Netzwerkübertragung wird keine Verlaufssperre gehalten.

Die Statusfelder `query_busy`, `query_failed` und `query_bytes` melden einen laufenden Abruf,
abgebrochene Übertragungen und die geplante Größe der letzten Antwort.
`largest_psram_block` zeigt den größten freien PSRAM-Block. Dies ergänzt `free_psram`.
Die bisherige HA-Anzeige „Freier Heap“ misst dagegen den freien internen RAM.

Die Frist wird auch im Socket-Sendeweg geprüft. Das gilt für wiederholte Teilsendungen
innerhalb eines HTTP-Blocks. Null gesendete Byte bei einem nicht leeren Block gelten als
Fehler, damit die interne HTTP-Schleife keinen Nullfortschritt endlos wiederholt.

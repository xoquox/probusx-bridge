# Änderungen

## 0.6.1 / Firmware 3.6.1

- Verlaufsdaten im Hintergrund übertragen. Der zentrale HTTP-Task bleibt für andere Seiten frei.
- Leere Zeitkörbe entfernen; Antwort auf 64 KiB Messdaten plus Kopf begrenzen.
- Sendezeit prüfen, langsame oder abgebrochene Abrufe abschließen und Speicher freigeben.
- Asynchrone Verlaufssitzungen ausdrücklich vor der Leerlaufbereinigung schützen.
- Neue Prüfungen führen die tatsächlichen HTTP-Handler und die Übertragungslogik aus.
- Diagnose um Abrufstatus, Antwortgröße und größten freien PSRAM-Block ergänzen.


## 0.6.0 / Firmware 3.6.0

- Getrennte Live-Tabellen für Hauptregler und FlexBox 1.
- FlexBox 1 in den Optionen abschaltbar; FlexBox 2 und 3 ausgegraut.
- Lokaler RAM-Verlauf mit zwei Diagrammen, Kanalwahl, Mauszoom und Datumsauswahl.
- Temperaturintervall dauerhaft einstellbar; Ausgangswechsel zusätzlich erfasst.
- PSRAM-Ringpuffer gibt Platz für Rohaufnahmen frei und wächst danach wieder.
- Ungültige Werte und verlorene Verlaufseinträge erscheinen als Messlücken.
- Keine dauerhafte Speicherung des Verlaufs über einen Neustart.


## 0.5.2 / Firmware 3.5.2

- F1-Ausgänge aus dem Haupttelegramm bei Byte 114 bis 121 lesen.
- F0-Telegramme überschreiben keine Ausgangswerte mehr mit Null.
- F1R1 und F1R3 gegen unabhängige Kurzstatusmeldungen mit aktiven Werten geprüft.
- F1-Frische aus Temperatur- und Ausgangsquelle gemeinsam bestimmen; Einzelalter über API ausgeben.
- Rohaufnahme bleibt ohne Tastensenden. Die bisherige Aufnahmesperre ist dokumentiert.


## 0.5.1 / Firmware 3.5.1

- Logo, Verbindungsstatus, Navigation und Firmwarestatus in einem linken Block zusammengefasst.
- Inhalt beginnt direkt neben der Seitenleiste am oberen Rand.
- Überflüssige Kopfzeile und zwei normale Schema-Hinweise entfernt.
- Schema-Bild an die verfügbare Bildschirmhöhe angepasst. Bild und Marker behalten dieselben relativen Koordinaten.
- Desktopansicht, Bearbeitung, Tasten, WLAN, MQTT und OTA im Browser geprüft.


## 0.5.0 / Firmware 3.5.0

- Native MQTT-Anbindung mit 61 Entities und automatischer HA-Erkennung.
- Temperaturen, Ausgänge und Diagnose als JSON; Display als binäres PNG.
- Fünf MQTT-Tasten mit Verbindungskennung und gemeinsamer Busreservierung.
- Broker-Einstellungen mit Passworterhaltung und dauerhafter NVS-Speicherung.
- MQTT-Wiederverbindung, Ausfallmeldung und getrennte Frischezustände.
- TLS mit öffentlichen Zertifikatsketten als Option.
- Bisherige HTTP-Integration als Altbestand erhalten; Wechsel dokumentiert.


## 0.4.1 – Firmware 3.4.1

- Türkises PX-Logo im Seitenkopf.
- Favicon als PNG und ICO mit mehreren Größen.
- 45-Grad-Abschrägungen an Flächen, Schaltflächen, Feldern und Wertanzeigen.
- Sichtbarer innerer Fokusrahmen für die Tastaturbedienung.
- Lokale Bildauslieferung mit fester Binärlänge aus dem Firmware-Flash.
- Keine Änderung an Busprotokoll, Schema-Bearbeitung, WLAN oder Home Assistant.

## 0.4.0 – Firmware 3.4.0

- Eigene Seite Heizungsschema mit gespeichertem Hintergrundbild.
- Sensoren und Ausgänge frei verschieben, ein-/ausblenden und optional beschriften.
- Anordnung auf der Bridge speichern und in anderen Browsern verwenden.
- Getrennte Bild- und Layoutbänke mit CRC und atomarem Standwechsel.
- Konflikte beim Speichern aus mehreren Browsern erkennen.
- Bestehendes OTA-Layout, WLAN-Daten, Busdekodierung und HA-Dateien bleiben erhalten.

## 0.3.3 – Firmware 3.3.1

- Automatische Abrufe pausieren nach einer Stunde ohne Bedienung auf allen drei Webseiten.
- Ein Klick, eine Mausbewegung oder eine Tastatureingabe setzt die Anzeige fort.
- Laufende Aufnahmen und Updates werden durch die Bedienpause nicht abgebrochen.
- Der HTTP-Server kann alte freie Sitzungen bei Platzmangel schließen. TCP-Keepalive erkennt abgebrochene Verbindungen.
- Ein Neustart ist unter Optionen möglich. Aufnahme, Startprüfung, Update und WLAN-Aktion sperren den Neustart.
- WLAN-Bindung und Einstellungen bleiben erhalten. Home Assistant und das Busprotokoll sind unverändert.

## Projekt 0.3.2 / Firmware 3.3.0

- Vollständige WLAN-Zugangspunktliste mit SSID, BSSID, Kanal und Signalstärke.
- Gleiche SSIDs bleiben einzeln sichtbar und auswählbar.
- Verborgene SSIDs werden als eigene Zugangspunkte angezeigt.
- Automatische Wahl oder feste BSSID-Bindung einstellbar.
- Feste Bindung bleibt nach Neustarts und Wiederverbindungen erhalten.
- Kein automatischer Wechsel auf einen fremden Zugangspunkt bei fester Bindung.
- BTM und MBO bleiben aus, damit der Treiber die feste Bindung nicht aufhebt.
- Aktuelle BSSID, Signalstärke und Kanal werden getrennt vom gespeicherten Ziel angezeigt.
- Bisherige WLAN-Daten werden ohne Zugangsdatenverlust als automatische Wahl übernommen.
- Neue native Tests für NVS-Übernahme und BSSID-Verhalten ergänzt.
- Browserprüfung umfasst 44 Treffer, gleiche SSIDs, Zielausfall und Moduswechsel.
- Projektversion steht jetzt in `VERSION`. Die HA-Integration bleibt bei 0.3.1.

## Projekt 0.3.1 / Firmware 3.2.1

- Vollständige Quellen als eigenständiges GitHub-Projekt geordnet.
- Firmwareverzeichnis in `firmware` umbenannt.
- Neue Abbildnamen verwenden `probusx-bridge`.
- WLAN-Zugangspunkt in `ProBusX-Bridge` umbenannt.
- Deutsche Kommentare und zusätzliche Erklärungen ergänzt.
- Feste Schreibregeln für technisches Deutsch hinzugefügt.
- Installation, Bedienung, Entwicklung und Schnittstellen dokumentiert.
- Automatische Prüfungen und Firmware-Build für GitHub ergänzt.
- Paketprogramm und lokale Git-Vorbereitung ergänzt.
- FlexBox-Modulnummer von der beobachteten Buskennung getrennt beschrieben.
- Bestehende API-Felder, OTA-Projektkennung und Entity-IDs beibehalten.

FlexBox 2 und FlexBox 3 erhalten noch keine geratenen Buskennungen.
Die bekannte Telegrammdekodierung bleibt auf den nachgewiesenen Stand begrenzt.

## Ausgangsstand 0.3.0 / Firmware 3.2.0

- Getrennte Seiten für Regler, Analyse und Optionen.
- Gespeicherte Intervalle und Sendefreigabe.
- CRC-Prüfung unabhängig von der Analyseoption.
- Drei vorbereitete FlexBox-Plätze in der API.
- Schnellere Bildabfrage und gemeinsamer HA-Bildcache.

HTTP-Sitzungen ohne neuen Request werden nach 30 Sekunden geschlossen. Die Prüfung
läuft alle fünf Sekunden im HTTP-Task. Während einer Vorgangsreservierung wird diese
Bereinigung ausgesetzt. Dadurch bleiben laufende Aufnahmen und OTA-Aufträge geschützt.
Nach deren Abschluss setzt die Bereinigung wieder ein. Home Assistant öffnet bei Bedarf
automatisch eine neue HTTP-Verbindung. Seine Entitäten werden nicht durch die Browserpause angehalten.

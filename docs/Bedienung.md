# Bedienung und Update

## Seiten

| Seite | Pfad | Inhalt |
| --- | --- | --- |
| Regler | `/` | Display, Messwerte und fünf Tasten |
| Analyse & Aufnahme | `/analysis` | Kommunikation prüfen und Rohdaten aufnehmen |
| Optionen | `/options` | WLAN, Intervalle, Freigaben und Firmware-Update |

„Sniffer“ bezeichnet hier die passive Rohdatenaufnahme.
Neue Projekt- und Firmwaredateien verwenden diesen Begriff nicht mehr im Namen.

## Intervalle

| Einstellung | Standard | Erlaubter Bereich |
| --- | --- | --- |
| Display | 250 ms | 100 bis 5000 ms |
| Temperaturen | 1000 ms | 250 bis 60000 ms |
| Ausgänge | 1000 ms | 250 bis 60000 ms |

Diese Werte ändern die Anzeige- und Abrufintervalle.
Sie ändern nicht den Sendetakt des Reglers.
Der bisherige Regler sendet etwa zwei Displaybilder je Sekunde.
Jedes Bild enthält 5120 Pixelbytes.

Die Bridge hält das neueste vollständige Bild vor.
Sie speichert keinen vollständigen Film.
Bei einem langen Anzeigeintervall können Blinkzustände ausgelassen werden.

## Tasten

Die Tasten sind ohne gespeicherte Sperre freigegeben.
Eine bewusst gesetzte Sperre bleibt nach einem Neustart erhalten.
Während einer Rohaufnahme sind Tastensendungen gesperrt.

Ein Klick erzeugt einen Tastenvorgang.
Die Richtungstasten werden etwa 500 ms gehalten.
Gedrückthalten und automatische Wiederholung sind nicht umgesetzt.
Die Mitteltaste sendet den Dreitastendruck mit Code `70`.

Die Bridge antwortet nur auf die passende Reglerabfrage.
Ohne Reglerabfrage wird kein Tastencode gesendet.
Ein noch nicht gesendeter Tastenvorgang verfällt nach zwei Sekunden.
Ein begonnener Vorgang muss seine Loslassfolge abschließen.
Bei Busausfall kann er weitere Vorgänge bis zur nächsten Reglerabfrage blockieren.

**Zur Übertragung vorgemerkt** bestätigt nur die Annahme durch die Bridge.
Die Meldung bestätigt keine Ausführung durch den Regler.
Nach einem HTTP-Fehler wird der Tastendruck nicht automatisch wiederholt.

## Rohaufnahme im Browser

1. Die Analyseseite öffnen.
2. Die Aufnahmedauer zwischen 1 und 3600 Sekunden wählen.
3. **Aufnahme starten & herunterladen** wählen.
4. Den Browserdownload vollständig abwarten.
5. Bei Bedarf **Aufnahme beenden** wählen.

Die PBXS-Datei wird während der Aufnahme zum PC übertragen.
Die Bridge legt keine vollständige Aufnahme im Flash ab.
Eine langsame Verbindung kann den Aufnahmepuffer füllen.
Der Bericht nennt dann verworfene Bytes und Datensätze.

Eine PBXS-Datei auswerten:

```sh
python firmware/capture.py decode aufnahme.pbxs auswertung --hex
```

Das Programm erzeugt `auswertung.bin`, `auswertung.jsonl` und `auswertung.summary.json`.
Eine bestehende Datei wird nicht überschrieben.

## Rohaufnahme über TCP

```sh
python firmware/capture.py capture 192.168.178.90 aufnahme --seconds 60 --hex
```

Die IP-Adresse durch die Adresse der Bridge ersetzen.
Der Standardport ist `4196`.
Dieser Dienst erwartet einen PBXS-Aufnahmeauftrag.
Er ist kein transparenter RS485-TCP-Adapter.

## Firmware über WLAN aktualisieren

1. Eine laufende Aufnahme beenden.
2. Die Seite **Optionen** öffnen.
3. Unter **Firmware-Update** die Datei `probusx-bridge-waveshare-ota.bin` auswählen.
4. **Update installieren** wählen.
5. Die Stromversorgung während des Updates beibehalten.
6. Den Neustart und die Startprüfung abwarten.
7. Die neue Versionsnummer prüfen.

Die Startprüfung dauert etwa 20 Sekunden.
Sie prüft den Start der Firmware, die UART und die lokale HTTP-Verbindung.
Sie bestätigt keine Antwort des Heizungsreglers.
Bei fehlender Startbestätigung wird die vorherige Firmware wieder gestartet.

OTA übernimmt WLAN-Daten und Optionen.
Das USB-Gesamtabbild setzt diese Daten zurück.
Das USB-Abbild darf nicht über das Web-Update installiert werden.

## Analyse ausschalten

Die Analyse kann auf der Analyseseite oder der Optionenseite ausgeschaltet werden.
Dann stoppen Statistiksammlung und Ereignisaufzeichnung.
Die CRC-Prüfung bleibt aktiv.
Beschädigte Telegramme werden weiterhin verworfen.
Beim erneuten Einschalten beginnen die Analysezähler neu.

## WLAN-Ausfall

Die Bridge aktiviert bei fehlender IP-Verbindung den Ersatzzugangspunkt.
Sein Name lautet `ProBusX-Bridge`.
Die Adresse lautet `http://192.168.4.1`.
Das voreingestellte Passwort lautet `probusx12345`.

Die Oberfläche verwendet HTTP und einen Sitzungstoken.
Sie besitzt keine Benutzerkonten oder TLS-Verschlüsselung.
Den Zugriff auf das vertrauenswürdige lokale Netzwerk begrenzen.
Für Fernzugriff die vorhandene Home-Assistant-Verbindung verwenden.

# WLAN-Zugangspunkt wählen

Ab Firmware 3.3.0 zeigt die Bridge jeden gefundenen 2,4-GHz-Zugangspunkt einzeln.
Gleiche WLAN-Namen werden nicht zusammengefasst.
Die bisherige Begrenzung auf 32 Suchtreffer entfällt.

## Anzeige

Jeder Suchtreffer enthält:

- WLAN-Name, auch SSID genannt.
- BSSID des Zugangspunkts.
- Funkkanal.
- Signalstärke in dBm.
- Kennzeichnung eines offenen WLANs.

Die BSSID bezeichnet die MAC-Adresse des jeweiligen WLAN-Zugangspunkts.
Sie ist nicht zwingend die aufgedruckte Geräte- oder LAN-MAC-Adresse.
Bei mehreren Zugangspunkten zählt jeweils die BSSID des 2,4-GHz-WLANs.

Die Bridge unterstützt kein 5-GHz-WLAN.
Die Suche zeigt daher keine Zugangspunkte an, die ausschließlich auf 5 GHz senden.
Ein verborgener WLAN-Name erscheint als **SSID verborgen**.
Seine BSSID, sein Kanal und seine Signalstärke bleiben sichtbar.

## Feste Bindung einrichten

1. Die Seite **Optionen** öffnen.
2. Unter **WLAN** auf **Zugangspunkte suchen** klicken.
3. Den gewünschten Zugangspunkt anhand seiner BSSID auswählen.
4. Die Auswahl prüfen. Der Modus wechselt dabei auf **Fest**.
5. Den WLAN-Namen prüfen. Bei verborgener SSID den bekannten Namen eingeben.
6. Das WLAN-Passwort erneut eingeben. Das gespeicherte Passwort wird nicht angezeigt.
7. **WLAN speichern & verbinden** wählen.
8. Nach der Wiederverbindung die angezeigte BSSID prüfen.

Die BSSID kann auch direkt eingegeben werden.
Das Format lautet zum Beispiel `A2:B3:C4:D5:E6:F7`.
Groß- und Kleinschreibung sind beim Speichern gleichwertig.
Nulladresse, Multicast und Broadcast werden abgelehnt.

Die feste Bindung wird zusammen mit dem WLAN-Namen und Passwort gespeichert.
Sie gilt nach Neustarts und bei jedem erneuten Verbindungsversuch.
Ein stärkerer anderer Zugangspunkt mit gleicher SSID wird dabei nicht verwendet.
Die Bridge bindet keinen festen Funkkanal.
Derselbe Zugangspunkt darf deshalb seinen Kanal wechseln.

Die Bindung ist zunächst eine gespeicherte Zielvorgabe.
Sie bestätigt noch keine erfolgreiche Verbindung.
Der Abschnitt zum aktuellen Zugangspunkt zeigt die tatsächlich bestehende Verbindung.

## Automatische Wahl verwenden

1. Unter **Zugangspunktwahl** den Modus **Automatisch** wählen.
2. WLAN-Name und Passwort eingeben.
3. **WLAN speichern & verbinden** wählen.

Der BSSID-Filter wird entfernt.
Beim Verbindungsaufbau sucht die Bridge alle unterstützten Kanäle ab.
Der Treiber ordnet passende Zugangspunkte nach Signalstärke.
Ein Wechsel zwischen bereits verbundenen Zugangspunkten wird nicht regelmäßig erzwungen.

## Aktuelle Verbindung prüfen

Die WLAN-Seite zeigt die aktuelle BSSID, den Kanal und die Signalstärke.
Die Werte werden mit dem Gerätestatus aktualisiert.
Bei einer getrennten Verbindung wird kein alter Zugangspunkt als aktuell angezeigt.
Die gespeicherte Ziel-BSSID bleibt getrennt davon sichtbar.

## Ziel nicht erreichbar

Bei fester Bindung bleibt die Bridge auf den ausgewählten Zugangspunkt beschränkt.
Sie wechselt nicht automatisch zu einem anderen Zugangspunkt mit demselben WLAN-Namen.
Sie versucht weiterhin, den gespeicherten Zugangspunkt zu erreichen.

Nach einem Verbindungsverlust wird der vorhandene Ersatzzugangspunkt wieder aktiviert.
Die bisherige Wartezeit beträgt 15 Sekunden ohne IP-Verbindung.
Beim Start ist der Zugangspunkt bereits aktiv.
Während einer Rohaufnahme oder eines Updates bleibt der Funkzustand reserviert.
Eine erforderliche Umschaltung wartet dann auf das Ende dieses Vorgangs.

Zugangsdaten des Ersatzhotspots:

- Name: `ProBusX-Bridge`
- Passwort: `probusx12345`
- Adresse: `http://192.168.4.1`

Darüber kann eine andere BSSID oder die automatische Wahl gespeichert werden.
**Gespeichertes WLAN vergessen** löscht auch die feste Bindung.

## Update und gespeicherte Daten

Das OTA-Update übernimmt bisherige WLAN-Zugangsdaten.
Nach dem Update die Weboberfläche mit Strg+F5 neu laden.
Erst dann sind die neuen Auswahlfelder in einem bereits geöffneten Browser sichtbar.
Ein Datensatz aus Firmware 3.2.x wird als automatische Wahl geladen.
Die vorhandene Verbindung benötigt daher keine neue WLAN-Einrichtung.

Beim Speichern verwendet Firmware 3.3.0 das neue NVS-Format der Version 2.
Ein späterer Rückwechsel auf Firmware 3.2.x kann dieses Format nicht lesen.
Dann muss das WLAN über den Ersatzhotspot neu eingerichtet werden.

Beschädigte Bindungsdaten werden nicht durch automatische Wahl ersetzt.
Die Bridge verwendet dann den Ersatzzugangspunkt zur erneuten Einrichtung.

## Technische Referenz

Die Bindung verwendet `wifi_sta_config_t.bssid_set` und `bssid`.
Clientseitige BTM- und MBO-Optionen bleiben aus.
Diese Optionen könnten laut ESP-IDF die feste BSSID für einen Wechsel aufheben.

- [ESP-IDF-WLAN-Schnittstelle](https://docs.espressif.com/projects/esp-idf/en/v5.5/esp32s3/api-reference/network/esp_wifi.html)
- Implementierung: `firmware/src/wifi_policy.h` und `firmware/src/web.cpp`.

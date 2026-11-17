# Home Assistant über MQTT verbinden

Ab Projekt 0.5.0 kann die Bridge Werte, Bilder und Tastendrücke über MQTT übertragen.
Die Bridge hält dafür eine TCP-Verbindung zum Broker.
Home Assistant verbindet sich mit demselben Broker.
Die MQTT-Anbindung braucht keine HTTP-Abfragen an die Bridge und keine eigene HA-Komponente.

## Einrichten

1. Firmware 3.5.0 per OTA auf die Bridge laden. WLAN, BSSID-Bindung, Schema und Busoptionen bleiben gespeichert.
2. Einen MQTT-Broker bereitstellen. Für Home Assistant OS eignet sich die Mosquitto-Broker-App.
3. Einen Benutzer für die Bridge anlegen. Bei Mosquitto unter HA OS einen eigenen HA-Benutzer mit Benutzername und Passwort verwenden. Kein Web-Anmeldetoken eintragen.
4. In HA unter **Einstellungen → Geräte & Dienste** die **MQTT**-Integration mit diesem Broker verbinden. Die Geräteerkennung muss aktiv sein. Der Erkennungspfad lautet `homeassistant`.
5. Die Weboberfläche der Bridge öffnen. Unter **Optionen → Home Assistant über MQTT** die Broker-Adresse, den Port und die Zugangsdaten eintragen.
6. **MQTT einschalten** wählen und speichern. Als Adresse die im LAN erreichbare Broker-IP oder einen DNS-Namen verwenden. `localhost`, `core-mosquitto` und `http://` sind keine LAN-Adressen für den ESP.
7. Den Status **Verbunden** prüfen. Nach ungefähr zehn Sekunden erscheint unter MQTT das Gerät **ProBusX** mit 61 Entities.
8. Die Übernahme von Temperatur, Display und Tasten prüfen.

MQTT ist nach dem ersten Update ausgeschaltet. Ein leeres Passwortfeld behält ein gespeichertes Passwort.
Zum Löschen die eigene Checkbox wählen. Das Passwort wird nicht über die Status-API zurückgegeben.
Bei einem Schreibfehler bleibt die bisherige Einstellung aktiv. Der Status zeigt den Fehler.
Die gespeicherte Einstellung gilt nach Neustarts und Wiederverbindungen.

Für eine lokale TCP-Verbindung ist Port `1883` üblich. Bei TLS ist `8883` üblich.
TLS muss ausdrücklich zum Broker-Port passen. Die Bridge prüft die Zertifikatskette und den Hostnamen.
Dafür den Namen aus dem Broker-Zertifikat verwenden. Die Uhrzeit wird über `pool.ntp.org` bezogen.
Diese Version unterstützt öffentliche Zertifikatsketten aus dem ESP-IDF-Zertifikatsbestand.
Sie unterstützt keine eigene CA und keine Client-Zertifikate. Für einen lokalen Broker ohne TLS die TLS-Checkbox ausschalten.

Broker-Adresse und Benutzername dürfen höchstens 127 UTF-8-Bytes lang sein. Das gilt auch für das Passwort.
Die Broker-Adresse muss ein DNS-Name oder eine IPv4-Adresse sein. IPv6-Adressen werden hier nicht angenommen.

## Von der bisherigen HTTP-Integration wechseln

Die alte benutzerdefinierte Integration und MQTT besitzen unterschiedliche eindeutige Entity-Kennungen.
Ein automatischer Wechsel innerhalb desselben Integrationseintrags ist deshalb nicht möglich.

1. Die bisherigen Entity-IDs und die Dashboard-Konfiguration sichern.
2. Die bisherige **ProBusX**-Integration in HA entfernen. Den vorhandenen MQTT-Eintrag beibehalten.
3. MQTT auf der Bridge einschalten. Falls es bereits eingeschaltet war, kurz aus- und wieder einschalten.
4. Die neuen Entity-IDs unter dem MQTT-Gerät prüfen.
5. Wenn nötig, die neuen IDs auf die bisherigen IDs ändern. Das ist insbesondere bei Suffixen wie `_2` nötig.
6. Das bestehende Dashboard und die Bedienfeld-Karte weiterverwenden.

Die Erkennung schlägt die bisherigen Namen vor, zum Beispiel `sensor.probusx_s1`,
`camera.probusx_display` und `button.probusx_dreitastendruck`.
Sie überschreibt keine belegten Namen. Die Verbindung vorhandener Langzeitstatistiken mit dem neuen Integrationseintrag muss in HA geprüft werden.
Vor dem Wechsel daher die HA-Sicherung verwenden. Bereits gespeicherte Verlaufseinträge werden von der Firmware nicht verändert.

**Den alten ProBusX-Eintrag nicht parallel weiterlaufen lassen.** Er würde weiterhin HTTP-Abfragen an den ESP senden.
Die Dateien unter `home_assistant/custom_components/probusx` bleiben im Projekt als bisheriger HTTP-Quellstand erhalten.
Für MQTT werden sie nicht installiert. Die Datei `home_assistant/www/probusx/probusx-panel.js` bleibt nutzbar.

## Entities

| Gruppe | Anzahl | Inhalt |
| --- | ---: | --- |
| Temperaturen | 14 | S1–S10 und F1S1–F1S4 |
| Ausgangsstellwerte | 15 | R1–R7, R0, HE1–HE3, F1R1–F1R3, F1HE |
| Binäre Ausgangszustände | 15 | Je Ausgang eine Entity mit Suffix `_aktiv` |
| Kamera | 1 | `camera.probusx_display` |
| Tasten | 5 | Hoch, Rechts, Runter, Links, Dreitastendruck |
| Diagnose | 11 | WLAN-Signal, Laufzeit, freier Heap, MQTT-Verbindungen, MQTT-Fehler, abgelehnte MQTT-Tasten, CRC gültig, CRC fehlerhaft, UART-Fehler, Bus-Sendefehler, verfallene Tasten |

Ein Ausgang zeigt einen Stellwert, keine gemessene Drehzahl.
Ab Firmware 3.5.2 stammen die F1-Ausgänge aus dem Haupttelegramm. F1R1 und F1R3 tragen `mapping: observed_active`.
F1R2 und F1HE tragen bis zu einer aktiven Gegenprobe weiter `mapping: provisional_zero_only`.
FlexBox 2 und 3 erhalten keine erfundenen Werte. Ihre Buszuordnung muss noch bestätigt werden.

## Aktualisierung und Ausfälle

- Die gespeicherten Temperatur-, Ausgangs- und Displayintervalle gelten auch für MQTT.
- Temperaturen und Ausgänge werden mindestens im jeweiligen Intervall veröffentlicht. Bei Frischewechseln werden sie sofort erneut veröffentlicht.
- Das Display wird nur bei einem neuen vollständigen Busbild und nach Ablauf des Displayintervalls veröffentlicht. Bei Überlast wird das jüngste Bild verwendet. Es entsteht keine Warteschlange alter Bilder.
- Ein Displaybild enthält 160 × 128 Pixel und vier Graustufen. Das PNG enthält 5316 Byte. Bei zwei Bildern je Sekunde sind das ungefähr 10,6 kB/s Nutzdaten.
- Das Reglerbild entsteht weiterhin ungefähr alle 500 ms. MQTT erzeugt keine zusätzlichen Zwischenbilder.
- Nach drei Sekunden ohne aktuelle Haupt- oder F1-Daten werden die betroffenen Entities nicht verfügbar. Bei langer Displayeinstellung gilt für Bilder die größere Grenze aus drei Sekunden und Displayintervall plus zwei Sekunden.
- Rohwert `2500` erscheint bei Temperaturen als unbekannt. Es wird kein Ersatzwert von 0 °C erzeugt.
- Bei Broker-Verlust verbindet sich der Client erneut. Der Broker setzt über die hinterlegte Ausfallmeldung den Bridge-Status auf `offline`. Bei einem harten Verbindungsabbruch kann die Erkennung etwa 23 Sekunden dauern.
- HA-Neustart und Broker-Wiederverbindung lösen die Geräteerkennung erneut aus. Discovery und aktuelle Daten bleiben im Broker gespeichert.
- Analysewerte sind bei ausgeschalteter Kommunikationsanalyse nicht verfügbar. Die CRC-Prüfung bleibt aktiv.

Die native HA-MQTT-Kamera übernimmt das PNG. HA kann den Kamera-Proxy dabei mit `image/jpeg` kennzeichnen.
Die Nutzdaten bleiben PNG. Die vorhandene Bedienfeld-Karte zeigt sie im Browser an.
Das Kameraattribut `refresh_interval_ms` steuert weiterhin das Abrufintervall der Karte.
Diese Browserabrufe gehen an HA. Sie öffnen keine Bildverbindung zum ESP.

## Tasten

Während einer Rohaufnahme sind Web- und MQTT-Tasten absichtlich gesperrt.
Für einen Ein-/Aus-Vergleich während der Aufnahme direkt die Tasten am Regler verwenden.

Die Tasten verwenden einen gemeinsamen MQTT-Befehlspfad.
Jede Broker-Verbindung erhält eine neue zufällige Verbindungskennung.
Die aktuelle Kennung wird in die fünf erkannten Tasten eingebaut.
Die Firmware nimmt nur vollständige, nicht gespeicherte, nicht als Wiederholung markierte Befehle mit aktueller Kennung an.

Ein angenommener Tastendruck wird nur vorgemerkt. Die bisherige Buslogik wartet auf die passende Reglerabfrage.
Sie sendet höchstens eine Antwort und verwirft einen nicht abgeholten Auftrag nach zwei Sekunden.
Aufnahme, OTA, WLAN-Aktion, andere Taste und gesperrte Sendefreigabe verhindern das Vormerken.
Der Dreitastendruck verwendet weiterhin den bestätigten Buscode `0x70`.
Die MQTT-Kennung ist ein Schutz gegen alte Aufträge. Sie ersetzt nicht die Benutzerprüfung und Topic-Rechte des Brokers.

## Topic-Aufbau

`<id>` ist die WLAN-MAC ohne Doppelpunkte. Beispiel: `aabbccddeeff`.
Der Datenpfad lautet fest `probusx/<id>`. Er wird unter Optionen angezeigt.

| Topic unter `probusx/<id>/` | Format | Speicherung im Broker |
| --- | --- | --- |
| `availability` | `online` oder `offline` | Ja; Ausfallmeldung `offline` |
| `available/main`, `available/ext` | Frische der Messwertgruppe | Ja |
| `available/display`, `available/controls` | Bild- und Bedienverfügbarkeit | Ja |
| `available/analysis`, `available/bridge` | Diagnoseverfügbarkeit | Ja |
| `temperatures` | JSON mit skalierter Temperatur und `<kanal>_raw` | Ja |
| `outputs` | JSON mit Prozent, `<kanal>_aktiv` und `<kanal>_raw` | Ja |
| `display` | Binäres PNG | Ja |
| `display_meta` | JSON mit Intervall, Bildzähler und Größe | Ja |
| `diagnostics` | JSON; Übertragung alle fünf Sekunden | Ja |
| `key` | JSON `key` und `session`; QoS 0, niemals speichern | Nein |

Discovery liegt unter `homeassistant/<typ>/probusx_<id>/<kanal>/config`.
Die Bridge liest außerdem `homeassistant/status` für die HA-Startmeldung.
Die Broker-Rechte müssen Lesen der beiden Eingangspfade und Schreiben der eigenen Daten- und Discovery-Pfade erlauben.

Messwerte und Bilder werden mit QoS 0 direkt aus dem MQTT-Hintergrundtask gesendet.
Sie werden nicht in die QoS-Warteschlange gestellt. Discovery verwendet QoS 1.
Die Warteschlange ist auf 32 KiB begrenzt. Bei mehr als 8 KiB wird die nächste Discovery-Nachricht zurückgestellt.
Ein langsamer Broker kann den MQTT-Task bis zum Netzwerk-Timeout von drei Sekunden aufhalten.
Er hält dabei keine Sperre des Busparsers. Der UART-Empfang läuft auf dem anderen Kern weiter.

## Quellen

- [HA MQTT Discovery](https://www.home-assistant.io/integrations/mqtt/#mqtt-discovery)
- [HA MQTT-Kamera](https://www.home-assistant.io/integrations/camera.mqtt/)
- [HA MQTT-Tasten](https://www.home-assistant.io/integrations/button.mqtt/)
- [ESP-MQTT](https://docs.espressif.com/projects/esp-mqtt/en/latest/esp32/api-reference/mqtt.html)

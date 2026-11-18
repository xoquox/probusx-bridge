# Home Assistant einrichten

**MQTT ist ab Firmware 3.5.0 die Hauptschnittstelle zu Home Assistant.**
Die [MQTT-Anbindung](MQTT.md) meldet alle 61 Entities ohne eigene ProBusX-Komponente an.
`home_assistant/custom_components/probusx` enthält weiterhin die bisherige HTTP-Integration.
**Diese Komponente wird nur benötigt, wenn Home Assistant ohne MQTT auf die Bridge zugreift.**
Sie ist eine benutzerdefinierte Integration, keine Home-Assistant-App und kein Add-on.

## MQTT und HTTP im Vergleich

| Bereich | MQTT | HTTP-Komponente |
| --- | --- | --- |
| Verbindung | Die Bridge veröffentlicht Daten an einen Broker; HA liest sie dort. | HA fragt Status und Display direkt bei der Bridge ab. |
| Einrichtung | Broker und HA-MQTT-Integration einrichten; ProBusX-Entities erscheinen durch Discovery. | Komponentenordner kopieren, HA neu starten und die Bridge-Adresse eintragen. |
| Messwerte und Bedienung | 14 Temperaturen, 15 Stellwerte, 15 Ein/Aus-Zustände, Display und fünf Tasten. | Dieselben Messwertgruppen, Display und fünf Tasten. |
| Diagnose | Unter anderem WLAN-Signal, Laufzeit, Heap, MQTT-Verbindungen und abgelehnte MQTT-Tasten. | Unter anderem CRC-Fehlerquote, Bus-Empfangsrate, Telegrammpausen und HTTP-Abruffehler. |
| Displayübertragung | Die Bridge erzeugt PNG-Bilder und sendet sie an den Broker. | HA lädt Rohpixel über HTTP und wandelt sie in PNG um. |
| Vorteil | Automatische Erkennung; keine eigene HA-Komponente pflegen; keine HA-Status- oder Bildabrufe am ESP. | Kein Broker erforderlich; direkte Verbindung zwischen HA und Bridge. |
| Nachteil | Zusätzlicher Broker mit Zugangsdaten und Verfügbarkeit erforderlich. | Regelmäßige HTTP-Abfragen am ESP; manuelle Installation und Pflege der eigenen Komponente. |

Beide Wege nutzen dieselben Busdaten und Anzeigeintervalle der Bridge.
Die HTTP-Komponente kennzeichnet alle F1-Ausgänge weiterhin als vorläufig.
MQTT kennzeichnet F1R1 und F1R3 bereits als mit aktiven Werten geprüft.
Die [Bedienfeld-Karte](#bedienfeld-installieren) kann mit beiden Wegen verwendet werden.
Schema, RAM-Verlauf, Rohaufnahme, WLAN und OTA bleiben Funktionen der Bridge-Weboberfläche.
Sie funktionieren auch ohne MQTT-Broker und ohne Home Assistant.
HA-Verläufe benötigen bei beiden Wegen den HA-Recorder.

Die HTTP-Integration und MQTT haben unterschiedliche eindeutige Entity-Kennungen.
Für den Wechsel gelten die [Migrationsschritte](MQTT.md#von-der-bisherigen-http-integration-wechseln).
Beide Wege nicht parallel für dieselbe Bridge betreiben. Sonst entstehen doppelte Entities und zusätzliche HTTP-Abfragen.

## HTTP-Integration ohne MQTT installieren

1. Den Ordner `home_assistant/custom_components/probusx` nach `/config/custom_components/probusx` kopieren.
2. Prüfen, dass `/config/custom_components/probusx/manifest.json` vorhanden ist.
3. Home Assistant neu starten.
4. **Einstellungen → Geräte & Dienste → Integration hinzufügen** öffnen.
5. **ProBusX** auswählen.
6. IP-Adresse oder Hostname der Bridge eingeben. `http://` weglassen.
7. Port `80` verwenden, sofern er nicht geändert wurde.

Bei einem Update den ganzen Komponentenordner ersetzen.
Die bestehende Integration nicht löschen.
Die bisherigen Gerätekennungen und Entity-IDs bleiben erhalten.

Die letzte vollständige HA-Prüfumgebung der Ausgangsversion nutzte Home Assistant 2025.8.3.
Der GitHub-Workflow verwendet dieselbe festgelegte Prüfumgebung mit Python 3.13.
Das ist keine Zusicherung für jede spätere HA-Version.

## Entities

| Gruppe | Namen und Umfang |
| --- | --- |
| Haupttemperaturen | `sensor.probusx_s1` bis `sensor.probusx_s10` |
| F1-Temperaturen | `sensor.probusx_f1s1` bis `sensor.probusx_f1s4` |
| Hauptausgänge | R1 bis R7, R0, HE1 bis HE3 |
| F1-Ausgänge | F1R1 bis F1R3 und F1HE |
| Binäre Zustände | Ein Sensor mit Suffix `_aktiv` je Ausgang |
| Display | `camera.probusx_display` |
| Tasten | `button.probusx_hoch`, `_rechts`, `_runter`, `_links`, `_dreitastendruck` |
| Diagnose | 11 Sensoren für CRC, UART, Pausen und HTTP |

Entity-IDs können abweichen, wenn Home Assistant einen Namen bereits vergeben hat.
Die tatsächlichen IDs unter dem Gerät **ProBusX** prüfen.

Ein Ausgangswert bezeichnet den gemeldeten Stellwert.
Er bezeichnet keine gemessene Pumpendrehzahl.
Für die MQTT-Anbindung sind F1R1 und F1R3 ab Firmware 3.5.2 mit aktiven Werten geprüft.
F1R2 und F1HE bleiben vorläufig. Die alte HTTP-Komponente verwendet weiterhin ihr allgemeines vorläufiges Zuordnungsattribut.

Rohwert `2500` ergibt bei Temperaturkanälen den Zustand **unbekannt**.
Der Rohwert bleibt im Attribut `raw` erhalten.
Nach drei Sekunden ohne aktuelle Familiendaten wird die betroffene Entity **nicht verfügbar**.

## Bedienfeld installieren

1. Den Ordner `home_assistant/www/probusx` nach `/config/www/probusx` kopieren.
2. Falls nötig, den erweiterten Modus im HA-Benutzerprofil aktivieren.
3. **Einstellungen → Dashboards → Ressourcen** öffnen.
4. Diese Ressource als **JavaScript-Modul** eintragen:

```text
/local/probusx/probusx-panel.js?v=4
```

5. Den Browser vollständig neu laden.
6. Eine manuelle Dashboard-Karte mit folgendem Inhalt anlegen:

```yaml
type: custom:probusx-panel
title: Heizungsregler
camera: camera.probusx_display
up: button.probusx_hoch
right: button.probusx_rechts
down: button.probusx_runter
left: button.probusx_links
special: button.probusx_dreitastendruck
```

`www` muss direkt unter `/config` liegen.
Der Pfad `/config/custom_components/www` ist falsch.

Falls **Custom element doesn't exist** erscheint:

1. Den Dateipfad prüfen.
2. Die Ressourcen-URL prüfen.
3. Home Assistant neu starten.
4. Den Browser mit Strg+F5 neu laden.

Die Karte zeigt PNG-Einzelbilder.
Sie nutzt das Displayintervall der Bridge.
Andere HA-Kamerakarten können eigene Intervalle verwenden.

## Temperaturübersicht

`home_assistant/temperaturen.yaml` enthält eine Karte mit Temperaturliste und Verlauf.
Die Datei in eine manuelle Dashboard-Karte einfügen.
Für Verläufe muss der HA-Recorder die betreffenden Sensoren speichern.

## Update und Fehlerdiagnose

Bei einer geänderten Kartenfassung den Wert hinter `?v=` erhöhen.
Damit fordert der Browser die neue Datei an.

Fehler beim Bildabruf und beim Statusabruf werden getrennt gezählt.
Ein fehlerhafter Bildabruf macht aktuelle Temperatursensoren nicht automatisch ungültig.
Der HA-Diagnoseexport entfernt den Sitzungstoken.
Weitere Hinweise stehen unter [Diagnose](Diagnose.md).

# Home Assistant einrichten

**Ab Firmware 3.5.0 die [MQTT-Anbindung](MQTT.md) verwenden.**
Sie meldet alle 61 Entities ohne eigene HA-Komponente an.
Die folgende Installation beschreibt den bisherigen HTTP-Betrieb.
Für den Wechsel gelten die Schritte in der MQTT-Anleitung.

## Bisherige HTTP-Integration installieren

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

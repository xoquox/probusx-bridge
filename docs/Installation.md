# Installation

## Voraussetzungen

- Waveshare ESP32-S3-RS485-CAN / -U mit 16 MiB Flash und 8 MiB Octal-PSRAM.
- USB-Datenkabel.
- Windows 10 oder Linux, zum Beispiel Fedora.
- Ein 2,4-GHz-WLAN.
- Home Assistant mit Zugriff auf das lokale Netzwerk.

Die Pinbelegung steht in der [Projektbeschreibung](../README.md).
Die Firmware nutzt ausschließlich den RS485-Anschluss dieser Platine.
CAN wird nicht verwendet.

## Firmwaredateien beziehen

Ein geprüftes Firmwarepaket enthält diese Dateien:

| Datei | Zweck |
| --- | --- |
| `probusx-bridge-waveshare-usb.bin` | Erstinstallation über USB ab Adresse `0x0` |
| `probusx-bridge-waveshare-ota.bin` | Update über die Weboberfläche |
| `SHA256SUMS` | SHA-256-Prüfsummen der Abbilder |

Nach einem lokalen Build liegen die Dateien unter `firmware/abbilder/`.
Der GitHub-Workflow stellt dieselben Dateien als herunterladbares Build-Ergebnis bereit.
Die Bauanleitung steht in [Entwicklung](Entwicklung.md).

## Erstinstallation unter Windows 10

1. Python installieren. Dabei die Option für den Suchpfad aktivieren.
2. Die ZIP-Datei entpacken.
3. PowerShell öffnen.
4. Ein Flashprogramm installieren:

```powershell
py -m pip install esptool==4.9.0
```

5. Den Waveshare über USB anschließen.
6. Den COM-Port im Windows-Geräte-Manager ablesen.
7. Den Port im folgenden Befehl ersetzen:

```powershell
py -m esptool --chip esp32s3 --port COM5 write_flash 0x0 firmware/abbilder/probusx-bridge-waveshare-usb.bin
```

8. Nach dem Schreiben das Gerät neu starten.

Falls das Flashprogramm keine Verbindung aufbaut:

1. Die BOOT-Taste gedrückt halten.
2. Die RESET-Taste kurz drücken.
3. Die BOOT-Taste loslassen.
4. Den Flashbefehl erneut ausführen.

Das USB-Abbild setzt die gespeicherten WLAN-Daten und Optionen zurück.
Für spätere Updates das OTA-Abbild verwenden.

## Erstinstallation unter Fedora oder anderem Linux

1. Python und pip installieren, falls sie fehlen.
2. Eine virtuelle Umgebung anlegen:

```sh
python3 -m venv .venv-flash
.venv-flash/bin/python -m pip install esptool==4.9.0
```

3. Die Bridge über USB anschließen.
4. Den Gerätenamen ermitteln. Häufig ist er `/dev/ttyACM0`.
5. Mit einem Benutzer mit Zugriff auf das serielle Gerät schreiben:

```sh
.venv-flash/bin/python -m esptool --chip esp32s3 --port /dev/ttyACM0 write_flash 0x0 firmware/abbilder/probusx-bridge-waveshare-usb.bin
```

Die erforderliche Benutzergruppe hängt von der Linux-Installation ab.
Bei fehlender Berechtigung die Gruppen- oder udev-Regel des Systems anpassen.

## WLAN einrichten

1. Mit dem WLAN `ProBusX-Bridge` verbinden.
2. Das Passwort `probusx12345` eingeben.
3. Im Browser `http://192.168.4.1` öffnen.
4. Die Seite **Optionen** öffnen.
5. Das eigene WLAN suchen oder dessen Namen eingeben.
6. Das WLAN-Passwort eingeben.
7. **WLAN speichern & verbinden** wählen.
8. Die angezeigte IP-Adresse notieren.
9. Den PC wieder mit dem normalen WLAN verbinden.
10. Die notierte IP-Adresse im Browser öffnen.

Im Router eine feste DHCP-Zuordnung für die Bridge anlegen.
Home Assistant kann dann dieselbe IP-Adresse weiterverwenden.

Bei älterer Firmware heißt der Zugangspunkt noch `ProBusX-Sniffer`.
Das neue OTA-Abbild übernimmt die gespeicherten Zugangsdaten.
Ein bereits eingerichtetes Heim-WLAN muss daher nicht neu eingetragen werden.

## Bus anschließen und prüfen

1. Die Bridge und den Regler nach den jeweiligen Geräteanleitungen anschließen.
2. A+ und B− entsprechend der Anschlussbelegung verbinden.
3. Auf der Reglerseite die Anzeige und aktuelle Temperaturen prüfen.
4. Auf der Analyseseite die CRC- und UART-Zähler prüfen.
5. Bei paralleler Conexio-Bedienung das Senden der Bridge sperren.
6. Für den alleinigen Betrieb die Conexio vom Bus trennen.
7. Einen einzelnen Richtungstastendruck prüfen.

Ein fehlender separater Masseanschluss am isolierten Waveshare ist kein zusätzlicher GPIO-Anschluss.
Keine frei gewählten Masse- oder Versorgungsleitungen ergänzen.
Die Verdrahtung muss zur tatsächlich verwendeten Platine passen.

## Home Assistant installieren

Die Anleitung steht unter [Home Assistant](Home-Assistant.md).

# ESP32-Firmware

Diese Anwendung verwendet ESP-IDF und PlatformIO.
Sie empfängt ProBusX, stellt die Weboberfläche bereit und beantwortet Tastenvorgänge.

- [Firmware bauen](../docs/Entwicklung.md)
- [USB-Installation und WLAN](../docs/Installation.md)
- [OTA und Bedienung](../docs/Bedienung.md)
- [Telegrammdekodierung](../docs/Protokoll.md)
- [Quellcodeaufbau](../docs/Architektur.md)

`src/web_assets.h` wird aus den HTML-, JavaScript- und CSS-Dateien erzeugt.
`abbilder/` enthält nach einem Build die USB- und OTA-Dateien.
Die OTA-Projektkennung bleibt aus Kompatibilitätsgründen `pbx_waveshare_ota_v1`.

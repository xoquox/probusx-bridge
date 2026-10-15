#pragma once
// WLAN-Zugangsdaten über die Weboberfläche einstellen. NVS speichert die Daten.
#define AP_SSID "ProBusX-Bridge"
#define AP_PASSWORD "probusx12345"
// Verwendet den eingebauten RS485-Anschluss des Waveshare ESP32-S3-RS485-CAN / -U.
#define BUS_BAUD 1000000
#define BUS_RX_GPIO 18
#define BUS_TX_GPIO 17
#define BUS_ENABLE_GPIO 21
#define CAPTURE_TCP_PORT 4196

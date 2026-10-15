#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>
namespace ota_policy {
constexpr size_t prefix_size =
    288; // 24 Byte Bildkopf, 8 Byte Segmentkopf und 256 Byte Anwendungsbeschreibung.
constexpr size_t slot_size = 0x300000;
constexpr char project[] = "pbx_waveshare_ota_v1";
inline bool upload_size(size_t n, size_t capacity) {
  return n >= prefix_size + 32 && n <= capacity;
}
inline const char *prefix_error(const uint8_t *p, size_t n) {
  if (n < prefix_size)
    return "Firmware-Kopf unvollständig.";
  if (p[0] != 0xe9 || p[1] < 1 || p[1] > 16)
    return "Keine ESP-Firmware. ZIP zuerst entpacken.";
  if (p[12] != 9 || p[13] != 0)
    return "Firmware ist nicht für ESP32-S3.";
  if (p[23] != 1)
    return "Firmware ohne SHA-256-Prüfsumme abgelehnt.";
  if (p[32] != 0x32 || p[33] != 0x54 || p[34] != 0xcd || p[35] != 0xab)
    return "Keine Anwendungsdatei. Bitte die Datei mit -ota.bin wählen.";
  if (memcmp(p + 80, project, sizeof(project)) != 0)
    return "Falsche Firmware oder altes Speicherlayout. ProBusX Waveshare OTA v1 erforderlich.";
  if (!memchr(p + 48, 0, 32))
    return "Ungültige Firmware-Version.";
  return nullptr;
}
} // Namensraum ota_policy

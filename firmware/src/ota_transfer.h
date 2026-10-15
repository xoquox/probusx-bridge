#pragma once
#include "ota_policy.h"
#include <algorithm>
// Gerät und Fehlertests verwenden denselben Ablauf. Das Backend verwaltet den Flashzugriff.
namespace ota_policy {
template <class Backend> const char *transfer(Backend &io, size_t length, size_t capacity) {
  if (!upload_size(length, capacity))
    return "Firmware passt nicht in den Update-Speicher.";
  uint8_t buffer[4096];
  size_t received = 0;
  while (received < prefix_size) {
    const int n = io.read(buffer + received, prefix_size - received);
    if (n <= 0)
      return "Upload unterbrochen. Bisherige Firmware bleibt aktiv.";
    received += n;
  }
  if (const char *error = prefix_error(buffer, received))
    return error;
  if (!io.begin(length))
    return "Update-Speicher konnte nicht vorbereitet werden.";
  if (!io.write(buffer, received)) {
    io.abort();
    return "Schreiben der Firmware fehlgeschlagen.";
  }
  io.progress(received);
  while (received < length) {
    const int n = io.read(buffer, std::min(sizeof(buffer), length - received));
    if (n <= 0) {
      io.abort();
      return "Upload unterbrochen. Bisherige Firmware bleibt aktiv.";
    }
    if (!io.write(buffer, n)) {
      io.abort();
      return "Schreiben der Firmware fehlgeschlagen.";
    }
    received += n;
    io.progress(received);
  }
  // end prüft die Daten und gibt den Flashzugriff auch bei einem Fehler frei.
  if (!io.end())
    return "Firmware-Prüfung fehlgeschlagen. Bisherige Firmware bleibt aktiv.";
  if (!io.select())
    return "Firmware konnte nicht als Startversion ausgewählt werden.";
  return nullptr;
}
} // Namensraum ota_policy

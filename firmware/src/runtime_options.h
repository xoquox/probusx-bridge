#pragma once
#include <cstdint>
#include <string>
namespace pbx {
// Der versionierte NVS-Datensatz enthält Intervalle und Freigaben. Größe und Version beim Laden
// prüfen.
struct RuntimeOptions {
  uint32_t version = 1, display_ms = 250, temperature_ms = 1000, output_ms = 1000;
  uint8_t analysis = 1, transmit = 1, reserved[2]{};
  bool valid() const {
    return version == 1 && display_ms >= 100 && display_ms <= 5000 && temperature_ms >= 250 &&
           temperature_ms <= 60000 && output_ms >= 250 && output_ms <= 60000 && analysis <= 1 &&
           transmit <= 1;
  }
};
inline bool option_number(const std::string &s, uint32_t &out) {
  if (s.empty() || s.size() > 5)
    return false;
  uint32_t n = 0;
  for (char c : s) {
    if (c < '0' || c > '9')
      return false;
    n = n * 10 + c - '0';
  }
  out = n;
  return true;
}
} // Namensraum pbx

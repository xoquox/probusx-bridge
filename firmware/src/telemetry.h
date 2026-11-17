#pragma once
#include "protocol.h"
namespace pbx {
// Nur nach Annahme durch Parser aufrufen. Hauptdaten: 241 Byte mit bestätigter CRC16.
// F1-Ausgänge stehen bei 114..121. Die scheinbaren Ausgangswörter im F0-Frame sind keine
// belegte Istwertquelle und dürfen diese Werte nicht überschreiben.
inline void main_values(const uint8_t *frame, int16_t *temperatures, int16_t *outputs) {
  for (int i = 0; i < 10; ++i)
    temperatures[i] = word(frame + 10 + 2 * i);
  for (int i = 0; i < 11; ++i)
    outputs[i] = word(frame + 44 + 2 * i);
  for (int i = 0; i < 4; ++i)
    outputs[11 + i] = word(frame + 114 + 2 * i);
}
inline void flex1_temperatures(const uint8_t *frame, int16_t *temperatures) {
  for (int i = 0; i < 4; ++i)
    temperatures[10 + i] = word(frame + 5 + 2 * i);
}
// Die gemeinsame F1-Verfügbarkeit verlangt beide Datenquellen. Ein weiterlaufender F0-Strom
// darf veraltete Ausgangswerte aus den Hauptdaten nicht als aktuell ausgeben.
inline uint64_t flex1_at(uint64_t temperatures_at, uint64_t outputs_at) {
  return temperatures_at && outputs_at ? std::min(temperatures_at, outputs_at) : 0;
}
} // Namensraum pbx

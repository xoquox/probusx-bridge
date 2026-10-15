#pragma once
#include <cstddef>
#include <cstdint>

namespace wire {
constexpr size_t HEADER_SIZE = 36;
constexpr uint8_t VERSION = 2;
enum Type : uint8_t { HELLO = 1, DATA = 2, STATUS = 3, END = 4 };
inline void le(uint8_t *p, uint64_t x, size_t n) {
  for (size_t i = 0; i < n; ++i)
    p[i] = static_cast<uint8_t>(x >> (8 * i));
}
// IEEE-CRC32 über Kopfbytes 0 bis 31 und Nutzdaten berechnen. Das CRC-Feld selbst auslassen.
inline uint32_t crc_update(uint32_t crc, const uint8_t *p, size_t n) {
  for (size_t i = 0; i < n; ++i) {
    crc ^= p[i];
    for (unsigned j = 0; j < 8; ++j)
      crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
  }
  return crc;
}
inline void header(uint8_t *h, uint8_t type, uint32_t sequence, uint64_t time_us, uint64_t offset,
                   const uint8_t *payload, uint32_t len) {
  h[0] = 'P';
  h[1] = 'B';
  h[2] = 'X';
  h[3] = 'S';
  h[4] = VERSION;
  h[5] = type;
  le(h + 6, 0, 2);
  le(h + 8, sequence, 4);
  le(h + 12, time_us, 8);
  le(h + 20, offset, 8);
  le(h + 28, len, 4);
  le(h + 32, ~crc_update(crc_update(0xFFFFFFFFu, h, 32), payload, len), 4);
}
} // Namensraum wire

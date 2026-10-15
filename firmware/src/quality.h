#pragma once
#include <cstdint>
#include <cstddef>
#include <algorithm>
namespace pbx {
enum Family { MAIN, EXT, DISPLAY, SHORT, POLL, KEY, AUX, FAMILIES };
inline Family family(const uint8_t *f) {
  return f[1] == 0xb3                   ? KEY
         : f[4] == 0x61                 ? MAIN
         : f[4] == 0xf0                 ? EXT
         : f[4] == 0x55                 ? DISPLAY
         : f[4] == 0x34                 ? POLL
         : f[4] >= 0x70 && f[4] <= 0x73 ? SHORT
                                        : AUX;
}
inline const char *family_name(int i) {
  const char *names[] = {"main", "ext", "display", "short", "poll", "key", "aux"};
  return names[i];
}
struct FamilyStats {
  uint64_t valid = 0, crc_bad = 0, crc_ok = 0, unchecked = 0, last_us = 0, max_gap_us = 0, gaps = 0;
  bool gap_open = false;
};
struct Event {
  uint64_t us = 0;
  uint32_t value = 0;
  uint8_t code = 0, family = 0;
};
// Ereignisspeicher begrenzen. Im Empfangstask keine Netzwerk- oder Dateizugriffe ausführen.
enum EventCode {
  CRC_ERROR = 1,
  FRAME_TIMEOUT,
  HEADER_ERROR,
  UART_FIFO,
  UART_BUFFER,
  UART_FRAME,
  UART_PARITY,
  UART_BREAK_EVENT,
  QUEUE_PRESSURE,
  GAP_START,
  GAP_END,
  CAPTURE_RESET,
  KEY_EXPIRED,
  TX_ERROR,
  FOREIGN_KEY,
  TRAILER_ERROR
};
// Zeitangaben sind Mikrosekunden seit Start. Zähler liegen im RAM und werden bei Analyseneustart
// zurückgesetzt.
struct Quality {
  bool enabled = true;
  uint64_t started_us = 0;
  FamilyStats families[FAMILIES]{};
  struct BadFrame {
    uint64_t us = 0;
    uint16_t expected = 0, received = 0, length = 0;
    uint8_t head[12]{};
  } last_bad{};
  uint64_t trailer_errors = 0;
  uint64_t bytes = 0, crc_ok = 0, crc_bad = 0, unchecked = 0, headers_bad = 0, partial_timeouts = 0,
           skipped_bytes = 0, padding_bytes = 0;
  uint64_t uart[6]{}, tx_errors = 0, key_expired = 0, foreign_keys = 0, skipped_poll_busy = 0;
  uint64_t rx_last_us = 0, rx_max_gap_us = 0;
  uint32_t buffered_high = 0;
  // 60 Sekundenplätze bilden ein gleitendes Zeitfenster. Ein neuer Sekundenwert ersetzt den alten
  // Platz.
  struct Bucket {
    uint64_t second = UINT64_MAX, bytes = 0, ok = 0, bad = 0, unchecked = 0;
  } buckets[60]{};
  // Nur die letzten 64 Ereignisse behalten. events_total läuft darüber hinaus weiter.
  Event events[64]{};
  uint32_t events_total = 0;
  void event(uint64_t t, EventCode code, int f = 0, uint32_t v = 0) {
    if (!enabled)
      return;
    events[events_total++ % 64] = {t, v, uint8_t(code), uint8_t(f)};
  }
  Bucket &bucket(uint64_t t) {
    auto &b = buckets[(t / 1000000) % 60];
    if (b.second != t / 1000000)
      b = {t / 1000000, 0, 0, 0, 0};
    return b;
  }
  void received(uint64_t t, size_t n) {
    if (!enabled)
      return;
    if (!started_us)
      started_us = t;
    bytes += n;
    bucket(t).bytes += n;
    if (rx_last_us)
      rx_max_gap_us = std::max(rx_max_gap_us, t - rx_last_us);
    rx_last_us = t;
  }
  // Nur gültige Frames aktualisieren die Frische. Nicht geprüfte Frames getrennt von CRC-Erfolg
  // zählen.
  void frame(uint64_t t, Family f, bool checked, bool valid) {
    if (!enabled)
      return;
    auto &s = families[f];
    auto &b = bucket(t);
    if (!valid) {
      ++s.crc_bad;
      ++crc_bad;
      ++b.bad;
      event(t, CRC_ERROR, f);
      return;
    }
    ++s.valid;
    if (checked) {
      ++s.crc_ok;
      ++crc_ok;
      ++b.ok;
    } else {
      ++s.unchecked;
      ++unchecked;
      ++b.unchecked;
    }
    if (s.last_us) {
      uint64_t gap = t - s.last_us;
      s.max_gap_us = std::max(s.max_gap_us, gap);
      if (s.gap_open) {
        s.gap_open = false;
        event(t, GAP_END, f, uint32_t(gap / 1000));
      }
    }
    s.last_us = t;
  }
  // Eine Pause erst nach dem ersten gültigen Frame öffnen. Einen fortdauernden Ausfall nur einmal
  // zählen.
  void tick(uint64_t t) {
    if (!enabled)
      return;
    for (int i : {int(MAIN), int(EXT), int(DISPLAY), int(POLL)}) {
      auto &s = families[i];
      uint64_t limit = i == MAIN ? 2500000 : 1500000;
      if (s.last_us && !s.gap_open && t - s.last_us > limit) {
        s.gap_open = true;
        ++s.gaps;
        event(t, GAP_START, i, uint32_t((t - s.last_us) / 1000));
      }
    }
  }
  // Nur Sekundenplätze aus dem aktuellen 60-Sekunden-Fenster aufsummieren.
  void window(uint64_t t, uint64_t &bytes_out, uint64_t &ok, uint64_t &bad,
              uint64_t &unknown) const {
    bytes_out = ok = bad = unknown = 0;
    uint64_t sec = t / 1000000;
    for (const auto &b : buckets)
      if (b.second <= sec && sec - b.second < 60) {
        bytes_out += b.bytes;
        ok += b.ok;
        bad += b.bad;
        unknown += b.unchecked;
      }
  }
};
} // Namensraum pbx

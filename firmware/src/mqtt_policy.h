#pragma once
#include <cstdint>
#include <cstring>
#include <string>
namespace pbx_mqtt {
// Eigenes NVS-Format. WLAN, Busparameter und Schema bleiben davon unabhängig.
struct Settings {
  uint32_t version = 1;
  uint16_t port = 1883;
  uint8_t enabled = 0, tls = 0;
  char host[128]{}, username[128]{}, password[128]{};
  bool valid() const {
    if (version != 1 || !port || enabled > 1 || tls > 1 || !memchr(host, 0, sizeof(host)) ||
        !memchr(username, 0, sizeof(username)) || !memchr(password, 0, sizeof(password)) ||
        (enabled && !host[0]))
      return false;
    for (const char *p = host; *p; ++p)
      if (!((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') || (*p >= '0' && *p <= '9') ||
            *p == '.' || *p == '-'))
        return false;
    return true;
  }
};
inline bool fresh(uint64_t now, uint64_t at, uint32_t ms = 3000) {
  return at && now >= at && now - at < uint64_t(ms) * 1000;
}
// Keine gespeicherten Befehle, Wiederholungen oder Teilnachrichten ausführen.
inline bool command_allowed(bool connected, bool retained, bool duplicate, int offset, int length,
                            int total, const char *session, const char *expected) {
  return connected && !retained && !duplicate && offset == 0 && length > 0 && length == total &&
         length <= 128 && session && expected && expected[0] && !strcmp(session, expected);
}
inline double output(int raw, unsigned index) {
  return raw / (index >= 8 && index <= 10 ? 100.0 : 2.0);
}
inline constexpr const char *temperatures[] = {"s1", "s2", "s3",  "s4",   "s5",   "s6",   "s7",
                                               "s8", "s9", "s10", "f1s1", "f1s2", "f1s3", "f1s4"};
inline constexpr const char *outputs[] = {"r1",  "r2",  "r3",  "r4",   "r5",   "r6",   "r7",  "r0",
                                          "he1", "he2", "he3", "f1r1", "f1r2", "f1r3", "f1he"};
inline constexpr const char *keys[] = {"up", "right", "down", "left", "special"};
inline constexpr const char *buttons[] = {"hoch", "rechts", "runter", "links", "dreitastendruck"};
// PNG mit 2-Bit-Graustufen, ohne verlustbehaftete Umwandlung und ohne zusätzliche Bibliothek.
inline uint32_t crc32(const uint8_t *p, size_t n) {
  uint32_t c = ~0u;
  while (n--) {
    c ^= *p++;
    for (int i = 0; i < 8; ++i)
      c = (c >> 1) ^ (0xedb88320u & (0u - (c & 1)));
  }
  return ~c;
}
inline void be32(std::string &out, uint32_t n) {
  for (int i = 3; i >= 0; --i)
    out.push_back(char(n >> (8 * i)));
}
inline void chunk(std::string &out, const char *type, const std::string &data) {
  be32(out, data.size());
  size_t begin = out.size();
  out.append(type, 4);
  out += data;
  be32(out, crc32(reinterpret_cast<const uint8_t *>(out.data() + begin), 4 + data.size()));
}
inline std::string png(const uint8_t *raw) {
  std::string rows;
  rows.reserve(5248);
  for (int y = 0; y < 128; ++y) {
    rows.push_back(0);
    for (int x = 0; x < 160; x += 4) {
      uint8_t packed = 0;
      for (int k = 0; k < 4; ++k) {
        auto sample = 3 - ((raw[(159 - x - k) * 32 + (y >> 2)] >> (2 * (y % 4))) & 3);
        packed |= sample << (6 - 2 * k);
      }
      rows.push_back(char(packed));
    }
  }
  std::string z("\x78\x01\x01", 3);
  unsigned n = rows.size();
  z.push_back(char(n));
  z.push_back(char(n >> 8));
  z.push_back(char(~n));
  z.push_back(char((~n) >> 8));
  z += rows;
  uint32_t a = 1, b = 0;
  for (uint8_t v : rows) {
    a = (a + v) % 65521;
    b = (b + a) % 65521;
  }
  be32(z, (b << 16) | a);
  std::string out("\x89PNG\r\n\x1a\n", 8), ihdr;
  be32(ihdr, 160);
  be32(ihdr, 128);
  ihdr.append("\x02\x00\x00\x00\x00", 5);
  out.reserve(5316);
  chunk(out, "IHDR", ihdr);
  chunk(out, "IDAT", z);
  chunk(out, "IEND", {});
  return out;
}
} // Namensraum pbx_mqtt

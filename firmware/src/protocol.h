#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>
#include "quality.h"
namespace pbx {
// Reflektierte CRC16 mit Startwert FFFF. Das Ergebnis wird im Telegramm High vor Low gespeichert.
inline uint16_t crc(const uint8_t *p, size_t n) {
  uint16_t c = 0xffff;
  while (n--) {
    c ^= *p++;
    for (int i = 0; i < 8; i++)
      c = (c >> 1) ^ ((c & 1) ? 0x8408 : 0);
  }
  return c;
}
inline bool valid(const uint8_t *p, size_t n) {
  return n >= 2 && crc(p, n - 2) == uint16_t(p[n - 2] * 256 + p[n - 1]);
}
// Zwei Bytes als vorzeichenbehaftetes 16-Bit-Wort in Big Endian lesen.
inline int16_t word(const uint8_t *p) {
  return int16_t(uint16_t(p[0]) * 256 + p[1]);
}
// Feste Längen verhindern, dass Bilddaten als neuer Telegrammkopf erkannt werden.
// Hauptdaten-CRC ohne AA-Markierung bei Offset 238 berechnen. An 360 Telegrammen geprüft.
class Parser {
  uint8_t buf[5130]{};
  size_t used = 0, expected = 0;

public:
  uint32_t rejected = 0;
  Quality quality{};
  uint64_t partial_at = 0;
  // Ein unvollständiges Telegramm nach mehr als 100 ms ohne Parserdaten verwerfen.
  void tick(uint64_t now) {
    if (used && partial_at && now - partial_at > 100000) {
      if (quality.enabled)
        ++quality.partial_timeouts;
      quality.event(now, FRAME_TIMEOUT, 0, uint32_t(used));
      reset();
    }
    quality.tick(now);
  }
  void reset() {
    used = expected = 0;
    partial_at = 0;
  }
  // Beliebige Empfangsblöcke annehmen. callback darf den internen Puffer nicht über den Aufruf
  // hinaus verwenden. Das letzte bool-Argument meldet, ob der Frame am Ende dieses Eingabeblocks
  // endet.
  template <class F> void feed(const uint8_t *p, size_t n, F callback, uint64_t now = 0) {
    tick(now);
    quality.received(now, n);
    while (n--) {
      uint8_t b = *p++;
      if (!used && b != 0xa5) {
        if (quality.enabled) {
          if (b == 0)
            ++quality.padding_bytes;
          else
            ++quality.skipped_bytes;
        }
        continue;
      }
      partial_at = now;
      buf[used++] = b;
      bool bad = false;
      if (used == 2 && b != 0x13 && b != 0xb3)
        bad = true;
      if (used == 2 && b == 0xb3)
        expected = 5;
      if (used == 4 && buf[1] == 0x13 &&
          (buf[3] != 1 || (buf[2] != 0x36 && buf[2] != 0x40 && buf[2] != 0x35)))
        bad = true;
      if (used == 5 && buf[1] == 0x13) {
        switch (buf[4]) {
        case 0x55:
          expected = 5125;
          break;
        case 0x61:
          expected = 241;
          break;
        case 0xf0:
          expected = 26;
          break;
        case 0x70:
        case 0x71:
        case 0x72:
        case 0x73:
          expected = 13;
          break;
        case 0x34:
        case 0x80:
        case 0x81:
        case 0x82:
        case 0x83:
          expected = 7;
          break;
        default:
          bad = true;
        }
      }
      // Bei einem falschen Kopf neu suchen. Ein abschließendes A5 kann bereits der nächste Start
      // sein.
      if (bad) {
        ++rejected;
        if (quality.enabled)
          ++quality.headers_bad;
        if (quality.headers_bad == 1 || quality.headers_bad % 100 == 0)
          quality.event(now, HEADER_ERROR, 0, uint32_t(quality.headers_bad));
        reset();
        if (b == 0xa5)
          buf[used++] = b;
        continue;
      }
      // Die Länge zuerst erfüllen. Kopfmuster innerhalb der Nutzdaten haben keine Steuerwirkung.
      if (expected && used == expected) {
        bool main = buf[1] == 0x13 && buf[4] == 0x61;
        // Die Analyseoption schaltet nur Zähler ab. Sie schaltet die CRC-Prüfung nicht ab.
        bool checked =
            main || buf[1] == 0xb3 || buf[4] == 0xf0 || (buf[4] >= 0x70 && buf[4] <= 0x73);
        if (main && buf[238] != 0xaa) {
          if (quality.enabled)
            ++quality.trailer_errors;
          quality.event(now, TRAILER_ERROR, MAIN, buf[238]);
          ++rejected;
          reset();
          continue;
        }
        uint16_t expected_crc = checked ? crc(buf, main ? 238 : used - 2) : 0;
        uint16_t received_crc = checked ? uint16_t(buf[used - 2] * 256 + buf[used - 1]) : 0;
        bool good = !checked || expected_crc == received_crc;
        if (!good && quality.enabled) {
          quality.last_bad.us = now;
          quality.last_bad.expected = expected_crc;
          quality.last_bad.received = received_crc;
          quality.last_bad.length = used;
          memcpy(quality.last_bad.head, buf, std::min(size_t(12), used));
        }
        quality.frame(now, family(buf), checked, good);
        if (good)
          callback(buf, used, n == 0);
        else
          ++rejected;
        reset();
      }
    }
  }
};
// Genau einen Tastenvorgang vormerken. Nur auf die passende Reglerabfrage antworten.
class Keys {
  enum Stage { IDLE, PRESS, MARK, HOLD, RELEASE_MARK } stage = IDLE;
  uint8_t key = 0;
  uint64_t deadline = 0, release_at = 0;

public:
  bool busy() const {
    return stage != IDLE;
  }
  bool queue(uint8_t k, uint64_t now) {
    if (busy())
      return false;
    key = k;
    stage = PRESS;
    deadline = now + 2000000;
    return true;
  }
  // Nur eine noch nicht gesendete Taste darf verfallen. Eine begonnene Loslassfolge muss
  // weiterlaufen.
  bool expire(uint64_t now) {
    if (stage == PRESS && now > deadline) {
      stage = IDLE;
      return true;
    }
    return false;
  }
  // Pro Reglerabfrage höchstens einen Code liefern. FF wird als beobachtete Folge nachgebildet.
  // Die Sondertaste 70 benötigt keine 500-ms-Haltephase. FF ist keine belegte Bestätigung.
  bool poll(uint64_t now, uint8_t &out) {
    expire(now);
    switch (stage) {
    case PRESS:
      out = key;
      release_at = now + (key == 0x70 ? 0 : 500000);
      stage = key == 0x70 ? HOLD : MARK;
      return true;
    case MARK:
      out = 0xff;
      stage = HOLD;
      return true;
    case HOLD:
      if (now < release_at)
        return false;
      out = 0;
      stage = RELEASE_MARK;
      return true;
    case RELEASE_MARK:
      out = 0xff;
      stage = IDLE;
      return true;
    default:
      return false;
    }
  }
};
} // Namensraum pbx

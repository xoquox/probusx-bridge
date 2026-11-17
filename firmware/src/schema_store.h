#pragma once
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <algorithm>

namespace schema_store {
// Beide Datentypen besitzen zwei Bänke. Erst der zuletzt geschriebene Kopf
// bestätigt den neuen Stand. Ein abgebrochener Schreibvorgang lässt die alte Bank gültig.
constexpr size_t sector = 4096, image_bank = 0x100000, layout_bank = 0x8000;
constexpr size_t image_limit = image_bank - sector, layout_limit = 16384;
constexpr size_t region_address = 0x620000, region_size = 2 * image_bank + 2 * layout_bank;
struct Header {
  uint32_t magic, version, generation, size, crc, format;
  uint16_t width, height;
  uint32_t header_crc;
};
static_assert(sizeof(Header) == 32);
struct Record {
  Header header{};
  int bank = -1;
};
inline uint32_t crc_update(uint32_t crc, const uint8_t *bytes, size_t size) {
  for (size_t i = 0; i < size; ++i) {
    crc ^= bytes[i];
    for (unsigned bit = 0; bit < 8; ++bit)
      crc = (crc >> 1) ^ (0xedb88320U & (0U - (crc & 1U)));
  }
  return crc;
}
inline uint32_t crc(const void *bytes, size_t size) {
  return ~crc_update(~0U, static_cast<const uint8_t *>(bytes), size);
}
inline bool newer(uint32_t a, uint32_t b) { return a != b && uint32_t(a - b) < 0x80000000U; }
inline bool valid_header(const Header &h, size_t capacity) {
  return h.magic == 0x31535850 && h.version == 1 && h.generation && h.size &&
         h.size <= capacity - sector && h.header_crc == crc(&h, offsetof(Header, header_crc));
}
template <class Flash> bool read_record(Flash &flash, size_t offset, size_t capacity, Header &h) {
  if (!flash.read(offset, &h, sizeof(h)) || !valid_header(h, capacity)) return false;
  uint8_t buffer[1024];
  uint32_t sum = ~0U;
  for (size_t at = 0; at < h.size;) {
    const size_t n = std::min(sizeof(buffer), size_t(h.size) - at);
    if (!flash.read(offset + sector + at, buffer, n)) return false;
    sum = crc_update(sum, buffer, n);
    at += n;
  }
  return ~sum == h.crc;
}
template <class Flash> Record load(Flash &flash, size_t offset, size_t capacity) {
  Record record;
  for (int bank = 0; bank < 2; ++bank) {
    Header h{};
    if (read_record(flash, offset + bank * capacity, capacity, h) &&
        (record.bank < 0 || newer(h.generation, record.header.generation))) {
      record.header = h;
      record.bank = bank;
    }
  }
  return record;
}
template <class Flash>
bool save(Flash &flash, size_t offset, size_t capacity, const uint8_t *data, size_t size,
          uint32_t format, uint16_t width, uint16_t height, Record &current) {
  if (!size || size > capacity - sector) return false;
  const int bank = current.bank == 0 ? 1 : 0;
  const size_t begin = offset + bank * capacity;
  uint32_t generation = current.header.generation + 1;
  if (!generation) generation = 1;
  Header h{0x31535850, 1, generation, uint32_t(size), crc(data, size), format, width, height, 0};
  h.header_crc = crc(&h, offsetof(Header, header_crc));
  const size_t used = sector + (size + sector - 1) / sector * sector;
  for (size_t at = 0; at < used; at += sector)
    if (!flash.erase(begin + at, sector)) return false;
  for (size_t at = 0; at < size;) {
    const size_t n = std::min(size_t(1024), size - at);
    if (!flash.write(begin + sector + at, data + at, n)) return false;
    at += n;
  }
  if (!flash.write(begin, &h, sizeof(h))) return false;
  Header check{};
  if (!read_record(flash, begin, capacity, check)) return false;
  current = Record{check, bank};
  return true;
}
inline bool channel(const char *id) {
  if (!id) return false;
  for (unsigned i = 1; i <= 10; ++i) {
    char name[8];
    std::snprintf(name, sizeof(name), "S%u", i);
    if (!std::strcmp(name, id)) return true;
  }
  for (auto name : {"R0", "R1", "R2", "R3", "R4", "R5", "R6", "R7", "HE1", "HE2", "HE3"})
    if (!std::strcmp(name, id)) return true;
  return std::strlen(id) == 4 && id[0] == 'F' && id[1] >= '1' && id[1] <= '3' &&
         ((id[2] == 'S' && id[3] >= '1' && id[3] <= '4') ||
          (id[2] == 'R' && id[3] >= '1' && id[3] <= '3') || (id[2] == 'H' && id[3] == 'E'));
}
} // Namensraum schema_store

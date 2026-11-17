#include "schema_store.h"
#include <cassert>
#include <vector>
#include <string>
#include <iostream>
using namespace schema_store;
struct Flash {
  std::vector<uint8_t> data;
  int fail_after = -1, operations = 0;
  Flash(size_t n) : data(n, 0xff) {}
  bool allow() { return fail_after < 0 || operations++ < fail_after; }
  bool read(size_t at, void *out, size_t n) {
    if (at + n > data.size()) return false;
    memcpy(out, data.data() + at, n); return true;
  }
  bool erase(size_t at, size_t n) {
    if (!allow() || at % sector || n % sector || at + n > data.size()) return false;
    std::fill(data.begin() + at, data.begin() + at + n, 0xff); return true;
  }
  bool write(size_t at, const void *in, size_t n) {
    if (!allow() || at + n > data.size()) return false;
    auto *bytes = static_cast<const uint8_t *>(in);
    for (size_t i = 0; i < n; ++i) {
      assert((data[at+i] & bytes[i]) == bytes[i]); data[at+i] &= bytes[i];
    }
    return true;
  }
};
int main() {
  assert(crc("123456789", 9) == 0xcbf43926);
  assert(region_address >= 0x612000 && region_address + region_size <= 0x1000000);
  for (auto id : {"S1", "S10", "R0", "HE3", "F1S1", "F2R3", "F3HE"}) assert(channel(id));
  for (auto id : {"", "S0", "S11", "R8", "F4S1", "F2S5", "F3R0", "../S1"}) assert(!channel(id));
  Flash flash(2 * layout_bank);
  auto current = load(flash, 0, layout_bank); assert(current.bank == -1);
  std::vector<uint8_t> old(5000, 17), next(7200, 21);
  assert(save(flash, 0, layout_bank, old.data(), old.size(), 0, 0, 0, current));
  auto boot = load(flash, 0, layout_bank); assert(boot.bank == current.bank && boot.header.generation == 1);
  const auto stable = flash;
  // Jeden Lösch- und Schreibschritt des nächsten Speicherns gezielt unterbrechen.
  for (int failure = 0; failure < 12; ++failure) {
    flash = stable; current = boot; flash.fail_after = failure; flash.operations = 0;
    const bool saved = save(flash, 0, layout_bank, next.data(), next.size(), 0, 0, 0, current);
    auto recovered = load(flash, 0, layout_bank);
    assert(recovered.bank >= 0);
    assert(recovered.header.crc == (saved ? crc(next.data(), next.size()) : crc(old.data(), old.size())));
  }
  flash = stable; current = boot;
  assert(save(flash, 0, layout_bank, next.data(), next.size(), 0, 0, 0, current));
  assert(current.header.generation == 2);
  const auto confirmed = flash;
  // Jeden möglichen Abbruch innerhalb des abschließenden Kopfes nachstellen.
  for (size_t prefix = 0; prefix < sizeof(Header); ++prefix) {
    auto torn = confirmed;
    const size_t at = current.bank * layout_bank;
    std::fill(torn.data.begin() + at + prefix, torn.data.begin() + at + sizeof(Header), 0xff);
    assert(load(torn, 0, layout_bank).header.generation == 1);
  }

  flash.data[current.bank * layout_bank + sector + 123] ^= 1;
  auto recovered = load(flash, 0, layout_bank); assert(recovered.header.generation == 1);
  assert(!save(flash, 0, layout_bank, old.data(), 0, 0, 0, 0, current));
  assert(!save(flash, 0, layout_bank, old.data(), layout_bank, 0, 0, 0, current));
  assert(newer(1, 0xffffffffU) && !newer(0xffffffffU, 1));
  // Bilddaten und Anordnung liegen in verschiedenen Bänken.
  Flash all(region_size); Record image, plan;
  assert(save(all, 0, image_bank, old.data(), old.size(), 1, 320, 200, image));
  assert(save(all, 2*image_bank, layout_bank, next.data(), next.size(), 0, 0, 0, plan));
  assert(load(all, 0, image_bank).header.crc == crc(old.data(), old.size()));
  assert(load(all, 2*image_bank, layout_bank).header.crc == crc(next.data(), next.size()));
  std::cout << "Bestanden: Schema bleibt nach Neustart und unterbrochenen Schreibvorgängen gültig. CRC, Bankwechsel, Grenzen und getrennte Bild-/Layoutdaten geprüft.\n";
}

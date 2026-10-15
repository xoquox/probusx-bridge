// Prüft feste Referenzantworten, beliebige Blockgrenzen und die Tastenzustände.
#include "protocol.h"
#include <cassert>
#include <fstream>
#include <iostream>
#include <iterator>
#include <vector>

int main(int argc, char **argv) {
  assert(argc == 2);
  std::ifstream input(argv[1], std::ios::binary);
  assert(input);
  const std::vector<uint8_t> main{std::istreambuf_iterator<char>(input), {}};
  assert(main.size() == 241);

  // Diese CRC-Werte stammen aus der Tastenaufnahme. Nicht im Test neu ableiten.
  const uint8_t special[] = {0xa5, 0xb3, 0x70, 0x6c, 0xd8};
  const uint8_t release[] = {0xa5, 0xb3, 0x00, 0x1f, 0x5f};
  assert(pbx::valid(special, sizeof(special)));
  assert(pbx::valid(release, sizeof(release)));
  const uint8_t negative[] = {0xff, 0x85};
  assert(pbx::word(negative) == -123);

  for (size_t stride : {size_t(1), size_t(7), size_t(1024)}) {
    pbx::Parser parser;
    unsigned accepted = 0;
    for (size_t i = 0; i < main.size(); i += stride) {
      parser.feed(
          main.data() + i, std::min(stride, main.size() - i),
          [&](const uint8_t *frame, size_t size, bool) {
            assert(size == 241 && frame[4] == 0x61);
            ++accepted;
          },
          1000);
    }
    assert(accepted == 1 && parser.quality.crc_bad == 0);
  }

  // Ein Kopfmuster im Pixelpuffer ist kein zusätzliches Telegramm.
  std::vector<uint8_t> display(5125, 0);
  const uint8_t header[] = {0xa5, 0x13, 0x36, 0x01, 0x55};
  std::copy(std::begin(header), std::end(header), display.begin());
  const uint8_t poll[] = {0xa5, 0x13, 0x36, 0x01, 0x34, 0x50, 0xc3};
  std::copy(std::begin(poll), std::end(poll), display.begin() + 100);
  pbx::Parser parser;
  unsigned images = 0;
  parser.feed(
      display.data(), display.size(),
      [&](auto frame, auto size, bool) {
        assert(frame[4] == 0x55 && size == 5125);
        ++images;
      },
      2000);
  assert(images == 1 && parser.quality.unchecked == 1);

  pbx::Keys keys;
  uint8_t code = 0;
  assert(!keys.poll(0, code));
  assert(keys.queue(0x10, 0));
  assert(!keys.queue(0x20, 1));
  assert(keys.poll(100, code) && code == 0x10);
  assert(keys.poll(200, code) && code == 0xff);
  assert(!keys.poll(300, code));
  assert(keys.poll(500100, code) && code == 0x00);
  assert(keys.poll(500200, code) && code == 0xff);
  assert(!keys.busy());
  assert(keys.queue(0x70, 1000000));
  assert(keys.poll(1000010, code) && code == 0x70);
  assert(keys.poll(1000020, code) && code == 0x00);
  assert(keys.poll(1000030, code) && code == 0xff);
  assert(!keys.busy());
  assert(keys.queue(0x80, 2000000));
  assert(keys.expire(4000001));
  assert(!keys.poll(4000002, code));
  std::cout << "Bestanden: Referenz-CRC, Vorzeichen, Blockgrenzen, Bildkopf und Tastenfolge\n";
}

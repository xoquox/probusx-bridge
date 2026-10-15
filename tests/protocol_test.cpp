#include "protocol.h"
#include <fstream>
#include <vector>
#include <cassert>
#include <iostream>
#include <map>
static std::vector<uint8_t> read(const char *path) {
  std::ifstream f(path, std::ios::binary);
  assert(f);
  return {std::istreambuf_iterator<char>(f), {}};
}
int main(int argc, char **argv) {
  assert(argc == 3);
  for (int a = 1; a < 3; a++) {
    auto bytes = read(argv[a]);
    for (size_t stride : {size_t(1), size_t(7), size_t(1024)}) {
      pbx::Parser parser;
      std::map<int, int> counts;
      int samples = 0;
      for (size_t at = 0; at < bytes.size(); at += stride)
        parser.feed(bytes.data() + at, std::min(stride, bytes.size() - at),
                    [&](const uint8_t *p, size_t n, bool) {
                      int cmd = p[1] == 0xb3 ? 0xb3 : p[4];
                      counts[cmd]++;
                      if (cmd == 0x61) {
                        assert(n == 241);
                        int s1 = pbx::word(p + 10);
                        assert(s1 > 0 && s1 < 1500);
                        samples++;
                      }
                    });
      assert(counts[0x55] == (a == 1 ? 600 : 120));
      assert(counts[0xf0] == (a == 1 ? 600 : 120));
      assert(counts[0x61] == (a == 1 ? 300 : 60));
      assert(counts[0xb3] == (a == 1 ? 0 : 106));
      assert(parser.quality.crc_bad == 0);
      assert(parser.quality.families[pbx::MAIN].crc_ok == (a == 1 ? 300 : 60));
      assert(parser.quality.families[pbx::DISPLAY].unchecked == (a == 1 ? 600 : 120));
      std::cout << a << " Blockgröße=" << stride << " Hauptwerte=" << samples
                << " Display=" << counts[0x55] << " verworfene-CRC=" << parser.rejected << "\n";
    }
  }
  pbx::Keys k;
  uint8_t b = 0;
  assert(!k.poll(0, b));
  assert(k.queue(0x10, 0));
  assert(!k.queue(0x20, 1));
  assert(k.poll(100, b) && b == 0x10);
  assert(k.poll(200, b) && b == 0xff);
  assert(!k.poll(300, b));
  assert(k.poll(500100, b) && b == 0);
  assert(k.poll(500200, b) && b == 0xff);
  assert(!k.busy());
  assert(k.queue(0x70, 1000000));
  assert(k.poll(1000010, b) && b == 0x70);
  assert(k.poll(1000020, b) && b == 0);
  assert(k.poll(1000030, b) && b == 0xff);
  assert(!k.busy());
  assert(k.queue(0x80, 2000000));
  assert(k.expire(4000001));
  assert(!k.poll(4000002, b));
  uint8_t reply[] = {0xa5, 0xb3, 0x70, 0x6c, 0xd8};
  assert(pbx::valid(reply, 5));
  reply[2] ^= 1;
  assert(!pbx::valid(reply, 5));
  // Eine kopfähnliche Bytefolge im Bild darf keine Reglerabfrage auslösen.
  std::vector<uint8_t> display(5125);
  display[0] = 0xa5;
  display[1] = 0x13;
  display[2] = 0x36;
  display[3] = 1;
  display[4] = 0x55;
  uint8_t poll[] = {0xa5, 0x13, 0x36, 1, 0x34, 0x50, 0xc3};
  memcpy(display.data() + 100, poll, 7);
  pbx::Parser p;
  int frames = 0;
  p.feed(display.data(), display.size(), [&](auto f, auto n, bool) {
    assert(f[4] == 0x55 && n == 5125);
    frames++;
  });
  assert(frames == 1);
  std::cout << "Bestanden: Tastenfolge, Verfall, CRC und Kopfmuster innerhalb der Bilddaten\n";
}

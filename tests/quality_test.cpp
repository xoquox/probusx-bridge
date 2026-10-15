#include "protocol.h"
#include <fstream>
#include <vector>
#include <cassert>
#include <iostream>
int main(int argc, char **argv) {
  assert(argc == 2);
  std::ifstream in(argv[1], std::ios::binary);
  std::vector<uint8_t> all{std::istreambuf_iterator<char>(in), {}};
  const uint8_t head[] = {0xa5, 0x13, 0x36, 1, 0x61};
  auto it = std::search(all.begin(), all.end(), std::begin(head), std::end(head));
  assert(it != all.end());
  std::vector<uint8_t> main(it, it + 241);
  assert(main[238] == 0xaa && pbx::crc(main.data(), 238) == uint16_t(main[239] * 256 + main[240]));
  for (int pos = 5; pos < 238; pos++)
    for (int bit = 0; bit < 8; bit++) {
      auto broken = main;
      broken[pos] ^= 1 << bit;
      pbx::Parser p;
      int accepted = 0;
      p.feed(broken.data(), broken.size(), [&](auto, auto, bool) { accepted++; }, 100);
      assert(!accepted && p.quality.crc_bad == 1 && p.quality.families[pbx::MAIN].last_us == 0);
      assert(p.quality.last_bad.expected != p.quality.last_bad.received);
      p.feed(main.data(), main.size(), [&](auto, auto, bool) { accepted++; }, 200);
      assert(accepted == 1 && p.quality.crc_ok == 1);
    }
  {
    pbx::Parser p;
    auto f = main;
    f[238] ^= 1;
    int accepted = 0;
    p.feed(f.data(), f.size(), [&](auto, auto, bool) { accepted++; }, 100);
    assert(!accepted && p.quality.trailer_errors == 1 && p.quality.crc_bad == 0);
  }
  {
    pbx::Parser p;
    int accepted = 0;
    auto callback = [&](auto, auto, bool) { accepted++; };
    p.feed(main.data(), 50, callback, 1000);
    p.tick(101001);
    assert(p.quality.partial_timeouts == 1);
    p.feed(main.data(), main.size(), callback, 200000);
    assert(accepted == 1);
    p.tick(2800000);
    assert(p.quality.families[pbx::MAIN].gaps == 1);
    p.tick(3900000);
    assert(p.quality.families[pbx::MAIN].gaps == 1);
    p.feed(main.data(), main.size(), callback, 4000000);
    assert(!p.quality.families[pbx::MAIN].gap_open);
    assert(p.quality.families[pbx::MAIN].max_gap_us == 3800000);
  }
  {
    pbx::Quality q;
    q.received(1000000, 123);
    q.frame(1000000, pbx::MAIN, true, true);
    q.frame(1000000, pbx::EXT, true, false);
    q.frame(1000000, pbx::DISPLAY, false, true);
    uint64_t bytes, ok, bad, unverified;
    q.window(2000000, bytes, ok, bad, unverified);
    assert(bytes == 123 && ok == 1 && bad == 1 && unverified == 1);
    q.window(61000000, bytes, ok, bad, unverified);
    assert(bytes == 0 && ok == 0 && bad == 0 && unverified == 0);
    for (int i = 0; i < 100; i++) {
      q.event(i, pbx::CRC_ERROR);
    }
    assert(q.events_total == 101);
    assert(q.events[100 % 64].us == 99);
  }
  std::cout << "Bestanden: 1864 Bitfehler verworfen, Erholung, separate Endemarkierung, "
               "Teiltelegramm-Zeitgrenze, Pausen, Zeitfenster und begrenzte Ereignisse\n";
}

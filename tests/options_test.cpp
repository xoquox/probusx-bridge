#include "runtime_options.h"
#include "protocol.h"
#include <cassert>
#include <fstream>
#include <vector>
#include <iostream>
int main(int argc, char **argv) {
  pbx::RuntimeOptions o;
  assert(o.valid() && o.transmit && o.analysis && o.display_ms == 250);
  uint32_t n;
  assert(!pbx::option_number("-1", n) && !pbx::option_number("nan", n) &&
         !pbx::option_number("1.5", n));
  for (unsigned ms : {0u, 99u, 5001u, 60000u}) {
    o.display_ms = ms;
    assert(!o.valid());
  }
  o.display_ms = 100;
  assert(o.valid());
  o.display_ms = 5000;
  assert(o.valid());
  o.temperature_ms = 249;
  assert(!o.valid());
  o.temperature_ms = 60000;
  assert(o.valid());
  o.output_ms = 60001;
  assert(!o.valid());
  o = pbx::RuntimeOptions{};
  o.transmit = 0;
  o.analysis = 0;
  assert(o.valid());
  assert(argc == 2);
  std::ifstream in(argv[1], std::ios::binary);
  std::vector<uint8_t> bytes{std::istreambuf_iterator<char>(in), {}};
  const uint8_t h[] = {0xa5, 0x13, 0x36, 1, 0x61};
  auto it = std::search(bytes.begin(), bytes.end(), std::begin(h), std::end(h));
  assert(it != bytes.end());
  std::vector<uint8_t> frame(it, it + 241);
  pbx::Parser p;
  p.quality.enabled = false;
  int accepted = 0;
  p.feed(frame.data(), frame.size(), [&](auto, auto, bool) { accepted++; }, 1000);
  assert(accepted == 1);
  frame[10] ^= 1;
  p.feed(frame.data(), frame.size(), [&](auto, auto, bool) { accepted++; }, 2000);
  assert(accepted == 1 && p.rejected == 1);
  assert(p.quality.bytes == 0 && p.quality.crc_ok == 0 && p.quality.crc_bad == 0 &&
         p.quality.events_total == 0);
  p.feed(frame.data(), 10, [&](auto, auto, bool) { accepted++; }, 3000);
  p.tick(200000);
  assert(p.quality.partial_timeouts == 0);
  frame[10] ^= 1;
  p.feed(frame.data(), frame.size(), [&](auto, auto, bool) { accepted++; }, 300000);
  assert(accepted == 2);
  std::cout << "Bestanden: Sendestandard, Einstellgrenzen und ungültige Eingaben. CRC-Verwerfung "
               "und Parsererholung auch bei ausgeschalteter Analyse.\n";
}

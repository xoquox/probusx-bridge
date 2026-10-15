#include "protocol.h"
#include <fstream>
#include <vector>
#include <cassert>
#include <iostream>
int main(int argc, char **argv) {
  assert(argc == 3);
  for (int a = 1; a < argc; a++) {
    std::ifstream in(argv[a], std::ios::binary);
    assert(in);
    pbx::Parser p;
    uint64_t t;
    uint32_t n;
    while (in.read(reinterpret_cast<char *>(&t), 8)) {
      in.read(reinterpret_cast<char *>(&n), 4);
      assert(n > 0 && n <= 1024);
      std::vector<uint8_t> b(n);
      in.read(reinterpret_cast<char *>(b.data()), n);
      assert(in);
      p.feed(b.data(), n, [](auto, auto, bool) {}, t);
    }
    assert(p.quality.partial_timeouts == 0);
    assert(p.quality.crc_bad == 0);
    assert(p.quality.headers_bad == 0);
    for (int i : {int(pbx::MAIN), int(pbx::EXT), int(pbx::DISPLAY), int(pbx::POLL)}) {
      auto &f = p.quality.families[i];
      assert(f.gaps == 0);
      std::cout << a << " " << pbx::family_name(i) << " längste-Pause-ms=" << f.max_gap_us / 1000.0
                << "\n";
    }
    std::cout << "CRC geprüft " << p.quality.crc_ok << ", ungeprüft " << p.quality.unchecked
              << "; keine falschen Zeit- oder Pausenalarme\n";
  }
}

// Erzeugt die Browser-Prüfdaten mit demselben Ringpuffer wie die Firmware.
#include "history_transfer.h"
#include <cstdio>
#include <cstdlib>
#include <vector>
int main(int argc, char **argv) {
  using namespace pbx_history;
  if (argc != 4)
    return 1;
  Store store(std::free);
  for (int i = 0; i < 8; ++i)
    store.add_block(std::malloc(chunk_bytes));
  for (uint64_t ms = 864000000 - 3600000; ms <= 864000000; ms += 1000) {
    Record r{};
    r.ms = ms;
    for (unsigned c = 0; c < channels; ++c)
      r.values[c] = c < 14 ? 200 + c * 10 + (ms / 1000) % 200 : (c >= 22 && c <= 24 ? 10000 : 200);
    if (ms % 10000 == 0)
      r.values[27] = 0;
    if (ms % 30000 == 0)
      r.values[1] = missing;
    store.append(r);
  }
  Header h;
  h.from = std::strtoull(argv[1], nullptr, 10);
  h.to = std::strtoull(argv[2], nullptr, 10);
  h.version = 2;
  h.count = std::min<size_t>(256, std::atoi(argv[3]));
  if (h.count < 16 || h.count > 1024 || h.from >= h.to)
    return 2;
  std::vector<Bucket> b(h.count);
  store.aggregate(h.from, h.to, b.data(), b.size());
  h.count = compact(b.data(), b.size());
  std::fwrite(&h, sizeof(h), 1, stdout);
  std::fwrite(b.data(), sizeof(Bucket), h.count, stdout);
}

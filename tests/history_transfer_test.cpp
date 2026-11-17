// Echte Übertragungslogik mit langsamen, abgebrochenen und leeren Antworten prüfen.
#include "history_transfer.h"
#include <cassert>
#include <cstring>
#include <vector>
#include <iostream>
using namespace pbx_history;
struct Sender {
  uint64_t clock = 0, delay = 0;
  size_t calls = 0, fail_at = SIZE_MAX, yields = 0;
  std::vector<uint8_t> bytes;
  uint64_t now() const {
    return clock;
  }
  bool send(const void *p, size_t n) {
    ++calls;
    clock += delay;
    if (calls == fail_at)
      return false;
    if (n) {
      auto *b = static_cast<const uint8_t *>(p);
      bytes.insert(bytes.end(), b, b + n);
    }
    return true;
  }
  void yield() {
    ++yields;
  }
};
int main() {
  assert(!deadline_reached(0xfffffff0u, 10));
  assert(!deadline_reached(9, 10));
  assert(deadline_reached(10, 10));
  assert(deadline_reached(11, 10));
  Bucket rows[256]{};
  rows[4].count = 1;
  rows[4].first_ms = 100;
  rows[4].last_ms = 200;
  rows[4].maximum[27] = 200;
  rows[200].count = 1;
  rows[200].first_ms = 900;
  rows[200].last_ms = 900;
  rows[200].gap_mask = 8;
  assert(compact(rows, 256) == 2);
  assert(rows[0].maximum[27] == 200 && rows[1].gap_mask == 8);
  Header h;
  h.version = 2;
  h.count = 2;
  h.from = 0;
  h.to = 1000;
  Sender normal;
  assert(send_response(normal, h, rows));
  assert(normal.bytes.size() == 32 + 512 && normal.yields == 1);
  assert(memcmp(normal.bytes.data(), &h, 32) == 0);
  Sender broken;
  broken.fail_at = 2;
  assert(!send_response(broken, h, rows));
  assert(broken.calls == 2);
  Header empty = h;
  empty.count = 0;
  Sender none;
  assert(send_response(none, empty, nullptr));
  assert(none.bytes.size() == 32 && none.calls == 2);
  h.count = 256;
  Sender slow;
  slow.delay = 1000000;
  assert(!send_response(slow, h, rows));
  assert(slow.clock == query_timeout_us && slow.calls == 8);
  Sender full;
  assert(send_response(full, h, rows));
  assert(full.bytes.size() == 65568);
  std::cout << "Bestanden: kompakte leere/belegte Zeitkörbe, Impuls und Lücke erhalten, "
               "64-KiB-Grenze, Gesamtfrist, Abbruch und leere Antwort.\n";
}

#include "history_store.h"
#include <cassert>
#include <cstdlib>
#include <iostream>
using namespace pbx_history;
int main() {
  Settings s;
  assert(s.valid());
  s.interval_s = 0;
  assert(!s.valid());
  s.interval_s = 3601;
  assert(!s.valid());
  s.interval_s = 3600;
  assert(s.valid());
  s.flex1 = 2;
  assert(!s.valid());
  Store store(std::free);
  assert(store.count() == 0 && store.oldest() == 0 && !store.release_one());
  Record r{};
  assert(!store.append(r));
  for (int i = 0; i < 3; ++i)
    store.add_block(std::malloc(chunk_bytes));
  for (unsigned i = 0; i < 2 * Store::per_chunk + 1; ++i) {
    r.ms = i * 1000;
    for (auto &v : r.values)
      v = missing;
    r.values[0] = int16_t(i % 1000);
    r.values[14] = i == 3 ? 200 : 0;
    assert(store.append(r));
  }
  auto oldest = store.oldest();
  auto count = store.count();
  assert(count == 2 * Store::per_chunk + 1);
  // Ein belegter ältester Block wird freigegeben. Die neueren Werte bleiben unverändert.
  assert(store.release_one());
  assert(store.count() == count - Store::per_chunk && store.oldest() > oldest);
  store.add_block(std::malloc(chunk_bytes));
  assert(store.count() == count - Store::per_chunk);
  // Ungenutzte Blöcke kosten keine vorhandenen Messpunkte beim Reservieren einer Aufnahme.
  count = store.count();
  assert(store.release_one());
  assert(store.count() == count);
  for (unsigned i = 0; i < 5 * Store::per_chunk; ++i) {
    r.ms += 1000;
    r.values[0] = 20;
    store.append(r);
  }
  assert(store.count() <= 2 * Store::per_chunk && store.removed() > 0);
  Store sample(std::free);
  sample.add_block(std::malloc(chunk_bytes));
  for (int i = 0; i < 4; ++i) {
    Record a{};
    a.ms = 1000 + i * 100;
    for (auto &v : a.values)
      v = missing;
    a.values[0] = i == 1 ? 800 : 200;
    a.values[14] = i == 1 ? 200 : 0;
    if (i == 2)
      a.values[0] = missing;
    sample.append(a);
  }
  Bucket points[2];
  sample.aggregate(1000, 2000, points, 2);
  assert(points[0].count == 4 && points[0].minimum[0] == 200 && points[0].maximum[0] == 800 &&
         points[0].last[0] == 200);
  assert(points[0].maximum[14] == 200 && points[0].first[14] == 0 && points[0].last[14] == 0);
  assert(points[0].gap_mask & 1);
  assert(points[1].count == 0 && points[1].minimum[0] == missing);
  sample.aggregate(1150, 1250, points, 2);
  assert(points[0].count == 0 && points[1].count == 1);
  assert(points[1].minimum[0] == missing);
  std::cout
      << "Bestanden: RAM-Blöcke, alte Daten zuerst, freie Blöcke ohne Datenverlust, Ringüberlauf, "
         "Frischelücken, Min/Max mit kurzem Ausgangsimpuls und Zeitraumgrenzen.\n";
}

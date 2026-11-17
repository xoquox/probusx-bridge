#pragma once
#include "history_store.h"
namespace pbx_history {
constexpr size_t max_query_buckets = 256;
constexpr uint64_t query_timeout_us = 8000000;
// Der 32-Bit-Millisekundenzähler darf während des Abrufs überlaufen.
inline bool deadline_reached(uint32_t now, uint32_t deadline) {
  return int32_t(now - deadline) >= 0;
}
// Leere Zeitkörbe kosten keine Funkzeit. Reihenfolge und echte Messzeiten bleiben erhalten.
inline size_t compact(Bucket *points, size_t size) {
  size_t count = 0;
  for (size_t i = 0; i < size; ++i)
    if (points[i].count)
      points[count++] = points[i];
  return count;
}
// Keine Sperre halten. Kleine Blöcke und eine Gesamtfrist begrenzen langsame Empfänger.
// Der Sender begrenzt zusätzlich jeden einzelnen Sendeaufruf auf eine Sekunde.
template <class Sender>
bool send_response(Sender &sender, const Header &header, const Bucket *points) {
  const uint64_t started = sender.now();
  auto send = [&](const void *p, size_t n) {
    return sender.now() - started < query_timeout_us && sender.send(p, n);
  };
  if (!send(&header, sizeof(header)))
    return false;
  for (size_t i = 0; i < header.count; i += 2) {
    if (!send(points + i, std::min<size_t>(2, header.count - i) * sizeof(Bucket)))
      return false;
    sender.yield();
  }
  return send(nullptr, 0);
}
} // Namensraum pbx_history

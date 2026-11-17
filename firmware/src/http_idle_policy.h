#pragma once
#include <cstdint>
namespace http_connections {
constexpr uint64_t idle_ms = 30000;
inline bool idle_expired(uint64_t now, uint64_t last_request, bool operation_busy) {
  // Asynchrone Aufnahme und OTA besitzen den Request bis zum ausdrücklichen Abschluss.
  // Den dazugehörigen Socket niemals über die Sitzungsbereinigung freigeben.
  return !operation_busy && now >= last_request && now - last_request >= idle_ms;
}
} // Namensraum http_connections

#pragma once
#include <cstddef>
#include <cstdint>
#include "esp_http_server.h"
void live_init();
void live_feed(const uint8_t *p, size_t n, bool transmit);
void live_reset(bool error = false);
void live_tick();
void live_register(httpd_handle_t server, const char *csrf);

void live_uart_fault(unsigned kind);
void live_uart_buffer(size_t bytes);

#include "runtime_options.h"
// Momentaufnahme unter kurzer Sperre kopieren. Netzwerkzugriffe folgen erst nach der Freigabe.
struct LiveSnapshot {
  uint64_t now_us{}, main_at{}, ext_at{}, display_at{}, poll_at{};
  uint32_t main_count{}, ext_count{}, display_count{}, tx_count{};
  int16_t temperatures[14]{}, outputs[15]{};
  uint8_t display[5120]{};
  pbx::RuntimeOptions options;
  bool enabled{}, busy{}, pending{}, analysis{};
  uint64_t crc_ok{}, crc_bad{}, uart_errors{}, tx_errors{}, key_expired{};
};
void live_snapshot(LiveSnapshot &out);
// Rückgabe nullptr bedeutet: angenommen. Die vorhandene Abfrage steuert weiterhin den Buszugriff.
const char *live_queue_key(const char *name);

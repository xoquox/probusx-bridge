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

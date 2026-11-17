#pragma once
#include <cstddef>
#include <cstdint>
#include "cJSON.h"
#include "esp_http_server.h"
void history_init();
void history_register(httpd_handle_t server, const char *token);
void history_status(cJSON *root);
bool history_flex1_enabled();
// Nur mit Vorgangsreservierung aufrufen. Entfernt bei Platzbedarf die ältesten Verlaufsblöcke.
void *history_capture_allocate(size_t bytes);
void history_capture_release(void *storage);

// Schnelle Kopie aus dem Bus-Empfang. Kein Warten auf Verlaufssperren oder Netzwerk.
void history_offer(uint64_t now_us, uint64_t main_at, uint64_t ext_at, const int16_t *temperatures,
                   const int16_t *outputs);

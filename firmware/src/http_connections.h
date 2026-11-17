#pragma once
#include <cstdint>
#include "esp_http_server.h"

namespace http_connections {
constexpr unsigned max_sessions = 6;
// Installiert Sitzungsverwaltung vor httpd_start(). Danach start() aufrufen.
void configure(httpd_config_t &config);
void start(httpd_handle_t server);
// Am Anfang jedes HTTP-Handlers aufrufen. Nur der HTTP-Task verwendet diesen Kontext.
void touch(httpd_req_t *request);
// Während eines asynchronen Leseauftrags keine Leerlaufbereinigung dieser Sitzung.
void protect_async(httpd_req_t *request, bool owned);
} // Namensraum http_connections

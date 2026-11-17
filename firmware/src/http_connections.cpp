#include "http_connections.h"
#include "http_idle_policy.h"
#include "capture_api.h"
#include <new>
#include <atomic>
#include "lwip/sockets.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace http_connections {
struct Session {
  uint64_t last_request;
  std::atomic<bool> async_owned{false};
};
static uint64_t now_ms() { return esp_timer_get_time() / 1000; }
static void free_session(void *context) { delete static_cast<Session *>(context); }
static esp_err_t open_session(httpd_handle_t server, int fd) {
  auto *session = new (std::nothrow) Session{now_ms()};
  if (!session) return ESP_ERR_NO_MEM;
  httpd_sess_set_ctx(server, fd, session, free_session);
  return ESP_OK;
}
void touch(httpd_req_t *request) {
  auto *session = static_cast<Session *>(request->sess_ctx);
  if (session) session->last_request = now_ms();
}
void protect_async(httpd_req_t *request, bool owned) {
  auto *session = static_cast<Session *>(request->sess_ctx);
  if (session) session->async_owned = owned;
}
// Läuft ausschließlich im HTTP-Task. Dort kann kein neuer asynchroner Handler
// zwischen der Prüfung der Reservierung und dem Schließen beginnen.
static void clean_idle(void *context) {
  auto server = static_cast<httpd_handle_t>(context);
  if (operation_busy()) return;
  int clients[max_sessions];
  size_t count = max_sessions;
  if (httpd_get_client_list(server, &count, clients) != ESP_OK) return;
  const uint64_t now = now_ms();
  for (size_t i = 0; i < count; ++i) {
    auto *session = static_cast<Session *>(httpd_sess_get_ctx(server, clients[i]));
    if (session && !session->async_owned && idle_expired(now, session->last_request, false)) {
      // Direkt in diesem Task herunterfahren. Der Server gibt Sitzung und Kontext
      // danach über seinen normalen Fehlerpfad frei. Keine verzögerte FD-Wiederverwendung.
      shutdown(clients[i], SHUT_RDWR);
    }
  }
}
static void cleanup_task(void *context) {
  auto server = static_cast<httpd_handle_t>(context);
  while (true) {
    vTaskDelay(pdMS_TO_TICKS(5000));
    if (httpd_queue_work(server, clean_idle, context) != ESP_OK)
      ESP_LOGW("http_sessions", "Sitzungsprüfung konnte nicht eingereiht werden.");
  }
}
void configure(httpd_config_t &config) {
  config.max_open_sockets = max_sessions;
  config.open_fn = open_session;
}
void start(httpd_handle_t server) {
  configASSERT(xTaskCreatePinnedToCore(cleanup_task, "http_cleanup", 3072, server,
                                     2, nullptr, 0) == pdPASS);
}
} // Namensraum http_connections

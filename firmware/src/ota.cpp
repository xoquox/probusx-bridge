#include "http_connections.h"
#include "ota.h"
#include "ota_transfer.h"
#include <unistd.h>
#include "capture_api.h"
#include <atomic>
#include <cstring>
#include <cstdio>
#include <algorithm>
#include "esp_ota_ops.h"
#include "esp_app_desc.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "lwip/sockets.h"

static httpd_handle_t server;
static const char *csrf;
static QueueHandle_t uploads;
static std::atomic<bool> pending{false}, updating{false};
static std::atomic<size_t> written{0}, total{0};
static esp_timer_handle_t deadline;
static const char *TAG = "ota";
static void wake_http(void *) {}
static esp_err_t reply(httpd_req_t *req, const char *body, const char *status = "200 OK") {
  httpd_resp_set_status(req, status);
  httpd_resp_set_type(req, "application/json");
  httpd_resp_set_hdr(req, "Cache-Control", "no-store");
  return httpd_resp_send(req, body, HTTPD_RESP_USE_STRLEN);
}
static void close_async(httpd_req_t *req, bool close) {
  if (close)
    shutdown(httpd_req_to_sockfd(req), SHUT_RDWR);
  httpd_req_async_handler_complete(req);
  httpd_queue_work(server, wake_http, nullptr);
}
static void reject(httpd_req_t *req, const char *error) {
  char body[320];
  snprintf(body, sizeof(body), "{\"error\":\"%s\"}", error);
  reply(req, body, "400 Bad Request");
}
// Schreibt nur in die inaktive OTA-Partition. Erst nach erfolgreicher Prüfung als Startversion
// wählen.
static void upload_worker(void *) {
  httpd_req_t *req;
  while (true) {
    xQueueReceive(uploads, &req, portMAX_DELAY);
    wifi_prepare_capture(); // Funkkanal des Zugangspunkts während der Übertragung beibehalten.
    const esp_partition_t *target = esp_ota_get_next_update_partition(nullptr);
    struct Backend {
      httpd_req_t *req;
      const esp_partition_t *target;
      esp_ota_handle_t handle = 0;
      int read(uint8_t *p, size_t n) {
        return httpd_req_recv(req, reinterpret_cast<char *>(p), n);
      }
      bool begin(size_t n) {
        return esp_ota_begin(target, n, &handle) == ESP_OK;
      }
      bool write(const uint8_t *p, size_t n) {
        return esp_ota_write(handle, p, n) == ESP_OK;
      }
      void progress(size_t n) {
        written = n;
      }
      void abort() {
        esp_ota_abort(handle);
      }
      bool end() {
        return esp_ota_end(handle) == ESP_OK;
      }
      bool select() {
        return esp_ota_set_boot_partition(target) == ESP_OK;
      }
    } backend{req, target};
    const char *error = ota_policy::transfer(backend, req->content_len, target ? target->size : 0);
    if (error) {
      reject(req, error);
      close_async(req, true); // Übrige Uploadbytes verwerfen. Sie sind keine neue HTTP-Anfrage.
      updating = false;
      operation_give();
      ESP_LOGW(TAG, "%s", error);
      continue;
    }
    const esp_err_t sent = reply(req, "{\"accepted\":true,\"reboot\":true}");
    close_async(req, sent != ESP_OK);
    // Reservierung bis zum Neustart halten. Das gilt auch nach einem Verbindungsabbruch.
    vTaskDelay(pdMS_TO_TICKS(1200));
    esp_restart();
  }
}
// Prüft Token, Dateityp, Größe und Gerätezustand vor Beginn des Updates.
static esp_err_t upload_handler(httpd_req_t *req) {
  http_connections::touch(req);
  char token[40], type[64];
  if (httpd_req_get_hdr_value_str(req, "X-CSRF-Token", token, sizeof(token)) != ESP_OK ||
      strcmp(token, csrf)) {
    reply(req, "{\"error\":\"Seite bitte neu laden.\"}", "403 Forbidden");
    return ESP_FAIL;
  }
  if (httpd_req_get_hdr_value_str(req, "Content-Type", type, sizeof(type)) != ESP_OK ||
      strcmp(type, "application/octet-stream")) {
    reply(req, "{\"error\":\"Binärdatei erwartet.\"}", "415 Unsupported Media Type");
    return ESP_FAIL;
  }
  if (!ota_policy::upload_size(req->content_len, ota_policy::slot_size)) {
    reply(req, "{\"error\":\"Ungültige Dateigröße. Bitte die Datei mit -ota.bin wählen.\"}",
          "413 Payload Too Large");
    return ESP_FAIL;
  }
  if (pending || !operation_take()) {
    reply(req, "{\"error\":\"Aufnahme, Startprüfung, Update oder WLAN-Aktion läuft bereits.\"}",
          "409 Conflict");
    return ESP_FAIL;
  }
  httpd_req_t *async = nullptr;
  if (httpd_req_async_handler_begin(req, &async) != ESP_OK) {
    operation_give();
    reply(req, "{\"error\":\"Update konnte nicht gestartet werden.\"}", "503 Service Unavailable");
    return ESP_FAIL;
  }
  total = req->content_len;
  written = 0;
  updating = true;
  if (xQueueSend(uploads, &async, 0) != pdTRUE) {
    reject(async, "Update belegt.");
    close_async(async, true);
    updating = false;
    operation_give();
  }
  return ESP_OK;
}
static esp_err_t health_handler(httpd_req_t *req) {
  http_connections::touch(req);
  return reply(req, "{\"health\":\"pbx-ota-v1\"}");
}
void ota_register(httpd_handle_t http, const char *token) {
  server = http;
  csrf = token;
  uploads = xQueueCreate(1, sizeof(httpd_req_t *));
  configASSERT(uploads);
  httpd_uri_t route{};
  route.uri = "/api/update";
  route.method = HTTP_POST;
  route.handler = upload_handler;
  ESP_ERROR_CHECK(httpd_register_uri_handler(server, &route));
  route.uri = "/api/health";
  route.method = HTTP_GET;
  route.handler = health_handler;
  ESP_ERROR_CHECK(httpd_register_uri_handler(server, &route));
  configASSERT(xTaskCreatePinnedToCore(upload_worker, "ota_upload", 10240, nullptr, 4, nullptr,
                                       0) == pdPASS);
}
bool ota_restart_allowed() {
  return !pending && !updating;
}
void ota_add_status(cJSON *root) {
  auto *ota = cJSON_AddObjectToObject(root, "ota");
  cJSON_AddBoolToObject(ota, "pending_verify", pending);
  cJSON_AddBoolToObject(ota, "updating", updating);
  cJSON_AddNumberToObject(ota, "written", written);
  cJSON_AddNumberToObject(ota, "total", total);
  cJSON_AddNumberToObject(ota, "max_size", ota_policy::slot_size);
  const auto *part = esp_ota_get_running_partition();
  cJSON_AddStringToObject(ota, "slot", part ? part->label : "unknown");
}
// Ein neuer Start muss rechtzeitig bestätigt werden. Andernfalls den Rückfall auslösen.
static void boot_timeout(void *) {
  // Beim nächsten Rücksetzen ersetzt der Bootloader einen unbestätigten Start durch die alte
  // Firmware.
  ESP_LOGE(
      TAG,
      "Startprüfung hat Zeitgrenze überschritten. Neustart stellt die alte Firmware wieder her.");
  esp_restart();
}
void ota_boot_start() {
  esp_ota_img_states_t state;
  if (esp_ota_get_state_partition(esp_ota_get_running_partition(), &state) != ESP_OK ||
      state != ESP_OTA_IMG_PENDING_VERIFY)
    return;
  pending = true;
  const esp_timer_create_args_t args = {boot_timeout, nullptr, ESP_TIMER_TASK, "ota_deadline",
                                        false};
  ESP_ERROR_CHECK(esp_timer_create(&args, &deadline));
  ESP_ERROR_CHECK(esp_timer_start_once(deadline, 60000000));
}
static void boot_check(void *) {
  vTaskDelay(pdMS_TO_TICKS(20000));
  const int fd = socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
  bool good = false;
  if (fd >= 0) {
    timeval timeout{3, 0};
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(80);
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (connect(fd, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) == 0) {
      constexpr char request[] =
          "GET /api/health HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n";
      if (send(fd, request, sizeof(request) - 1, 0) == sizeof(request) - 1) {
        char response[1024]{};
        size_t count = 0;
        while (count < sizeof(response) - 1) {
          const int n = recv(fd, response + count, sizeof(response) - 1 - count, 0);
          if (n <= 0)
            break;
          count += n;
          if (strstr(response, "\"health\":\"pbx-ota-v1\"")) {
            good = true;
            break;
          }
        }
      }
    }
    close(fd);
  }
  if (good && esp_ota_mark_app_valid_cancel_rollback() == ESP_OK) {
    esp_timer_stop(deadline);
    pending = false;
    ESP_LOGI(TAG,
             "OTA-Start bestätigt: PSRAM, UART und HTTP bereit. Lokale HTTP-Prüfung bestanden.");
  } else
    boot_timeout(nullptr);
  vTaskDelete(nullptr);
}
void ota_boot_ready() {
  // Erst aufrufen, nachdem PSRAM, Empfang, WLAN, HTTP-Routen und TCP-Task bereit sind.
  if (pending)
    configASSERT(xTaskCreatePinnedToCore(boot_check, "ota_boot_test", 4096, nullptr, 3, nullptr,
                                         0) == pdPASS);
}

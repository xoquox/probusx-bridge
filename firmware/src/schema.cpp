#include "schema.h"
#include "schema_store.h"
#include "schema_validation.h"
#include "http_connections.h"
#include "capture_api.h"
#include "ota.h"
#include "cJSON.h"
#include "esp_partition.h"
#include "esp_flash.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "lwip/sockets.h"
#include <cmath>
#include <string>
#include <memory>
#include <set>

namespace {
using namespace schema_store;
using namespace schema_validation;
const esp_partition_t *partition;
httpd_handle_t server;
const char *csrf;
SemaphoreHandle_t mutex;
QueueHandle_t jobs;
Record image_record, layout_record;
std::string layout = "{\"version\":1,\"items\":[]}";
struct Job { httpd_req_t *request; bool image, read; };
struct Lock {
  Lock() { xSemaphoreTake(mutex, portMAX_DELAY); }
  ~Lock() { xSemaphoreGive(mutex); }
};
struct Flash {
  bool read(size_t offset, void *data, size_t size) {
    return partition && esp_partition_read(partition, offset, data, size) == ESP_OK;
  }
  bool erase(size_t offset, size_t size) {
    const bool ok = esp_partition_erase_range(partition, offset, size) == ESP_OK;
    vTaskDelay(pdMS_TO_TICKS(2));
    return ok;
  }
  bool write(size_t offset, const void *data, size_t size) {
    // Der Flash-Treiber bekommt einen internen Puffer, keine Adresse im PSRAM.
    uint8_t copy[1024];
    if (size > sizeof(copy)) return false;
    memcpy(copy, data, size);
    const bool ok = esp_partition_write(partition, offset, copy, size) == ESP_OK;
    vTaskDelay(pdMS_TO_TICKS(1));
    return ok;
  }
};
esp_err_t reply(httpd_req_t *req, cJSON *json, const char *status = "200 OK") {
  char *body = cJSON_PrintUnformatted(json);
  cJSON_Delete(json);
  if (!body) return ESP_FAIL;
  httpd_resp_set_status(req, status);
  httpd_resp_set_type(req, "application/json; charset=utf-8");
  httpd_resp_set_hdr(req, "Cache-Control", "no-store");
  const auto result = httpd_resp_send(req, body, HTTPD_RESP_USE_STRLEN);
  cJSON_free(body);
  return result;
}
esp_err_t error(httpd_req_t *req, const char *message, const char *status = "400 Bad Request") {
  auto *json = cJSON_CreateObject();
  cJSON_AddStringToObject(json, "error", message);
  return reply(req, json, status);
}
esp_err_t status_handler(httpd_req_t *req) {
  http_connections::touch(req);
  auto *json = cJSON_CreateObject();
  {
  Lock lock;
  cJSON_AddBoolToObject(json, "storage_ready", partition != nullptr);
  cJSON_AddNumberToObject(json, "revision", layout_record.header.generation);
  cJSON_AddNumberToObject(json, "image_revision", image_record.header.generation);
  cJSON_AddNumberToObject(json, "image_width", image_record.header.width);
  cJSON_AddNumberToObject(json, "image_height", image_record.header.height);
  cJSON_AddNumberToObject(json, "max_image_bytes", image_limit);
  auto *saved = cJSON_Parse(layout.c_str());
  cJSON_AddItemToObject(json, "items", saved ? cJSON_DetachItemFromObject(saved, "items") : cJSON_CreateArray());
  cJSON_Delete(saved);
  }
  return reply(req, json);
}
void wake_http(void *) {}
void worker(void *) {
  Job job{};
  while (true) {
    xQueueReceive(jobs, &job, portMAX_DELAY);
    auto *req = job.request;
    bool close = false;
    Flash flash;
    if (job.read) {
      const auto record = image_record;
      if (record.bank < 0) error(req, "Noch kein Schema-Bild gespeichert.", "404 Not Found");
      else {
        httpd_resp_set_type(req, record.header.format == 1 ? "image/png" : "image/jpeg");
        httpd_resp_set_hdr(req, "Cache-Control", "no-store");
        httpd_resp_set_hdr(req, "X-Content-Type-Options", "nosniff");
        uint8_t buffer[1024];
        size_t at = 0;
        while (at < record.header.size) {
          const size_t n = std::min(sizeof(buffer), size_t(record.header.size) - at);
          if (!flash.read(record.bank * image_bank + sector + at, buffer, n) ||
              httpd_resp_send_chunk(req, reinterpret_cast<char *>(buffer), n) != ESP_OK) { close = true; break; }
          at += n;
        }
        if (!close && httpd_resp_send_chunk(req, nullptr, 0) != ESP_OK) close = true;
      }
    } else {
      wifi_prepare_capture();
      auto *data = static_cast<uint8_t *>(heap_caps_malloc(req->content_len + 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
      std::unique_ptr<uint8_t, decltype(&heap_caps_free)> memory(data, heap_caps_free);
      bool received = data != nullptr;
      size_t at = 0;
      const int64_t until = esp_timer_get_time() + 60000000;
      while (received && at < req->content_len) {
        char chunk[1024];
        const int n = httpd_req_recv(req, chunk, std::min(sizeof(chunk), size_t(req->content_len) - at));
        if (n <= 0 || esp_timer_get_time() > until) { received = false; break; }
        memcpy(data + at, chunk, n); at += n;
      }
      if (!received) {
        error(req, data ? "Upload unvollständig. Bisheriger Stand bleibt erhalten." : "Zu wenig Speicher für den Upload.");
        close = true;
      } else {
        data[at] = 0;
        uint32_t format = 0, revision = 0;
        uint16_t width = 0, height = 0;
        std::string normalized;
        const bool valid = job.image ? image_info(data, at, format, width, height)
                                     : normalize_layout(reinterpret_cast<char *>(data), at, revision, normalized);
        if (!valid) error(req, job.image ? "Bild ungültig. PNG oder JPEG mit höchstens 4096 Pixeln je Seite verwenden." : "Anordnung ungültig. Kanäle, Bezeichnungen und Positionen prüfen.");
        else if (!job.image && revision != layout_record.header.generation)
          error(req, "Die Anordnung wurde inzwischen geändert. Neu laden und erneut bearbeiten.", "409 Conflict");
        else {
          Record next = job.image ? image_record : layout_record;
          const bool ok = job.image
            ? save(flash, 0, image_bank, data, at, format, width, height, next)
            : save(flash, 2 * image_bank, layout_bank,
                   reinterpret_cast<const uint8_t *>(normalized.data()), normalized.size(), 0, 0, 0, next);
          if (!ok) error(req, "Speichern fehlgeschlagen. Bisheriger Stand bleibt erhalten.", "500 Internal Server Error");
          else {
            { Lock lock;
              if (job.image) image_record = next;
              else { layout_record = next; layout = normalized; }
            }
            auto *json = cJSON_CreateObject();
            cJSON_AddBoolToObject(json, "accepted", true);
            cJSON_AddNumberToObject(json, "revision", next.header.generation);
            reply(req, json);
          }
        }
      }
    }
    if (close) shutdown(httpd_req_to_sockfd(req), SHUT_RDWR);
    httpd_req_async_handler_complete(req);
    httpd_queue_work(server, wake_http, nullptr);
    operation_give();
  }
}
esp_err_t transfer_handler(httpd_req_t *req) {
  http_connections::touch(req);
  const bool read = req->method == HTTP_GET;
  const bool image = read || !strncmp(req->uri, "/api/schema/image", 17);
  if (!partition) { error(req, "Schema-Speicher nicht verfügbar.", "503 Service Unavailable"); return ESP_FAIL; }
  if (!read) {
    char token[40], type[80];
    if (httpd_req_get_hdr_value_str(req, "X-CSRF-Token", token, sizeof(token)) != ESP_OK || strcmp(token, csrf)) {
      error(req, "Seite bitte neu laden.", "403 Forbidden"); return ESP_FAIL;
    }
    if (httpd_req_get_hdr_value_str(req, "Content-Type", type, sizeof(type)) != ESP_OK ||
        strcmp(type, image ? "application/octet-stream" : "application/json")) {
      error(req, "Ungültiger Inhaltstyp.", "415 Unsupported Media Type"); return ESP_FAIL;
    }
    if (req->content_len < 1 || req->content_len > (image ? image_limit : layout_limit)) {
      error(req, "Upload zu groß oder leer.", "413 Payload Too Large"); return ESP_FAIL;
    }
  }
  if (!ota_restart_allowed() || !operation_take()) {
    error(req, "Aufnahme, Update oder andere Aktion läuft bereits.", "409 Conflict"); return ESP_FAIL;
  }
  httpd_req_t *async = nullptr;
  if (httpd_req_async_handler_begin(req, &async) != ESP_OK) {
    operation_give(); error(req, "Schema-Auftrag konnte nicht gestartet werden.", "503 Service Unavailable"); return ESP_FAIL;
  }
  Job job{async, image, read};
  if (xQueueSend(jobs, &job, 0) != pdTRUE) {
    error(async, "Schema-Auftrag belegt.", "503 Service Unavailable");
    shutdown(httpd_req_to_sockfd(async), SHUT_RDWR);
    httpd_req_async_handler_complete(async);
    httpd_queue_work(server, wake_http, nullptr);
    operation_give();
  }
  return ESP_OK;
}
} // Namensraum
void schema_register(httpd_handle_t http, const char *token) {
  server = http; csrf = token;
  mutex = xSemaphoreCreateMutex();
  jobs = xQueueCreate(1, sizeof(Job));
  configASSERT(mutex && jobs);
  uint32_t size = 0;
  if (esp_flash_get_size(nullptr, &size) == ESP_OK && size >= region_address + region_size) {
    const esp_partition_t *found = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY, "pbx_schema");
    if (found && found->address == region_address && found->size == region_size) partition = found;
    else if (!found && esp_partition_register_external(nullptr, region_address, region_size, "pbx_schema",
             ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_UNDEFINED, &partition) != ESP_OK) partition = nullptr;
  }
  if (partition) {
    Flash flash;
    image_record = load(flash, 0, image_bank);
    layout_record = load(flash, 2 * image_bank, layout_bank);
    if (layout_record.bank >= 0 && layout_record.header.size <= layout_limit) {
      std::string saved(layout_record.header.size, '\0');
      if (flash.read(2 * image_bank + layout_record.bank * layout_bank + sector, saved.data(), saved.size())) layout = saved;
    }
  }
  for (const auto &path : {"/api/schema", "/api/schema/image"}) {
    httpd_uri_t route{};
    route.uri = path; route.method = HTTP_GET;
    route.handler = !strcmp(path, "/api/schema") ? status_handler : transfer_handler;
    ESP_ERROR_CHECK(httpd_register_uri_handler(server, &route));
    route.method = HTTP_POST; route.handler = transfer_handler;
    ESP_ERROR_CHECK(httpd_register_uri_handler(server, &route));
  }
  configASSERT(xTaskCreatePinnedToCore(worker, "schema_store", 12288, nullptr, 3, nullptr, 0) == pdPASS);
}

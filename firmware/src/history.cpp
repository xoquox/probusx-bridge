#include "history.h"
#include "history_store.h"
#include "live.h"
#include "mqtt_policy.h"
#include "capture_api.h"
#include "http_connections.h"
#include "web_logic.h"
#include "ota.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "nvs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "history_transfer.h"
#include "lwip/sockets.h"
#include <atomic>
#include <memory>
#include <string>
using namespace pbx_history;
namespace {
constexpr size_t reserve_bytes = 512 * 1024;
SemaphoreHandle_t mutex;
QueueHandle_t events;
std::atomic<uint32_t> queue_dropped{0};
Settings settings;
std::atomic<bool> flex_enabled{true};
bool capture_reserved = false;
Store store(heap_caps_free);
const char *csrf;
httpd_handle_t http_server;
struct Query {
  httpd_req_t *request;
  uint64_t from, to;
  size_t limit;
};
QueueHandle_t queries;
std::atomic<bool> query_busy{false};
std::atomic<uint32_t> query_failed{0}, query_bytes{0}, query_deadline_ms{0};
struct Lock {
  Lock() {
    xSemaphoreTake(mutex, portMAX_DELAY);
  }
  ~Lock() {
    xSemaphoreGive(mutex);
  }
};
void grow() {
  if (capture_reserved)
    return;
  while (store.blocks() < 128 &&
         heap_caps_get_free_size(MALLOC_CAP_SPIRAM) > reserve_bytes + chunk_bytes) {
    void *p = heap_caps_malloc(chunk_bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!p)
      break;
    store.add_block(p);
  }
}
Record make_record(uint64_t now_us, uint64_t main_at, uint64_t ext_at, const int16_t *temperatures,
                   const int16_t *outputs) {
  Record r{};
  r.ms = now_us / 1000;
  bool main = pbx_mqtt::fresh(now_us, main_at),
       ext = flex_enabled && pbx_mqtt::fresh(now_us, ext_at);
  for (unsigned i = 0; i < 14; ++i)
    r.values[i] = (i < 10 ? main : ext) && temperatures[i] != 2500 ? temperatures[i] : missing;
  for (unsigned i = 0; i < 15; ++i) {
    double v = pbx_mqtt::output(outputs[i], i);
    r.values[14 + i] = (i < 11 ? main : ext) && v >= 0 && v <= 100 ? outputs[i] : missing;
  }
  return r;
}
void worker(void *) {
  auto snapshot = std::make_unique<LiveSnapshot>();
  Record previous{};
  bool have = false;
  uint64_t last_periodic = 0, last_grow = 0, last_seen = 0;
  uint32_t drops = 0;
  auto consume = [&](Record r) {
    if (r.ms < last_seen)
      return;
    last_seen = r.ms;
    if (!settings.enabled)
      return;
    bool changed = !have || memcmp(r.values + 14, previous.values + 14, 15 * sizeof(int16_t));
    if (have)
      for (unsigned i = 0; i < 14; ++i)
        if ((r.values[i] == missing) != (previous.values[i] == missing))
          changed = true;
    bool periodic = !last_periodic || r.ms - last_periodic >= uint64_t(settings.interval_s) * 1000;
    if (changed || periodic) {
      r.flags = (periodic ? 1 : 0) | (changed ? 2 : 0);
      if (store.append(r)) {
        previous = r;
        have = true;
        if (periodic)
          last_periodic = r.ms;
      }
    }
  };
  for (;;) {
    live_snapshot(*snapshot); // Erst kopieren; die Verlaufssperre hält niemals die Live-Sperre.
    {
      Lock lock;
      if (!store.blocks() || snapshot->now_us - last_grow >= 5000000) {
        grow();
        last_grow = snapshot->now_us;
      }
      Record queued;
      while (xQueueReceive(events, &queued, 0) == pdTRUE)
        consume(queued);
      if (queue_dropped.load() != drops) {
        drops = queue_dropped;
        Record gap{};
        gap.ms = esp_timer_get_time() / 1000;
        for (auto &v : gap.values)
          v = missing;
        if (settings.enabled)
          store.append(gap);
        have = false;
        last_seen = gap.ms;
      }
      consume(make_record(snapshot->now_us, snapshot->main_at, snapshot->ext_at,
                          snapshot->temperatures, snapshot->outputs));
      if (!settings.enabled) {
        have = false;
        last_periodic = 0;
      }
    }
    vTaskDelay(pdMS_TO_TICKS(100));
  }
}
esp_err_t reply(httpd_req_t *r, cJSON *o, const char *status = "200 OK") {
  httpd_resp_set_status(r, status);
  httpd_resp_set_type(r, "application/json");
  httpd_resp_set_hdr(r, "Cache-Control", "no-store");
  char *s = cJSON_PrintUnformatted(o);
  cJSON_Delete(o);
  if (!s)
    return ESP_ERR_NO_MEM;
  auto e = httpd_resp_sendstr(r, s);
  cJSON_free(s);
  return e;
}
esp_err_t fail(httpd_req_t *r, const char *message, const char *status = "400 Bad Request") {
  auto *o = cJSON_CreateObject();
  cJSON_AddStringToObject(o, "error", message);
  return reply(r, o, status);
}
esp_err_t status_handler(httpd_req_t *r) {
  http_connections::touch(r);
  auto *o = cJSON_CreateObject();
  history_status(o);
  return reply(r, o);
}
esp_err_t configure(httpd_req_t *r) {
  http_connections::touch(r);
  char type[80]{};
  httpd_req_get_hdr_value_str(r, "Content-Type", type, sizeof(type));
  if (strncmp(type, "application/x-www-form-urlencoded", 33) || r->content_len < 1 ||
      r->content_len > 768)
    return fail(r, "Ungültige Formulardaten");
  std::string body(r->content_len, '\0');
  size_t got = 0;
  while (got < body.size()) {
    int n = httpd_req_recv(r, body.data() + got, body.size() - got);
    if (n <= 0)
      return ESP_FAIL;
    got += n;
  }
  web_logic::Form f;
  if (!web_logic::form(body, f) || f["token"] != csrf)
    return fail(r, "Token ungültig", "403 Forbidden");
  Settings next;
  {
    Lock lock;
    next = settings;
  }
  uint32_t interval;
  if (!pbx::option_number(f["interval_s"], interval) || interval < 1 || interval > 3600 ||
      (f["enabled"] != "0" && f["enabled"] != "1") || (f["flex1"] != "0" && f["flex1"] != "1") ||
      f["flex2"] == "1" || f["flex3"] == "1")
    return fail(r, "Intervall: 1–3600 Sekunden. FlexBox 2 und 3 sind noch nicht verfügbar.");
  next.interval_s = interval;
  next.enabled = f["enabled"] == "1";
  next.flex1 = f["flex1"] == "1";
  if (!ota_restart_allowed() || !operation_take())
    return fail(r, "Ein anderer Vorgang läuft", "409 Conflict");
  nvs_handle_t h;
  auto e = nvs_open("pbx_history", NVS_READWRITE, &h);
  if (e == ESP_OK) {
    e = nvs_set_blob(h, "settings", &next, sizeof(next));
    if (e == ESP_OK)
      e = nvs_commit(h);
    nvs_close(h);
  }
  if (e == ESP_OK) {
    Lock lock;
    if (settings.enabled && !next.enabled) {
      Record gap{};
      gap.ms = esp_timer_get_time() / 1000;
      for (auto &v : gap.values)
        v = missing;
      store.append(gap);
    }
    settings = next;
    flex_enabled = next.flex1;
  }
  operation_give();
  if (e != ESP_OK)
    return fail(r, "Speichern fehlgeschlagen", "500 Internal Server Error");
  auto *o = cJSON_CreateObject();
  cJSON_AddBoolToObject(o, "accepted", true);
  return reply(r, o);
}
bool integer(const char *s, uint64_t &n) {
  if (!s || !*s || strlen(s) > 15)
    return false;
  n = 0;
  for (; *s; ++s) {
    if (*s < '0' || *s > '9')
      return false;
    n = n * 10 + *s - '0';
  }
  return true;
}
// Netzwerkübertragung gehört nicht in den zentralen HTTP-Task.
void wake_http(void *) {}
int normal_send(httpd_handle_t, int fd, const char *data, size_t size, int flags) {
  const int n = send(fd, data, size, flags);
  // Nullfortschritt darf die interne HTTP-Sendeschleife nicht endlos wiederholen.
  return n > 0 || !size ? n : HTTPD_SOCK_ERR_FAIL;
}
int limited_send(httpd_handle_t server, int fd, const char *data, size_t size, int flags) {
  const uint32_t now_ms = uint64_t(esp_timer_get_time()) / 1000;
  if (pbx_history::deadline_reached(now_ms, query_deadline_ms.load()))
    return HTTPD_SOCK_ERR_TIMEOUT;
  return normal_send(server, fd, data, size, flags);
}
void finish_query(httpd_req_t *r, bool failed) {
  if (failed) {
    ++query_failed;
    shutdown(httpd_req_to_sockfd(r), SHUT_RDWR);
  }
  http_connections::protect_async(r, false);
  httpd_req_async_handler_complete(r);
  query_busy = false;
  httpd_queue_work(http_server, wake_http, nullptr);
}
void query_worker(void *) {
  Query query;
  for (;;) {
    xQueueReceive(queries, &query, portMAX_DELAY);
    auto *r = query.request;
    const int fd = httpd_req_to_sockfd(r);
    timeval timeout{1, 0};
    // Ein einzelner blockierter Sendeaufruf darf höchstens eine Sekunde warten.
    query_deadline_ms = uint64_t(esp_timer_get_time()) / 1000 + query_timeout_us / 1000;
    const bool socket_ok =
        setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout)) == 0 &&
        httpd_sess_set_send_override(http_server, fd, limited_send) == ESP_OK;
    size_t limit = std::min<size_t>(query.limit, pbx_history::max_query_buckets);
    Bucket *points = nullptr;
    while (socket_ok && limit >= 16) {
      points = static_cast<Bucket *>(
          heap_caps_malloc(limit * sizeof(Bucket), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
      if (points)
        break;
      limit /= 2;
    }
    bool ready = points && xSemaphoreTake(mutex, pdMS_TO_TICKS(200)) == pdTRUE;
    if (ready) {
      store.aggregate(query.from, query.to, points, limit);
      xSemaphoreGive(mutex);
    }
    bool good = false;
    if (ready) {
      Header header;
      header.version = 2;
      header.from = query.from;
      header.to = query.to;
      header.count = pbx_history::compact(points, limit);
      httpd_resp_set_type(r, "application/octet-stream");
      httpd_resp_set_hdr(r, "Cache-Control", "no-store");
      struct Sender {
        httpd_req_t *request;
        uint64_t now() const {
          return esp_timer_get_time();
        }
        bool send(const void *p, size_t n) {
          return httpd_resp_send_chunk(request, static_cast<const char *>(p), n) == ESP_OK;
        }
        void yield() {
          vTaskDelay(1);
        }
      } sender{r};
      good = pbx_history::send_response(sender, header, points);
      query_bytes = sizeof(Header) + header.count * sizeof(Bucket);
    } else if (socket_ok) {
      good = fail(r, "Verlauf momentan belegt oder Speicher knapp. Erneut versuchen.",
                  "503 Service Unavailable") == ESP_OK;
    }
    heap_caps_free(points);
    // Nach erfolgreicher Antwort gilt wieder der normale HTTP-Sendezeitraum.
    timeout.tv_sec = 5;
    if (httpd_sess_set_send_override(http_server, fd, normal_send) != ESP_OK ||
        setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout)) != 0)
      good = false;
    finish_query(r, !good);
  }
}
esp_err_t data_handler(httpd_req_t *r) {
  http_connections::touch(r);
  char query[160]{}, value[32];
  if (httpd_req_get_url_query_str(r, query, sizeof(query)) != ESP_OK)
    return fail(r, "Zeitraum fehlt");
  uint64_t from, to, limit = pbx_history::max_query_buckets;
  if (httpd_query_key_value(query, "from", value, sizeof(value)) != ESP_OK ||
      !integer(value, from) || httpd_query_key_value(query, "to", value, sizeof(value)) != ESP_OK ||
      !integer(value, to) || from >= to || to > uint64_t(esp_timer_get_time() / 1000) + 60000)
    return fail(r, "Zeitraum ungültig");
  if (httpd_query_key_value(query, "limit", value, sizeof(value)) == ESP_OK &&
      (!integer(value, limit) || limit < 16 || limit > 1024))
    return fail(r, "Auflösung ungültig");
  if (query_busy.exchange(true))
    return fail(r, "Ein Verlaufsabruf läuft bereits. Erneut versuchen.", "429 Too Many Requests");
  httpd_req_t *async = nullptr;
  if (httpd_req_async_handler_begin(r, &async) != ESP_OK) {
    query_busy = false;
    return fail(r, "Verlaufsabruf konnte nicht gestartet werden", "503 Service Unavailable");
  }
  http_connections::protect_async(r, true);
  Query job{async, from, to, size_t(limit)};
  if (xQueueSend(queries, &job, 0) != pdTRUE) {
    fail(async, "Verlaufsabruf belegt", "503 Service Unavailable");
    finish_query(async, false);
  }
  return ESP_OK;
}
} // Ende interner Bereich
bool history_flex1_enabled() {
  return flex_enabled;
}
void history_init() {
  mutex = xSemaphoreCreateMutex();
  events = xQueueCreate(64, sizeof(Record));
  configASSERT(mutex && events);
  nvs_handle_t h;
  if (nvs_open("pbx_history", NVS_READONLY, &h) == ESP_OK) {
    Settings s;
    size_t n = sizeof(s);
    auto e = nvs_get_blob(h, "settings", &s, &n);
    nvs_close(h);
    if (e == ESP_OK && n == sizeof(s) && s.valid())
      settings = s;
  }
  flex_enabled = settings.flex1;
  // Erst nach Start von WLAN/MQTT wachsen. Deren Grundbedarf bleibt damit frei.
}
void history_register(httpd_handle_t server, const char *token) {
  csrf = token;
  http_server = server;
  queries = xQueueCreate(1, sizeof(Query));
  configASSERT(queries);
  configASSERT(xTaskCreatePinnedToCore(query_worker, "history_query", 8192, nullptr, 2, nullptr,
                                       0) == pdPASS);
  for (const char *path : {"/api/history/status", "/api/history/data", "/api/history/settings"}) {
    httpd_uri_t u{};
    u.uri = path;
    u.method = !strcmp(path, "/api/history/settings") ? HTTP_POST : HTTP_GET;
    u.handler = !strcmp(path, "/api/history/status") ? status_handler
                : !strcmp(path, "/api/history/data") ? data_handler
                                                     : configure;
    ESP_ERROR_CHECK(httpd_register_uri_handler(server, &u));
  }
  configASSERT(xTaskCreatePinnedToCore(worker, "probusx_history", 8192, nullptr, 3, nullptr, 0) ==
               pdPASS);
}
void history_status(cJSON *root) {
  if (!mutex)
    return;
  Lock lock;
  auto *o = cJSON_AddObjectToObject(root, "history");
  cJSON_AddBoolToObject(o, "enabled", settings.enabled);
  cJSON_AddBoolToObject(o, "flex1", settings.flex1);
  cJSON_AddBoolToObject(o, "flex2", false);
  cJSON_AddBoolToObject(o, "flex3", false);
  cJSON_AddNumberToObject(o, "interval_s", settings.interval_s);
  cJSON_AddNumberToObject(o, "now_ms", esp_timer_get_time() / 1000);
  cJSON_AddNumberToObject(o, "oldest_ms", store.oldest());
  cJSON_AddNumberToObject(o, "newest_ms", store.newest());
  cJSON_AddNumberToObject(o, "samples", store.count());
  cJSON_AddNumberToObject(o, "capacity_bytes", store.bytes());
  cJSON_AddNumberToObject(o, "used_bytes", store.count() * sizeof(Record));
  cJSON_AddNumberToObject(o, "removed_samples", store.removed());
  cJSON_AddNumberToObject(o, "dropped_events", queue_dropped.load());
  cJSON_AddBoolToObject(o, "query_busy", query_busy);
  cJSON_AddNumberToObject(o, "query_failed", query_failed.load());
  cJSON_AddNumberToObject(o, "query_bytes", query_bytes.load());
  cJSON_AddNumberToObject(o, "largest_psram_block",
                          heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM));
  cJSON_AddNumberToObject(o, "free_psram", heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
  cJSON_AddBoolToObject(o, "capture_reserved", capture_reserved);
  cJSON_AddBoolToObject(o, "persistent", false);
}
void *history_capture_allocate(size_t bytes) {
  Lock lock;
  capture_reserved = true;
  void *p = nullptr;
  for (;;) {
    if (heap_caps_get_free_size(MALLOC_CAP_SPIRAM) >= bytes + reserve_bytes)
      p = heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (p || !store.release_one())
      break;
  }
  if (!p)
    capture_reserved = false;
  return p;
}
void history_capture_release(void *p) {
  Lock lock;
  heap_caps_free(p);
  capture_reserved = false;
  grow();
}

void history_offer(uint64_t now_us, uint64_t main_at, uint64_t ext_at, const int16_t *temperatures,
                   const int16_t *outputs) {
  if (!events)
    return;
  auto record = make_record(now_us, main_at, ext_at, temperatures, outputs);
  if (xQueueSend(events, &record, 0) != pdTRUE)
    ++queue_dropped;
}

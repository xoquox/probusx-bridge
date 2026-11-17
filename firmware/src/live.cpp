#include "history.h"
#include "http_connections.h"
#include "live.h"
#include "protocol.h"
#include "telemetry.h"
#include "capture_api.h"
#include "settings.h"
#include "nvs.h"
#include "runtime_options.h"
#include "web_logic.h"
#include "cJSON.h"
#include "driver/uart.h"
#include "driver/gpio.h"
#include "esp_timer.h"
#include "esp_mac.h"
#include "esp_wifi.h"
#include "esp_system.h"
#include "esp_heap_caps.h"
#include "esp_app_desc.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <cstring>
#include <new>
#include <memory>
#include <string>
static SemaphoreHandle_t mutex;
static pbx::Parser parser;
static pbx::Keys keys;
static const char *token;
static bool enabled = true, reserved = false;
static pbx::RuntimeOptions options;
static uint64_t main_at = 0, ext_at = 0, display_at = 0, poll_at = 0, last_tx_at = 0,
                key_queued_at = 0;
static uint8_t last_tx_code = 0;
static int16_t temperatures[14]{}, outputs[15]{};
static uint8_t display[5120]{};
static uint32_t main_count = 0, ext_count = 0, display_count = 0, tx_count = 0, errors = 0;
static char device[18];
static uint64_t now() {
  return esp_timer_get_time();
}
// Schützt Parser, Messwerte und Tastenzustand gegen gleichzeitige HTTP-Zugriffe.
struct Lock {
  Lock() {
    xSemaphoreTake(mutex, portMAX_DELAY);
  }
  ~Lock() {
    xSemaphoreGive(mutex);
  }
};
// Speichert einen versionierten Datensatz. Erst nach erfolgreichem Commit übernehmen.
static esp_err_t save_options(const pbx::RuntimeOptions &value) {
  nvs_handle_t h;
  auto err = nvs_open("pbx_options", NVS_READWRITE, &h);
  if (err != ESP_OK)
    return err;
  err = nvs_set_blob(h, "options", &value, sizeof(value));
  if (err == ESP_OK)
    err = nvs_commit(h);
  nvs_close(h);
  return err;
}
static void load_options() {
  nvs_handle_t h;
  if (nvs_open("pbx_options", NVS_READONLY, &h) == ESP_OK) {
    pbx::RuntimeOptions value;
    size_t n = sizeof(value);
    auto e = nvs_get_blob(h, "options", &value, &n);
    nvs_close(h);
    if (e == ESP_OK && n == sizeof(value) && value.valid())
      options = value;
  }
  enabled = options.transmit;
  parser.quality.enabled = options.analysis;
}
static void add_options(cJSON *root) {
  auto *o = cJSON_AddObjectToObject(root, "settings");
  cJSON_AddNumberToObject(o, "display_ms", options.display_ms);
  cJSON_AddNumberToObject(o, "temperature_ms", options.temperature_ms);
  cJSON_AddNumberToObject(o, "output_ms", options.output_ms);
  cJSON_AddBoolToObject(o, "analysis_enabled", options.analysis);
  cJSON_AddBoolToObject(o, "transmit_enabled", options.transmit);
}
void live_init() {
  load_options();
  mutex = xSemaphoreCreateMutex();
  configASSERT(mutex);
  uint8_t m[6];
  esp_read_mac(m, ESP_MAC_WIFI_STA);
  snprintf(device, sizeof(device), "%02x:%02x:%02x:%02x:%02x:%02x", m[0], m[1], m[2], m[3], m[4],
           m[5]);
}
void live_reset(bool error) {
  Lock lock;
  parser.reset();
  if (error) {
    main_at = ext_at = display_at = poll_at = 0;
    ++errors;
  } else
    parser.quality.event(now(), pbx::CAPTURE_RESET);
}
void live_uart_fault(unsigned kind) {
  Lock lock;
  if (kind > 5)
    return;
  if (parser.quality.enabled)
    ++parser.quality.uart[kind];
  parser.quality.event(now(), pbx::EventCode(pbx::UART_FIFO + kind));
  if (kind < 5) {
    parser.reset();
    main_at = ext_at = display_at = poll_at = 0;
    ++errors;
  }
}
void live_uart_buffer(size_t bytes) {
  Lock lock;
  if (parser.quality.enabled)
    parser.quality.buffered_high = std::max(parser.quality.buffered_high, uint32_t(bytes));
}
static void release_operation() {
  if (reserved && !keys.busy()) {
    reserved = false;
    operation_give();
  }
}
void live_tick() {
  Lock lock;
  uint64_t t = now();
  parser.tick(t);
  if (keys.expire(t)) {
    if (parser.quality.enabled)
      ++parser.quality.key_expired;
    parser.quality.event(t, pbx::KEY_EXPIRED);
  }
  release_operation();
}
// Übernimmt nur vom Parser angenommene Telegramme. Beschädigte CRC-Frames kommen hier nicht an.
// Eine Tastenantwort benötigt eine passende Abfrage und einen leeren UART-Empfangspuffer.
void live_feed(const uint8_t *p, size_t n, bool transmit) {
  Lock lock;
  parser.feed(
      p, n,
      [transmit](const uint8_t *f, size_t len, bool last) {
        uint64_t t = now();
        if (f[1] == 0xb3) {
          if (parser.quality.enabled)
            ++parser.quality.foreign_keys;
          parser.quality.event(t, pbx::FOREIGN_KEY, pbx::KEY, f[2]);
          return;
        }
        if (f[1] != 0x13)
          return;
        if (f[2] == 0x36 && f[4] == 0x61 && len == 241) {
          pbx::main_values(f, temperatures, outputs);
          main_at = t;
          ++main_count;
          history_offer(t, main_at, pbx::flex1_at(ext_at, main_at), temperatures, outputs);
        } else if (f[2] == 0x40 && f[4] == 0xf0) {
          pbx::flex1_temperatures(f, temperatures);
          ext_at = t;
          ++ext_count;
          history_offer(t, main_at, pbx::flex1_at(ext_at, main_at), temperatures, outputs);
        } else if (f[2] == 0x36 && f[4] == 0x55) {
          memcpy(display, f + 5, 5120);
          display_at = t;
          ++display_count;
        } else if (f[2] == 0x36 && f[4] == 0x34 && f[5] == 0x50 && f[6] == 0xc3) {
          poll_at = t;
          size_t buffered = 0;
          uart_get_buffered_data_len(UART_NUM_1, &buffered);
          if (reserved && (!last || buffered))
            if (parser.quality.enabled)
              ++parser.quality.skipped_poll_busy;
          if (!transmit || !enabled || !last || buffered || !reserved)
            return;
          uint8_t code;
          if (keys.poll(t, code)) {
            uint8_t reply[] = {0xa5, 0xb3, code, 0, 0};
            uint16_t c = pbx::crc(reply, 3);
            reply[3] = c >> 8;
            reply[4] = c;
            gpio_set_level(gpio_num_t(BUS_ENABLE_GPIO), 1);
            int sent = uart_write_bytes(UART_NUM_1, reply, 5);
            esp_err_t result = uart_wait_tx_done(UART_NUM_1, pdMS_TO_TICKS(5));
            gpio_set_level(gpio_num_t(BUS_ENABLE_GPIO), 0);
            if (sent == 5 && result == ESP_OK) {
              ++tx_count;
              last_tx_at = now();
              last_tx_code = code;
            } else {
              if (parser.quality.enabled)
                ++parser.quality.tx_errors;
              parser.quality.event(t, pbx::TX_ERROR);
              enabled = false;
              keys = pbx::Keys{};
            }
          }
          release_operation();
        }
      },
      now());
}
static esp_err_t json(httpd_req_t *r, cJSON *o) {
  char *s = cJSON_PrintUnformatted(o);
  cJSON_Delete(o);
  if (!s)
    return ESP_FAIL;
  httpd_resp_set_type(r, "application/json");
  httpd_resp_set_hdr(r, "Cache-Control", "no-store");
  auto e = httpd_resp_send(r, s, HTTPD_RESP_USE_STRLEN);
  cJSON_free(s);
  return e;
}
static esp_err_t fail(httpd_req_t *r, const char *s, const char *status = "409 Conflict") {
  httpd_resp_set_status(r, status);
  cJSON *o = cJSON_CreateObject();
  cJSON_AddStringToObject(o, "error", s);
  return json(r, o);
}
static double age(uint64_t t, uint64_t stamp) {
  return stamp ? double(t - stamp) / 1000 : -1;
}
static esp_err_t state_handler(httpd_req_t *r) {
  http_connections::touch(r);
  cJSON *o = cJSON_CreateObject();
  {
    Lock lock;
    uint64_t t = now();
    add_options(o);
    cJSON_AddNumberToObject(o, "schema", 1);
    cJSON_AddStringToObject(o, "device_id", device);
    cJSON_AddStringToObject(o, "version", esp_app_get_description()->version);
    cJSON_AddStringToObject(o, "token", token);
    cJSON_AddBoolToObject(o, "tx_enabled", enabled);
    cJSON_AddBoolToObject(o, "busy", operation_busy());
    cJSON_AddBoolToObject(o, "key_pending", keys.busy());
    cJSON_AddNumberToObject(o, "main_age_ms", age(t, main_at));
    cJSON_AddNumberToObject(o, "ext_age_ms", age(t, history_flex1_enabled()?pbx::flex1_at(ext_at, main_at):0));
    cJSON_AddNumberToObject(o, "ext_temperatures_age_ms", age(t, ext_at));
    cJSON_AddNumberToObject(o, "ext_outputs_age_ms", age(t, main_at));
    cJSON_AddBoolToObject(o, "flex1_enabled", history_flex1_enabled());
    cJSON_AddNumberToObject(o, "display_age_ms", age(t, display_at));
    cJSON_AddNumberToObject(o, "poll_age_ms", age(t, poll_at));
    cJSON_AddNumberToObject(o, "main_frames", main_count);
    cJSON_AddNumberToObject(o, "ext_frames", ext_count);
    cJSON_AddNumberToObject(o, "display_frames", display_count);
    cJSON_AddNumberToObject(o, "tx_frames", tx_count);
    cJSON_AddNumberToObject(o, "uart_errors", errors);
    cJSON *d = cJSON_AddObjectToObject(o, "diagnostics");
    auto &q = parser.quality;
    cJSON_AddBoolToObject(d, "enabled", q.enabled);
    cJSON_AddNumberToObject(d, "crc_ok", q.crc_ok);
    cJSON_AddNumberToObject(d, "crc_bad", q.crc_bad);
    cJSON_AddNumberToObject(d, "unchecked", q.unchecked);
    cJSON_AddNumberToObject(d, "rx_bytes", q.bytes);
    cJSON_AddNumberToObject(d, "header_errors", q.headers_bad);
    cJSON_AddNumberToObject(d, "partial_timeouts", q.partial_timeouts);
    cJSON_AddNumberToObject(d, "rx_age_ms", age(t, q.rx_last_us));
    cJSON_AddNumberToObject(d, "uptime_s", t / 1000000);
    uint64_t wb, wo, we, wu;
    q.window(t, wb, wo, we, wu);
    cJSON_AddNumberToObject(d, "crc_bad_60s", we);
    cJSON_AddNumberToObject(d, "crc_checked_60s", wo + we);
    cJSON_AddNumberToObject(d, "crc_error_percent_60s", wo + we ? 100.0 * we / (wo + we) : -1);
    cJSON_AddNumberToObject(d, "rx_bytes_per_s",
                            wb / std::max(1.0, std::min(60.0, double(t - q.started_us) / 1000000)));
    cJSON_AddNumberToObject(d, "uart_errors",
                            q.uart[0] + q.uart[1] + q.uart[2] + q.uart[3] + q.uart[4]);
    cJSON_AddNumberToObject(d, "main_gaps", q.families[pbx::MAIN].gaps);
    cJSON_AddNumberToObject(d, "display_gaps", q.families[pbx::DISPLAY].gaps);
    cJSON_AddBoolToObject(d, "main_crc_verified", true);
    cJSON *ts = cJSON_AddArrayToObject(o, "temperatures_raw"),
          *os = cJSON_AddArrayToObject(o, "outputs_raw");
    for (auto v : temperatures) {
      cJSON_AddItemToArray(ts, cJSON_CreateNumber(v));
    }
    for (auto v : outputs) {
      cJSON_AddItemToArray(os, cJSON_CreateNumber(v));
    }
    // Drei Modulplätze sind vorgesehen. Nur die Buskennung von F1 ist durch einen Mitschnitt
    // belegt.
    cJSON *modules = cJSON_AddArrayToObject(o, "extensions");
    for (unsigned slot = 1; slot <= 3; slot++) {
      auto *m = cJSON_CreateObject();
      cJSON_AddNumberToObject(m, "slot", slot);
      cJSON_AddBoolToObject(m, "supported", slot == 1);
      cJSON_AddBoolToObject(m, "enabled", slot == 1 && history_flex1_enabled());
      cJSON_AddBoolToObject(m, "seen", slot == 1 && ext_at);
      cJSON_AddNumberToObject(m, "age_ms", slot == 1 ? age(t, pbx::flex1_at(ext_at, main_at)) : -1);
      cJSON_AddNumberToObject(m, "frames", slot == 1 ? ext_count : 0);
      cJSON_AddStringToObject(m, "mapping",
                              slot == 1 ? "temperatures_r1_r3_observed_r2_he_provisional"
                                        : "not_verified");
      cJSON_AddItemToArray(modules, m);
    }
  }
  return json(r, o);
}
// Kopiert ein frisches Bild unter der Sperre. Die HTTP-Übertragung läuft danach ohne Sperre.
static esp_err_t display_handler(httpd_req_t *r) {
  http_connections::touch(r);
  auto *copy = new (std::nothrow) uint8_t[5120];
  if (!copy)
    return fail(r, "Speicher knapp");
  bool fresh;
  {
    Lock lock;
    fresh = display_at && now() - display_at < 3000000;
    memcpy(copy, display, 5120);
  }
  if (!fresh) {
    delete[] copy;
    return fail(r, "Kein aktuelles Display", "503 Service Unavailable");
  }
  httpd_resp_set_type(r, "application/octet-stream");
  httpd_resp_set_hdr(r, "Cache-Control", "no-store");
  auto e = httpd_resp_send(r, reinterpret_cast<char *>(copy), 5120);
  delete[] copy;
  return e;
}
// Erstellt einen kurzen Schnappschuss unter der Sperre. JSON wird außerhalb der Sperre aufgebaut.
static esp_err_t diagnostics_handler(httpd_req_t *r) {
  http_connections::touch(r);
  auto *snapshot = new (std::nothrow) pbx::Quality;
  if (!snapshot)
    return fail(r, "Speicher knapp", "503 Service Unavailable");
  uint64_t t, tx_at, queued_at;
  uint32_t sent;
  uint8_t code;
  bool tx, pending;
  {
    Lock lock;
    *snapshot = parser.quality;
    t = now();
    tx = enabled;
    pending = keys.busy();
    sent = tx_count;
    tx_at = last_tx_at;
    queued_at = key_queued_at;
    code = last_tx_code;
  }
  const auto &q = *snapshot;
  cJSON *o = cJSON_CreateObject();
  cJSON_AddBoolToObject(o, "analysis_enabled", q.enabled);
  cJSON_AddStringToObject(o, "device_id", device);
  cJSON_AddStringToObject(o, "version", esp_app_get_description()->version);
  cJSON_AddNumberToObject(o, "uptime_s", double(t) / 1000000);
  cJSON_AddNumberToObject(o, "reset_reason", esp_reset_reason());
  cJSON_AddNumberToObject(o, "baud", BUS_BAUD);
  cJSON_AddNumberToObject(o, "free_heap",
                          heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
  cJSON_AddNumberToObject(o, "minimum_free_heap",
                          heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
  wifi_ap_record_t ap{};
  bool connected = esp_wifi_sta_get_ap_info(&ap) == ESP_OK;
  cJSON_AddBoolToObject(o, "wifi_connected", connected);
  if (connected)
    cJSON_AddNumberToObject(o, "wifi_rssi_dbm", ap.rssi);
  cJSON_AddBoolToObject(o, "tx_enabled", tx);
  cJSON_AddBoolToObject(o, "key_pending", pending);
  cJSON_AddNumberToObject(o, "tx_frames", sent);
  cJSON_AddNumberToObject(o, "last_tx_age_ms", age(t, tx_at));
  cJSON_AddNumberToObject(o, "last_tx_code", code);
  cJSON_AddNumberToObject(o, "key_pending_age_ms", pending ? age(t, queued_at) : -1);
  cJSON_AddNumberToObject(o, "rx_bytes", q.bytes);
  cJSON_AddNumberToObject(o, "rx_age_ms", age(t, q.rx_last_us));
  cJSON_AddNumberToObject(o, "rx_max_read_gap_ms", double(q.rx_max_gap_us) / 1000);
  cJSON_AddNumberToObject(o, "crc_ok", q.crc_ok);
  cJSON_AddNumberToObject(o, "crc_bad", q.crc_bad);
  cJSON_AddNumberToObject(o, "unchecked", q.unchecked);
  cJSON_AddNumberToObject(o, "trailer_errors", q.trailer_errors);
  cJSON_AddNumberToObject(o, "header_errors", q.headers_bad);
  cJSON_AddNumberToObject(o, "partial_timeouts", q.partial_timeouts);
  cJSON_AddNumberToObject(o, "skipped_bytes", q.skipped_bytes);
  cJSON_AddNumberToObject(o, "padding_bytes", q.padding_bytes);
  cJSON_AddNumberToObject(o, "uart_buffer_high_water", q.buffered_high);
  cJSON_AddNumberToObject(o, "uart_buffer_capacity", 32768);
  const char *names[] = {"fifo_overflows", "buffer_full", "frame_errors",
                         "parity_errors",  "breaks",      "event_queue_pressure"};
  cJSON *u = cJSON_AddObjectToObject(o, "uart");
  for (int i = 0; i < 6; i++)
    cJSON_AddNumberToObject(u, names[i], q.uart[i]);
  cJSON_AddNumberToObject(o, "tx_errors", q.tx_errors);
  cJSON_AddNumberToObject(o, "key_expired", q.key_expired);
  cJSON_AddNumberToObject(o, "foreign_key_frames", q.foreign_keys);
  cJSON_AddNumberToObject(o, "skipped_poll_busy", q.skipped_poll_busy);
  uint64_t bytes, ok, bad, unverified;
  q.window(t, bytes, ok, bad, unverified);
  cJSON *window = cJSON_AddObjectToObject(o, "window_60s");
  cJSON_AddNumberToObject(window, "bytes_per_s",
                          bytes /
                              std::max(1.0, std::min(60.0, double(t - q.started_us) / 1000000)));
  cJSON_AddNumberToObject(window, "crc_ok", ok);
  cJSON_AddNumberToObject(window, "crc_bad", bad);
  cJSON_AddNumberToObject(window, "unchecked", unverified);
  if (ok + bad)
    cJSON_AddNumberToObject(window, "crc_error_percent", 100.0 * bad / (ok + bad));
  else
    cJSON_AddNullToObject(window, "crc_error_percent");
  cJSON *families = cJSON_AddObjectToObject(o, "families");
  for (int i = 0; i < pbx::FAMILIES; i++) {
    const auto &f = q.families[i];
    cJSON *j = cJSON_AddObjectToObject(families, pbx::family_name(i));
    cJSON_AddNumberToObject(j, "valid", f.valid);
    cJSON_AddNumberToObject(j, "crc_ok", f.crc_ok);
    cJSON_AddNumberToObject(j, "crc_bad", f.crc_bad);
    cJSON_AddNumberToObject(j, "unchecked", f.unchecked);
    cJSON_AddNumberToObject(j, "age_ms", age(t, f.last_us));
    cJSON_AddNumberToObject(j, "max_gap_ms", double(f.max_gap_us) / 1000);
    cJSON_AddNumberToObject(j, "gaps", f.gaps);
    cJSON_AddBoolToObject(j, "gap_open", f.gap_open);
  }
  cJSON_AddNumberToObject(o, "events_total", q.events_total);
  cJSON *events = cJSON_AddArrayToObject(o, "events");
  for (uint32_t i = q.events_total > 64 ? q.events_total - 64 : 0; i < q.events_total; i++) {
    const auto &e = q.events[i % 64];
    cJSON *v = cJSON_CreateObject();
    cJSON_AddNumberToObject(v, "uptime_s", double(e.us) / 1000000);
    cJSON_AddNumberToObject(v, "code", e.code);
    cJSON_AddStringToObject(v, "family", pbx::family_name(e.family));
    cJSON_AddNumberToObject(v, "value", e.value);
    cJSON_AddItemToArray(events, v);
  }
  if (q.last_bad.us) {
    cJSON *b = cJSON_AddObjectToObject(o, "last_bad_crc");
    cJSON_AddNumberToObject(b, "uptime_s", double(q.last_bad.us) / 1000000);
    cJSON_AddNumberToObject(b, "expected", q.last_bad.expected);
    cJSON_AddNumberToObject(b, "received", q.last_bad.received);
    cJSON_AddNumberToObject(b, "length", q.last_bad.length);
    char hex[37]{};
    for (unsigned i = 0; i < std::min(unsigned(12), unsigned(q.last_bad.length)); i++)
      snprintf(hex + i * 3, 4, "%02x ", q.last_bad.head[i]);
    cJSON_AddStringToObject(b, "head_hex", hex);
  }
  delete snapshot;
  return json(r, o);
}
// Änderungen erst prüfen, dann speichern und zuletzt gemeinsam im RAM übernehmen.
static const char *change_options(const char *path, web_logic::Form &f) {
  if (!operation_take())
    return "Vorgang läuft";
  struct Reservation {
    ~Reservation() {
      operation_give();
    }
  } reservation;
  pbx::RuntimeOptions previous;
  {
    Lock lock;
    previous = options;
  }
  auto next = previous;
  if (!strcmp(path, "/api/transmit")) {
    if (f["enable"] != "0" && f["enable"] != "1")
      return "Ungültige Freigabe";
    next.transmit = f["enable"] == "1";
  } else {
    if (!pbx::option_number(f["display_ms"], next.display_ms) ||
        !pbx::option_number(f["temperature_ms"], next.temperature_ms) ||
        !pbx::option_number(f["output_ms"], next.output_ms))
      return "Ungültiges Intervall";
    if ((f["analysis_enabled"] != "0" && f["analysis_enabled"] != "1") ||
        (f["transmit_enabled"] != "0" && f["transmit_enabled"] != "1"))
      return "Ungültige Option";
    next.analysis = f["analysis_enabled"] == "1";
    next.transmit = f["transmit_enabled"] == "1";
  }
  if (!next.valid())
    return "Display: 100–5000 ms; Messwerte: 250–60000 ms";
  std::unique_ptr<pbx::Quality> cleared;
  if (next.analysis != previous.analysis) {
    cleared.reset(new (std::nothrow) pbx::Quality);
    if (!cleared)
      return "Speicher knapp";
  }
  // NVS außerhalb der Empfangssperre schreiben. Die Reservierung sperrt Aufnahme, Tasten und OTA.
  if (memcmp(&next, &previous, sizeof(next)) && save_options(next) != ESP_OK)
    return "Speichern fehlgeschlagen";
  Lock lock;
  if (cleared) {
    parser.quality = *cleared;
    parser.quality.enabled = next.analysis;
  }
  options = next;
  enabled = options.transmit;
  return nullptr;
}
// Prüft Format und Sitzungstoken. HTTP-Erfolg bestätigt nur die Annahme des Auftrags.
static esp_err_t command_handler(httpd_req_t *r) {
  http_connections::touch(r);
  if (r->content_len < 1 || r->content_len > 256)
    return fail(r, "Ungültige Anfrage", "400 Bad Request");
  char type[80]{};
  httpd_req_get_hdr_value_str(r, "Content-Type", type, sizeof(type));
  if (strncmp(type, "application/x-www-form-urlencoded", 33) != 0)
    return fail(r, "Formulardaten erwartet", "415 Unsupported Media Type");
  std::string body(r->content_len, '\0');
  size_t got = 0;
  while (got < body.size()) {
    int n = httpd_req_recv(r, body.data() + got, body.size() - got);
    if (n <= 0)
      return ESP_FAIL;
    got += n;
  }
  web_logic::Form f;
  if (!web_logic::form(body, f) || f["token"] != token)
    return fail(r, "Token ungültig", "403 Forbidden");
  const char *failure;
  if (!strcmp(r->uri, "/api/transmit") || !strcmp(r->uri, "/api/settings"))
    failure = change_options(r->uri, f);
  else
    failure = live_queue_key(f["key"].c_str());
  if (failure)
    return fail(r, failure);
  cJSON *o = cJSON_CreateObject();
  cJSON_AddBoolToObject(o, "accepted", true);
  return json(r, o);
}
void live_register(httpd_handle_t s, const char *csrf) {
  token = csrf;
  httpd_uri_t diagnostic{};
  diagnostic.uri = "/api/diagnostics";
  diagnostic.method = HTTP_GET;
  diagnostic.handler = diagnostics_handler;
  ESP_ERROR_CHECK(httpd_register_uri_handler(s, &diagnostic));
  for (const auto &path :
       {"/api/live", "/api/display", "/api/key", "/api/transmit", "/api/settings"}) {
    httpd_uri_t u{};
    u.uri = path;
    u.method = (!strcmp(path, "/api/live") || !strcmp(path, "/api/display")) ? HTTP_GET : HTTP_POST;
    u.handler = !strcmp(path, "/api/live")      ? state_handler
                : !strcmp(path, "/api/display") ? display_handler
                                                : command_handler;
    ESP_ERROR_CHECK(httpd_register_uri_handler(s, &u));
  }
}

void live_snapshot(LiveSnapshot &s) {
  Lock lock;
  s.now_us = now();
  s.main_at = main_at;
  s.ext_at = history_flex1_enabled()?pbx::flex1_at(ext_at, main_at):0;
  s.display_at = display_at;
  s.poll_at = poll_at;
  s.main_count = main_count;
  s.ext_count = ext_count;
  s.display_count = display_count;
  s.tx_count = tx_count;
  memcpy(s.temperatures, temperatures, sizeof(temperatures));
  memcpy(s.outputs, outputs, sizeof(outputs));
  memcpy(s.display, display, sizeof(display));
  s.options = options;
  s.enabled = enabled;
  s.busy = operation_busy();
  s.pending = keys.busy();
  const auto &q = parser.quality;
  s.analysis = q.enabled;
  s.crc_ok = q.crc_ok;
  s.crc_bad = q.crc_bad;
  s.uart_errors = 0;
  for (int i = 0; i < 5; ++i)
    s.uart_errors += q.uart[i];
  s.tx_errors = q.tx_errors;
  s.key_expired = q.key_expired;
}
const char *live_queue_key(const char *name) {
  Lock lock;
  if (!enabled)
    return "Senden ist nicht freigegeben";
  if (!poll_at || now() - poll_at >= 2000000)
    return "Keine aktuellen Regler-Abfragen";
  const char *names[] = {"up", "right", "down", "left", "special"};
  const uint8_t codes[] = {0x10, 0x20, 0x40, 0x80, 0x70};
  uint8_t key = 0;
  for (int i = 0; i < 5; ++i)
    if (!strcmp(name, names[i]))
      key = codes[i];
  if (!key)
    return "Unbekannte Taste";
  if (keys.busy() || !operation_take())
    return "Vorgang läuft";
  reserved = true;
  key_queued_at = now();
  keys.queue(key, key_queued_at);
  return nullptr;
}

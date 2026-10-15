#include "capture_api.h"
#include "live.h"
#include "ota.h"
#include "esp_app_desc.h"
#include "settings.h"
#include "web_logic.h"
#include "web_assets.h"
#include <atomic>
#include <cstdio>
#include <cstring>
#include <string>
#include "cJSON.h"
#include "esp_event.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "nvs.h"
#include "lwip/sockets.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

static const char *TAG = "web_wifi";
static httpd_handle_t server;
static esp_netif_t *sta_netif;
static std::atomic<bool> ip_ready{false}, linked{false};
static std::atomic<unsigned> disconnect_reason{0};
static SemaphoreHandle_t model_mutex;
static QueueHandle_t wifi_commands, downloads;
static char token[33];
struct StoredWifi {
  uint32_t version;
  char ssid[33];
  char password[65];
};
struct Command {
  enum Kind { SAVE, FORGET, SCAN } kind;
  StoredWifi wifi;
};
struct WifiModel {
  char ssid[33] = {}, ip[16] = {},
       message[160] = "WLAN einrichten oder direkt im Zugangspunkt aufnehmen.";
  bool connected = false, ap = true, scanning = false;
  unsigned ap_seconds = 0, ap_clients = 0, reason = 0;
  uint16_t found = 0;
  struct Network {
    char ssid[33];
    int rssi;
    bool open;
  } networks[32]{};
};
static WifiModel model;
struct Download {
  httpd_req_t *req;
  uint32_t duration_ms;
};
static uint64_t milliseconds() {
  return esp_timer_get_time() / 1000;
}
static void message(const char *text) {
  xSemaphoreTake(model_mutex, portMAX_DELAY);
  strlcpy(model.message, text, sizeof(model.message));
  xSemaphoreGive(model_mutex);
}
static void wifi_event(void *, esp_event_base_t base, int32_t id, void *data) {
  if (base == WIFI_EVENT && id == WIFI_EVENT_STA_CONNECTED)
    linked = true;
  if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
    linked = false;
    ip_ready = false;
    disconnect_reason = static_cast<wifi_event_sta_disconnected_t *>(data)->reason;
  }
  if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
    ip_ready = true;
    disconnect_reason = 0;
  }
  if (base == IP_EVENT && id == IP_EVENT_STA_LOST_IP)
    ip_ready = false;
}
void wifi_prepare_capture() {
  // Die Vorgangsreservierung verhindert einen weiteren WLAN-Verbindungsversuch.
  // Einen laufenden Verbindungsaufbau beenden. So bleibt der Funkkanal der Aufnahme gleich.
  if (!ip_ready) {
    esp_wifi_disconnect();
    linked = false;
  }
}
// Schreibt Zugangsdaten in NVS. Das Passwort nicht in Meldungen oder Antworten ausgeben.
static esp_err_t store_wifi(const StoredWifi &wifi, bool forget) {
  nvs_handle_t handle;
  esp_err_t err = nvs_open("pbx_wifi", NVS_READWRITE, &handle);
  if (err != ESP_OK)
    return err;
  err = forget ? nvs_erase_key(handle, "credentials")
               : nvs_set_blob(handle, "credentials", &wifi, sizeof(wifi));
  if (forget && err == ESP_ERR_NVS_NOT_FOUND)
    err = ESP_OK;
  if (err == ESP_OK)
    err = nvs_commit(handle);
  nvs_close(handle);
  return err;
}
static StoredWifi load_wifi() {
  StoredWifi wifi{};
  nvs_handle_t handle;
  if (nvs_open("pbx_wifi", NVS_READONLY, &handle) == ESP_OK) {
    size_t size = sizeof(wifi);
    const esp_err_t err = nvs_get_blob(handle, "credentials", &wifi, &size);
    nvs_close(handle);
    if (err != ESP_OK || size != sizeof(wifi) || wifi.version != 1 || wifi.ssid[32] ||
        wifi.password[64] || !web_logic::credentials(wifi.ssid, wifi.password))
      wifi = {};
  }
  return wifi;
}
static esp_err_t configure_sta(const StoredWifi &wifi) {
  wifi_config_t config{};
  memcpy(config.sta.ssid, wifi.ssid, strlen(wifi.ssid));
  memcpy(config.sta.password, wifi.password, strlen(wifi.password));
  config.sta.threshold.authmode = wifi.password[0] ? WIFI_AUTH_WPA2_PSK : WIFI_AUTH_OPEN;
  config.sta.pmf_cfg.capable = true;
  config.sta.pmf_cfg.required = false;
  return esp_wifi_set_config(WIFI_IF_STA, &config);
}
// Verarbeitet WLAN-Aufträge außerhalb des HTTP-Tasks.
// Die Vorgangsreservierung verhindert Funkwechsel während Aufnahme, Tastenvorgang oder Update.
static void wifi_manager(void *) {
  StoredWifi saved = load_wifi();
  if (saved.ssid[0])
    configure_sta(saved);
  xSemaphoreTake(model_mutex, portMAX_DELAY);
  strlcpy(model.ssid, saved.ssid, sizeof(model.ssid));
  xSemaphoreGive(model_mutex);
  bool previous = false, ap = true;
  uint64_t state_since = milliseconds(), next_attempt = 0, attempt_since = 0;
  while (true) {
    Command command{};
    if (xQueueReceive(wifi_commands, &command, pdMS_TO_TICKS(250)) == pdTRUE) {
      // Der HTTP-Aufruf hat den Funkzugriff vor dem Einreihen dieses Auftrags reserviert.
      // Vor dem Kanalwechsel die HTTP-Antwort an den Browser übertragen.
      vTaskDelay(pdMS_TO_TICKS(500));
      if (command.kind == Command::SCAN) {
        if (!ip_ready) {
          esp_wifi_disconnect();
          linked = false;
        }
        wifi_scan_config_t scan{};
        scan.show_hidden = false;
        const esp_err_t err = esp_wifi_scan_start(&scan, true);
        wifi_ap_record_t records[32]{};
        uint16_t count = 32;
        esp_err_t results = err;
        if (err == ESP_OK)
          results = esp_wifi_scan_get_ap_records(&count, records);
        xSemaphoreTake(model_mutex, portMAX_DELAY);
        model.found = 0;
        if (results == ESP_OK) {
          for (unsigned i = 0; i < count; ++i) {
            if (!records[i].ssid[0])
              continue;
            auto &n = model.networks[model.found++];
            memcpy(n.ssid, records[i].ssid, 32);
            n.ssid[32] = 0;
            n.rssi = records[i].rssi;
            n.open = records[i].authmode == WIFI_AUTH_OPEN;
          }
        }
        model.scanning = false;
        strlcpy(model.message,
                results == ESP_OK
                    ? "Netzwerksuche abgeschlossen."
                    : "Netzwerksuche fehlgeschlagen. SSID kann direkt eingegeben werden.",
                sizeof(model.message));
        xSemaphoreGive(model_mutex);
        next_attempt = milliseconds() + 3000;
      } else {
        const bool forget = command.kind == Command::FORGET;
        const esp_err_t err = store_wifi(command.wifi, forget);
        if (err == ESP_OK) {
          // Vor dem Wechsel des WLAN-Zugangs den Ersatzzugangspunkt aktivieren.
          if (esp_wifi_set_mode(WIFI_MODE_APSTA) == ESP_OK)
            ap = true;
          esp_wifi_disconnect();
          linked = false;
          ip_ready = false;
          saved = forget ? StoredWifi{} : command.wifi;
          const esp_err_t configured = configure_sta(saved);
          previous = false;
          state_since = milliseconds();
          next_attempt = milliseconds() + 1000;
          xSemaphoreTake(model_mutex, portMAX_DELAY);
          strlcpy(model.ssid, saved.ssid, sizeof(model.ssid));
          strlcpy(
              model.message,
              configured != ESP_OK
                  ? "WLAN-Einstellung konnte nicht angewendet werden. Zugangspunkt bleibt aktiv."
                  : (forget ? "WLAN vergessen. Zugangspunkt bleibt aktiv."
                            : "WLAN gespeichert. Verbindung wird hergestellt."),
              sizeof(model.message));
          xSemaphoreGive(model_mutex);
        } else
          message("Speichern fehlgeschlagen. Bisherige WLAN-Einstellung bleibt erhalten.");
      }
      memset(&command, 0, sizeof(command));
      operation_give();
    }
    const uint64_t now = milliseconds();
    const bool connected = ip_ready;
    if (connected != previous) {
      state_since = now;
      previous = connected;
      message(connected ? "Mit dem Heimnetz verbunden."
                        : "WLAN nicht erreichbar. Ersatzzugangspunkt wird bereitgestellt.");
    }
    wifi_sta_list_t clients{};
    if (ap)
      esp_wifi_ap_get_sta_list(&clients);
    // Zugangspunkt aktiv lassen, solange ein PC darüber verbunden ist.
    const bool desired = web_logic::want_ap(ap, saved.ssid[0], connected, now - state_since,
                                            operation_busy() || (connected && clients.num > 0));
    if (desired != ap && operation_take()) {
      if (esp_wifi_set_mode(desired ? WIFI_MODE_APSTA : WIFI_MODE_STA) == ESP_OK)
        ap = desired;
      operation_give();
    }
    if (saved.ssid[0] && !connected && now >= next_attempt && operation_take()) {
      // Bis zu 30 Sekunden auf DHCP warten. Einen fehlgeschlagenen Verbindungsaufbau
      // nach 15 Sekunden erneut versuchen. Während einer Aufnahme weder suchen noch neu verbinden.
      if (!linked || now - attempt_since >= 30000) {
        esp_wifi_disconnect();
        linked = false;
        ip_ready = false;
        esp_err_t err = configure_sta(saved);
        if (err == ESP_OK)
          err = esp_wifi_connect();
        attempt_since = now;
        if (err != ESP_OK)
          message("WLAN-Verbindung fehlgeschlagen. Neuer Versuch folgt automatisch.");
      }
      next_attempt = now + 15000;
      operation_give();
    }
    esp_netif_ip_info_t ip{};
    esp_netif_get_ip_info(sta_netif, &ip);
    xSemaphoreTake(model_mutex, portMAX_DELAY);
    model.connected = connected;
    model.ap = ap;
    model.ap_clients = clients.num;
    model.reason = disconnect_reason;
    model.ap_seconds = (connected && ap && now - state_since < 60000)
                           ? (60000 - (now - state_since) + 999) / 1000
                           : 0;
    if (connected)
      snprintf(model.ip, sizeof(model.ip), IPSTR, IP2STR(&ip.ip));
    else
      model.ip[0] = 0;
    xSemaphoreGive(model_mutex);
  }
}

static esp_err_t json_response(httpd_req_t *req, cJSON *json, const char *status = "200 OK") {
  char *text = cJSON_PrintUnformatted(json);
  cJSON_Delete(json);
  if (!text)
    return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Speicher nicht verfügbar");
  httpd_resp_set_status(req, status);
  httpd_resp_set_type(req, "application/json; charset=utf-8");
  httpd_resp_set_hdr(req, "Cache-Control", "no-store");
  httpd_resp_set_hdr(req, "X-Content-Type-Options", "nosniff");
  const esp_err_t err = httpd_resp_send(req, text, HTTPD_RESP_USE_STRLEN);
  cJSON_free(text);
  return err;
}
static esp_err_t error_response(httpd_req_t *req, const char *text,
                                const char *status = "400 Bad Request") {
  cJSON *json = cJSON_CreateObject();
  cJSON_AddStringToObject(json, "error", text);
  return json_response(req, json, status);
}
static bool receive_form(httpd_req_t *req, web_logic::Form &form) {
  if (req->content_len < 1 || req->content_len > 768) {
    error_response(req, "Ungültige Anfragegröße.");
    return false;
  }
  char type[80];
  if (httpd_req_get_hdr_value_str(req, "Content-Type", type, sizeof(type)) != ESP_OK ||
      strncmp(type, "application/x-www-form-urlencoded", 33) != 0 ||
      (type[33] && type[33] != ';')) {
    error_response(req, "Formulardaten erwartet.", "415 Unsupported Media Type");
    return false;
  }
  std::string body(req->content_len, '\0');
  size_t received = 0;
  while (received < body.size()) {
    const int n = httpd_req_recv(req, body.data() + received, body.size() - received);
    if (n <= 0) {
      error_response(req, "Anfrage unvollständig.");
      return false;
    }
    received += n;
  }
  if (!web_logic::form(body, form)) {
    error_response(req, "Ungültige Formulardaten.");
    return false;
  }
  auto value = form.find("token");
  if (value == form.end() || value->second != token) {
    error_response(req, "Seite bitte neu laden.", "403 Forbidden");
    return false;
  }
  return true;
}
static esp_err_t status_handler(httpd_req_t *req) {
  char capture[1280];
  capture_status(capture, sizeof(capture));
  cJSON *root = cJSON_CreateObject();
  cJSON_AddStringToObject(root, "version", esp_app_get_description()->version);
  cJSON_AddStringToObject(root, "token", token);
  ota_add_status(root);
  cJSON_AddItemToObject(root, "capture", cJSON_Parse(capture));
  cJSON *wifi = cJSON_AddObjectToObject(root, "wifi");
  xSemaphoreTake(model_mutex, portMAX_DELAY);
  cJSON_AddStringToObject(wifi, "ssid", model.ssid);
  cJSON_AddStringToObject(wifi, "ip", model.ip);
  cJSON_AddStringToObject(wifi, "message", model.message);
  cJSON_AddBoolToObject(wifi, "connected", model.connected);
  cJSON_AddBoolToObject(wifi, "ap", model.ap);
  cJSON_AddBoolToObject(wifi, "scanning", model.scanning);
  cJSON_AddNumberToObject(wifi, "ap_seconds", model.ap_seconds);
  cJSON_AddNumberToObject(wifi, "ap_clients", model.ap_clients);
  cJSON_AddNumberToObject(wifi, "disconnect_reason", model.reason);
  cJSON *networks = cJSON_AddArrayToObject(wifi, "networks");
  for (unsigned i = 0; i < model.found; ++i) {
    cJSON *network = cJSON_CreateObject();
    cJSON_AddStringToObject(network, "ssid", model.networks[i].ssid);
    cJSON_AddNumberToObject(network, "rssi", model.networks[i].rssi);
    cJSON_AddBoolToObject(network, "open", model.networks[i].open);
    cJSON_AddItemToArray(networks, network);
  }
  xSemaphoreGive(model_mutex);
  return json_response(req, root);
}
static esp_err_t wifi_handler(httpd_req_t *req) {
  web_logic::Form form;
  if (!receive_form(req, form))
    return ESP_FAIL;
  Command command{};
  if (!strcmp(req->uri, "/api/scan"))
    command.kind = Command::SCAN;
  else if (!strcmp(req->uri, "/api/forget"))
    command.kind = Command::FORGET;
  else {
    command.kind = Command::SAVE;
    if (!web_logic::credentials(form["ssid"], form["password"]))
      return error_response(req, "SSID: 1–32 UTF-8-Bytes. Passwort: 8–63 Zeichen, 64 Hexziffern "
                                 "oder leer für ein offenes WLAN.");
    command.wifi.version = 1;
    strlcpy(command.wifi.ssid, form["ssid"].c_str(), sizeof(command.wifi.ssid));
    strlcpy(command.wifi.password, form["password"].c_str(), sizeof(command.wifi.password));
  }
  if (!operation_take())
    return error_response(req, "Aufnahme, Update oder WLAN-Aktion läuft bereits.", "409 Conflict");
  if (command.kind == Command::SCAN) {
    xSemaphoreTake(model_mutex, portMAX_DELAY);
    model.scanning = true;
    xSemaphoreGive(model_mutex);
  }
  if (xQueueSend(wifi_commands, &command, 0) != pdTRUE) {
    xSemaphoreTake(model_mutex, portMAX_DELAY);
    model.scanning = false;
    xSemaphoreGive(model_mutex);
    operation_give();
    return error_response(req, "Bitte erneut versuchen.", "503 Service Unavailable");
  }
  memset(&command, 0, sizeof(command));
  cJSON *json = cJSON_CreateObject();
  cJSON_AddBoolToObject(json, "accepted", true);
  return json_response(req, json, "202 Accepted");
}
static bool http_sink(void *context, const uint8_t *data, size_t size) {
  return httpd_resp_send_chunk(static_cast<httpd_req_t *>(context),
                               reinterpret_cast<const char *>(data), size) == ESP_OK;
}
static void wake_http(void *) {}
static void download_worker(void *) {
  Download download{};
  while (true) {
    xQueueReceive(downloads, &download, portMAX_DELAY);
    auto *req = download.req;
    char filename[100];
    snprintf(filename, sizeof(filename), "attachment; filename=\"probusx-%08lx.pbxs\"",
             static_cast<unsigned long>(esp_random()));
    httpd_resp_set_type(req, "application/octet-stream");
    httpd_resp_set_hdr(req, "Content-Disposition", filename);
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    httpd_resp_set_hdr(req, "X-Content-Type-Options", "nosniff");
    const bool complete = capture_run(download.duration_ms, http_sink, req);
    const bool sent = complete && httpd_resp_send_chunk(req, nullptr, 0) == ESP_OK;
    if (!sent) {
      capture_transport_failed();
      // Die Sitzung nicht löschen, solange der asynchrone Auftrag sie verwendet.
      // Danach den HTTP-Task wecken. Der HTTP-Server schließt die Verbindung.
      shutdown(httpd_req_to_sockfd(req), SHUT_RDWR);
    }
    httpd_req_async_handler_complete(req);
    httpd_queue_work(server, wake_http, nullptr);
    operation_give();
  }
}
static esp_err_t download_handler(httpd_req_t *req) {
  web_logic::Form form;
  if (!receive_form(req, form))
    return ESP_FAIL;
  uint32_t duration;
  if (!web_logic::seconds(form["seconds"], duration))
    return error_response(req, "Dauer muss 1 bis 3600 Sekunden betragen.");
  if (!operation_take())
    return error_response(req, "Aufnahme, Update oder WLAN-Aktion läuft bereits.", "409 Conflict");
  httpd_req_t *async = nullptr;
  if (httpd_req_async_handler_begin(req, &async) != ESP_OK) {
    operation_give();
    return error_response(req, "Download konnte nicht gestartet werden.",
                          "503 Service Unavailable");
  }
  Download download{async, duration};
  if (xQueueSend(downloads, &download, 0) != pdTRUE) {
    error_response(async, "Download belegt.", "503 Service Unavailable");
    httpd_req_async_handler_complete(async);
    operation_give();
  }
  return ESP_OK;
}
static esp_err_t stop_handler(httpd_req_t *req) {
  web_logic::Form form;
  if (!receive_form(req, form))
    return ESP_FAIL;
  capture_stop();
  cJSON *json = cJSON_CreateObject();
  cJSON_AddBoolToObject(json, "accepted", true);
  return json_response(req, json);
}
static esp_err_t asset_handler(httpd_req_t *req) {
  const char *data = index_start, *type = "text/html; charset=utf-8";
  if (!strcmp(req->uri, "/app.js")) {
    data = js_start;
    type = "text/javascript; charset=utf-8";
  }
  if (!strcmp(req->uri, "/style.css")) {
    data = css_start;
    type = "text/css; charset=utf-8";
  }
  httpd_resp_set_type(req, type);
  httpd_resp_set_hdr(req, "Cache-Control", "no-store");
  httpd_resp_set_hdr(req, "X-Content-Type-Options", "nosniff");
  httpd_resp_set_hdr(req, "Content-Security-Policy",
                     "default-src 'self'; script-src 'self'; style-src 'self'; object-src 'none'; "
                     "base-uri 'none'; frame-ancestors 'none'; form-action 'self'");
  return httpd_resp_send(req, data, HTTPD_RESP_USE_STRLEN);
}
static void route(const char *uri, httpd_method_t method, esp_err_t (*handler)(httpd_req_t *)) {
  httpd_uri_t route{};
  route.uri = uri;
  route.method = method;
  route.handler = handler;
  ESP_ERROR_CHECK(httpd_register_uri_handler(server, &route));
}
void web_wifi_start() {
  model_mutex = xSemaphoreCreateMutex();
  wifi_commands = xQueueCreate(1, sizeof(Command));
  downloads = xQueueCreate(1, sizeof(Download));
  configASSERT(model_mutex && wifi_commands && downloads);
  for (unsigned i = 0; i < 4; ++i)
    snprintf(token + i * 8, 9, "%08lx", static_cast<unsigned long>(esp_random()));
  ESP_ERROR_CHECK(esp_netif_init());
  ESP_ERROR_CHECK(esp_event_loop_create_default());
  sta_netif = esp_netif_create_default_wifi_sta();
  esp_netif_create_default_wifi_ap();
  ESP_ERROR_CHECK(esp_netif_set_hostname(sta_netif, "probusx-bridge"));
  const wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
  ESP_ERROR_CHECK(esp_wifi_init(&init));
  ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
  ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_event, nullptr));
  ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, wifi_event, nullptr));
  ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_LOST_IP, wifi_event, nullptr));
  ESP_ERROR_CHECK(esp_wifi_set_country_code("DE", true));
  ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));
  wifi_config_t config{};
  strlcpy(reinterpret_cast<char *>(config.ap.ssid), AP_SSID, sizeof(config.ap.ssid));
  strlcpy(reinterpret_cast<char *>(config.ap.password), AP_PASSWORD, sizeof(config.ap.password));
  config.ap.ssid_len = strlen(AP_SSID);
  config.ap.channel = 1;
  config.ap.max_connection = 2;
  config.ap.authmode = WIFI_AUTH_WPA2_PSK;
  ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &config));
  ESP_ERROR_CHECK(esp_wifi_start());
  ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_NONE));
  httpd_config_t http = HTTPD_DEFAULT_CONFIG();
  http.stack_size = 8192;
  http.core_id = 0;
  http.max_uri_handlers = 24;
  http.max_open_sockets = 6;
  http.recv_wait_timeout = 5;
  http.send_wait_timeout = 5;
  http.lru_purge_enable = false;
  ESP_ERROR_CHECK(httpd_start(&server, &http));
  route("/", HTTP_GET, asset_handler);
  route("/analysis", HTTP_GET, asset_handler);
  route("/options", HTTP_GET, asset_handler);
  route("/app.js", HTTP_GET, asset_handler);
  route("/style.css", HTTP_GET, asset_handler);
  route("/api/status", HTTP_GET, status_handler);
  route("/api/wifi", HTTP_POST, wifi_handler);
  route("/api/scan", HTTP_POST, wifi_handler);
  route("/api/forget", HTTP_POST, wifi_handler);
  route("/api/stop", HTTP_POST, stop_handler);
  route("/capture", HTTP_POST, download_handler);
  ota_register(server, token);
  live_register(server, token);
  configASSERT(xTaskCreatePinnedToCore(wifi_manager, "wifi_manager", 12288, nullptr, 4, nullptr,
                                       0) == pdPASS);
  configASSERT(xTaskCreatePinnedToCore(download_worker, "web_download", 8192, nullptr, 5, nullptr,
                                       0) == pdPASS);
  ESP_LOGI(TAG, "Weboberfläche: http://192.168.4.1 (AP %s)", AP_SSID);
}

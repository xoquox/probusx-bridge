#include "mqtt_bridge.h"
#include "mqtt_discovery.h"
#include "live.h"
#include "capture_api.h"
#include "http_connections.h"
#include "web_logic.h"
#include "ota.h"
#include "mqtt_client.h"
#include "esp_app_desc.h"
#include "esp_crt_bundle.h"
#include "esp_heap_caps.h"
#include "esp_mac.h"
#include "esp_random.h"
#include "esp_sntp.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "nvs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include <atomic>
#include <memory>
#include <string>
#include <cstdio>
using pbx_mqtt::Settings;
namespace {
SemaphoreHandle_t mutex;
QueueHandle_t changes;
Settings saved;
esp_mqtt_client_handle_t client = nullptr;
std::atomic<bool> connected{false}, announce{false};
std::atomic<uint32_t> connections{0}, failures{0}, rejected{0};
std::string root, mac;
char session[33]{}, message[128] = "MQTT ausgeschaltet";
const char *csrf;
struct Guard {
  Guard() {
    xSemaphoreTake(mutex, portMAX_DELAY);
  }
  ~Guard() {
    xSemaphoreGive(mutex);
  }
};
void result(const char *text) {
  Guard g;
  snprintf(message, sizeof(message), "%s", text);
}
std::string session_copy() {
  Guard g;
  return session;
}
esp_err_t persist(const Settings &s) {
  nvs_handle_t h;
  auto err = nvs_open("pbx_mqtt", NVS_READWRITE, &h);
  if (err != ESP_OK)
    return err;
  err = nvs_set_blob(h, "settings", &s, sizeof(s));
  if (err == ESP_OK)
    err = nvs_commit(h);
  nvs_close(h);
  return err;
}
// Nur dieser Hintergrundtask sendet Nutzdaten. Er hält dabei keine Sperre des Busdecoders.
bool publish(const std::string &topic, const char *data, size_t len, bool retain = true) {
  if (!connected.load())
    return false;
  int n = esp_mqtt_client_publish(client, topic.c_str(), data, int(len), 0, retain);
  if (n < 0) {
    ++failures;
    return false;
  }
  return true;
}
bool text(const std::string &topic, const char *s) {
  return publish(topic, s, strlen(s));
}
bool json(const std::string &topic, cJSON *o) {
  if (!o) {
    ++failures;
    return false;
  }
  char *s = cJSON_PrintUnformatted(o);
  cJSON_Delete(o);
  if (!s) {
    ++failures;
    return false;
  }
  bool ok = publish(topic, s, strlen(s));
  cJSON_free(s);
  return ok;
}
void event(void *, esp_event_base_t, int32_t id, void *data) {
  auto *e = static_cast<esp_mqtt_event_handle_t>(data);
  if (id == MQTT_EVENT_CONNECTED) {
    {
      Guard g;
      snprintf(session, sizeof(session), "%08lx%08lx%08lx%08lx", (unsigned long)esp_random(),
               (unsigned long)esp_random(), (unsigned long)esp_random(),
               (unsigned long)esp_random());
    }
    connected = true;
    ++connections;
    announce = true;
    result("MQTT verbunden");
    esp_mqtt_client_subscribe(e->client, (root + "/key").c_str(), 0);
    esp_mqtt_client_subscribe(e->client, "homeassistant/status", 0);
  } else if (id == MQTT_EVENT_DISCONNECTED) {
    connected = false;
    result("Broker nicht erreichbar. Verbindung wird erneut aufgebaut.");
  } else if (id == MQTT_EVENT_ERROR) {
    ++failures;
    result("MQTT-Fehler. Broker, Zugangsdaten und TLS prüfen.");
  } else if (id == MQTT_EVENT_DATA) {
    std::string topic(e->topic ? e->topic : "", e->topic_len);
    if (topic == "homeassistant/status") {
      if (e->current_data_offset == 0 && e->total_data_len == 6 && e->data_len == 6 &&
          !memcmp(e->data, "online", 6))
        announce = true;
      return;
    }
    // Folgefragmente haben keinen Topic. Sie dürfen nie einen neuen Befehl bilden.
    if (topic != root + "/key")
      return;
    if (e->data_len <= 0 || e->data_len > 128 || e->current_data_offset ||
        e->total_data_len != e->data_len || e->retain || e->dup) {
      ++rejected;
      result("MQTT-Taste abgelehnt: gespeichert, doppelt oder unvollständig");
      return;
    }
    std::string body(e->data, e->data_len);
    auto *o = cJSON_ParseWithLengthOpts(body.c_str(), body.size() + 1, nullptr, true);
    auto *s = cJSON_GetObjectItemCaseSensitive(o, "session");
    auto *k = cJSON_GetObjectItemCaseSensitive(o, "key");
    auto nonce = session_copy();
    bool valid =
        cJSON_IsObject(o) && cJSON_IsString(k) &&
        pbx_mqtt::command_allowed(connected, e->retain, e->dup, e->current_data_offset, e->data_len,
                                  e->total_data_len, cJSON_IsString(s) ? s->valuestring : nullptr,
                                  nonce.c_str());
    const char *error = valid ? live_queue_key(k->valuestring)
                              : "MQTT-Taste abgelehnt: Verbindungskennung ungültig";
    if (error) {
      ++rejected;
      result(error);
    } else
      result("MQTT-Taste angenommen");
    cJSON_Delete(o);
  }
}
void start_client(const Settings &s) {
  if (!s.enabled)
    return;
  static bool time_started = false;
  if (s.tls && !time_started) {
    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, "pool.ntp.org");
    esp_sntp_init();
    time_started = true;
  }
  esp_mqtt_client_config_t config{};
  config.broker.address.hostname = s.host;
  config.broker.address.port = s.port;
  config.broker.address.transport = s.tls ? MQTT_TRANSPORT_OVER_SSL : MQTT_TRANSPORT_OVER_TCP;
  if (s.tls)
    config.broker.verification.crt_bundle_attach = esp_crt_bundle_attach;
  config.credentials.client_id = root.c_str();
  config.credentials.username = s.username[0] ? s.username : nullptr;
  config.credentials.authentication.password = s.password[0] ? s.password : nullptr;
  // Diese Zeichenketten bleiben bis zum Stoppen des Clients erhalten.
  static std::string will;
  will = root + "/availability";
  config.session.last_will.topic = will.c_str();
  config.session.last_will.msg = "offline";
  config.session.last_will.qos = 1;
  config.session.last_will.retain = true;
  config.session.keepalive = 15;
  config.session.disable_clean_session = false;
  config.session.protocol_ver = MQTT_PROTOCOL_V_3_1_1;
  config.network.reconnect_timeout_ms = 5000;
  config.network.timeout_ms = 3000;
  config.buffer.size = 4096;
  config.buffer.out_size = 4096;
  config.outbox.limit = 32768;
  config.task.stack_size = 8192;
  config.task.priority = 4;
  client = esp_mqtt_client_init(&config);
  if (!client) {
    ++failures;
    result("MQTT konnte nicht gestartet werden");
    return;
  }
  if (esp_mqtt_client_register_event(client, MQTT_EVENT_ANY, event, nullptr) != ESP_OK ||
      esp_mqtt_client_start(client) != ESP_OK) {
    esp_mqtt_client_destroy(client);
    client = nullptr;
    ++failures;
    result("MQTT konnte nicht gestartet werden");
  } else
    result("MQTT verbindet mit dem Broker …");
}
void number(cJSON *o, const char *key, double value) {
  cJSON_AddNumberToObject(o, key, value);
}
bool temperatures(const LiveSnapshot &s) {
  auto *o = cJSON_CreateObject();
  for (unsigned i = 0; i < 14; ++i) {
    number(o, (std::string(pbx_mqtt::temperatures[i]) + "_raw").c_str(), s.temperatures[i]);
    bool valid =
        pbx_mqtt::fresh(s.now_us, i < 10 ? s.main_at : s.ext_at) && s.temperatures[i] != 2500;
    if (valid)
      number(o, pbx_mqtt::temperatures[i], s.temperatures[i] / 10.0);
    else
      cJSON_AddNullToObject(o, pbx_mqtt::temperatures[i]);
  }
  return json(root + "/temperatures", o);
}
bool outputs(const LiveSnapshot &s) {
  auto *o = cJSON_CreateObject();
  for (unsigned i = 0; i < 15; ++i) {
    double v = pbx_mqtt::output(s.outputs[i], i);
    bool valid = pbx_mqtt::fresh(s.now_us, i < 11 ? s.main_at : s.ext_at) && v >= 0 && v <= 100;
    const char *key = pbx_mqtt::outputs[i];
    number(o, (std::string(key) + "_raw").c_str(), s.outputs[i]);
    std::string binary = std::string(key) + "_aktiv";
    if (valid) {
      number(o, key, v);
      cJSON_AddStringToObject(o, binary.c_str(), v > 0 ? "ON" : "OFF");
    } else {
      cJSON_AddNullToObject(o, key);
      cJSON_AddNullToObject(o, binary.c_str());
    }
  }
  return json(root + "/outputs", o);
}
void diagnostics(const LiveSnapshot &s) {
  auto *o = cJSON_CreateObject();
  wifi_ap_record_t ap{};
  if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK)
    number(o, "wifi_signal", ap.rssi);
  else
    cJSON_AddNullToObject(o, "wifi_signal");
  number(o, "uptime", s.now_us / 1000000);
  number(o, "free_heap", heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
  number(o, "mqtt_connections", connections);
  number(o, "mqtt_errors", failures);
  number(o, "mqtt_rejected", rejected);
  number(o, "crc_ok", s.crc_ok);
  number(o, "crc_bad", s.crc_bad);
  number(o, "uart_errors", s.uart_errors);
  number(o, "tx_errors", s.tx_errors);
  number(o, "key_expired", s.key_expired);
  json(root + "/diagnostics", o);
}
void worker(void *) {
  auto s = std::make_unique<LiveSnapshot>();
  Settings active = saved;
  start_client(active);
  unsigned next = pbx_mqtt::ENTITY_COUNT;
  uint64_t t_temp = 0, t_output = 0, t_image = 0, t_diag = 0;
  uint32_t image_count = 0;
  int old[6] = {-1, -1, -1, -1, -1, -1};
  const char *groups[] = {"main", "ext", "display", "controls", "analysis", "bridge"};
  for (;;) {
    Settings proposed;
    if (xQueueReceive(changes, &proposed, 0) == pdTRUE) {
      if (persist(proposed) == ESP_OK) {
        if (client) {
          if (connected)
            text(root + "/availability", "offline");
          connected = false;
          esp_mqtt_client_stop(client);
          esp_mqtt_client_destroy(client);
          client = nullptr;
        }
        {
          Guard g;
          saved = proposed;
          session[0] = 0;
        }
        active = proposed;
        start_client(active);
        if (!active.enabled)
          result("MQTT ausgeschaltet");
      } else
        result("MQTT-Einstellungen konnten nicht gespeichert werden");
      operation_give();
    }
    if (connected) {
      if (announce.exchange(false)) {
        next = 0;
        t_temp = t_output = t_image = t_diag = 0;
        image_count = 0;
        for (int &v : old)
          v = -1;
        // Vor neuen Zuständen alte Verfügbarkeit zurücknehmen. LWT deckt einen Verbindungsabbruch
        // ab.
        text(root + "/availability", "offline");
        for (const char *g : groups)
          text(root + "/available/" + g, "offline");
        text(root + "/availability", "online");
      }
      live_snapshot(*s);
      bool states[] = {pbx_mqtt::fresh(s->now_us, s->main_at),
                       pbx_mqtt::fresh(s->now_us, s->ext_at),
                       pbx_mqtt::fresh(s->now_us, s->display_at,
                                       std::max<uint32_t>(3000, s->options.display_ms + 2000)),
                       s->enabled && !s->busy && !s->pending &&
                           pbx_mqtt::fresh(s->now_us, s->poll_at, 2000),
                       s->analysis,
                       true};
      // Bei Rückkehr des Busses zuerst aktuelle Werte senden, danach verfügbar melden.
      bool force_temp = old[0] != int(states[0]) || old[1] != int(states[1]);
      if (force_temp || s->now_us - t_temp >= uint64_t(s->options.temperature_ms) * 1000 ||
          !t_temp) {
        if (temperatures(*s))
          t_temp = s->now_us;
        else
          states[0] = states[1] = false;
      }
      if (force_temp || s->now_us - t_output >= uint64_t(s->options.output_ms) * 1000 ||
          !t_output) {
        if (outputs(*s))
          t_output = s->now_us;
        else
          states[0] = states[1] = false;
      }
      if (states[2] && (old[2] != 1 || image_count != s->display_count) &&
          (!t_image || s->now_us - t_image >= uint64_t(s->options.display_ms) * 1000)) {
        auto image = pbx_mqtt::png(s->display);
        if (publish(root + "/display", image.data(), image.size())) {
          image_count = s->display_count;
          t_image = s->now_us;
          auto *meta = cJSON_CreateObject();
          number(meta, "refresh_interval_ms", s->options.display_ms);
          number(meta, "display_frames", s->display_count);
          number(meta, "width", 160);
          number(meta, "height", 128);
          json(root + "/display_meta", meta);
        } else
          states[2] = false;
      }
      if (!t_diag || s->now_us - t_diag >= 5000000) {
        diagnostics(*s);
        t_diag = s->now_us;
      }
      for (unsigned i = 0; i < 6; ++i)
        if (old[i] != int(states[i]))
          if (text(root + "/available/" + groups[i], states[i] ? "online" : "offline"))
            old[i] = states[i];
      if (next < pbx_mqtt::ENTITY_COUNT && esp_mqtt_client_get_outbox_size(client) < 8192) {
        auto e = pbx_mqtt::entity(next);
        auto key = pbx_mqtt::entity_key(e);
        auto *o = pbx_mqtt::discovery(next, root, mac, session_copy(),
                                      esp_app_get_description()->version);
        char *payload = cJSON_PrintUnformatted(o);
        cJSON_Delete(o);
        if (payload) {
          std::string topic =
              "homeassistant/" + std::string(e.domain) + "/probusx_" + mac + "/" + key + "/config";
          int id = esp_mqtt_client_enqueue(client, topic.c_str(), payload, strlen(payload), 1, true,
                                           true);
          if (id >= 0)
            ++next;
          else
            ++failures;
          cJSON_free(payload);
        }
      }
    }
    vTaskDelay(pdMS_TO_TICKS(100));
  }
}
esp_err_t response(httpd_req_t *r, const char *status, const char *error) {
  httpd_resp_set_status(r, status);
  httpd_resp_set_type(r, "application/json");
  httpd_resp_set_hdr(r, "Cache-Control", "no-store");
  auto *o = cJSON_CreateObject();
  if (error)
    cJSON_AddStringToObject(o, "error", error);
  else
    cJSON_AddBoolToObject(o, "accepted", true);
  char *s = cJSON_PrintUnformatted(o);
  cJSON_Delete(o);
  auto e = httpd_resp_sendstr(r, s);
  cJSON_free(s);
  return e;
}
esp_err_t configure(httpd_req_t *r) {
  http_connections::touch(r);
  char type[80]{};
  httpd_req_get_hdr_value_str(r, "Content-Type", type, sizeof(type));
  if (strncmp(type, "application/x-www-form-urlencoded", 33) || r->content_len < 1 ||
      r->content_len > 2048)
    return response(r, "400 Bad Request", "Formulardaten ungültig");
  std::string body(r->content_len, '\0');
  size_t got = 0;
  while (got < body.size()) {
    int n = httpd_req_recv(r, body.data() + got, body.size() - got);
    if (n <= 0)
      return ESP_FAIL;
    got += n;
  }
  web_logic::Form f;
  if (!web_logic::form(body, f, 2048) || f["token"] != csrf)
    return response(r, "403 Forbidden", "Token ungültig");
  Settings s;
  {
    Guard g;
    s = saved;
  }
  uint32_t port;
  if ((f["enabled"] != "0" && f["enabled"] != "1") || (f["tls"] != "0" && f["tls"] != "1") ||
      !pbx::option_number(f["port"], port) || !port || port > 65535 ||
      f["host"].size() >= sizeof(s.host) || f["username"].size() >= sizeof(s.username) ||
      f["password"].size() >= sizeof(s.password) || f["host"].find('\0') != std::string::npos ||
      f["username"].find('\0') != std::string::npos ||
      f["password"].find('\0') != std::string::npos)
    return response(r, "400 Bad Request", "MQTT-Einstellungen ungültig");
  s.enabled = f["enabled"] == "1";
  s.tls = f["tls"] == "1";
  s.port = port;
  snprintf(s.host, sizeof(s.host), "%s", f["host"].c_str());
  snprintf(s.username, sizeof(s.username), "%s", f["username"].c_str());
  if (f["clear_password"] == "1")
    s.password[0] = 0;
  else if (!f["password"].empty())
    snprintf(s.password, sizeof(s.password), "%s", f["password"].c_str());
  if (!s.valid())
    return response(r, "400 Bad Request",
                    "Broker als DNS-Name oder IPv4-Adresse angeben. Port und Zugangsdaten prüfen.");
  if (!ota_restart_allowed() || !operation_take())
    return response(r, "409 Conflict", "Ein anderer Vorgang läuft");
  if (xQueueSend(changes, &s, 0) != pdTRUE) {
    operation_give();
    return response(r, "503 Service Unavailable", "Bitte erneut versuchen");
  }
  return response(r, "200 OK", nullptr);
}
} // Ende interner Bereich
void mqtt_bridge_status(cJSON *root_json) {
  if (!mutex)
    return;
  Guard g;
  auto *o = cJSON_AddObjectToObject(root_json, "mqtt");
  cJSON_AddBoolToObject(o, "enabled", saved.enabled);
  cJSON_AddBoolToObject(o, "tls", saved.tls);
  cJSON_AddBoolToObject(o, "connected", connected);
  cJSON_AddStringToObject(o, "host", saved.host);
  cJSON_AddNumberToObject(o, "port", saved.port);
  cJSON_AddStringToObject(o, "username", saved.username);
  cJSON_AddBoolToObject(o, "password_set", saved.password[0]);
  cJSON_AddStringToObject(o, "root", root.c_str());
  cJSON_AddStringToObject(o, "message", message);
  cJSON_AddNumberToObject(o, "connections", connections);
  cJSON_AddNumberToObject(o, "publish_errors", failures);
  cJSON_AddNumberToObject(o, "rejected_keys", rejected);
}
void mqtt_bridge_start(httpd_handle_t server, const char *token) {
  csrf = token;
  mutex = xSemaphoreCreateMutex();
  changes = xQueueCreate(1, sizeof(Settings));
  configASSERT(mutex && changes);
  nvs_handle_t h;
  if (nvs_open("pbx_mqtt", NVS_READONLY, &h) == ESP_OK) {
    Settings s;
    size_t size = sizeof(s);
    auto err = nvs_get_blob(h, "settings", &s, &size);
    nvs_close(h);
    if (err == ESP_OK && size == sizeof(s) && s.valid())
      saved = s;
  }
  uint8_t m[6];
  ESP_ERROR_CHECK(esp_read_mac(m, ESP_MAC_WIFI_STA));
  char id[13];
  snprintf(id, sizeof(id), "%02x%02x%02x%02x%02x%02x", m[0], m[1], m[2], m[3], m[4], m[5]);
  mac = id;
  root = "probusx/" + mac;
  httpd_uri_t uri{};
  uri.uri = "/api/mqtt";
  uri.method = HTTP_POST;
  uri.handler = configure;
  ESP_ERROR_CHECK(httpd_register_uri_handler(server, &uri));
  configASSERT(xTaskCreatePinnedToCore(worker, "probusx_mqtt", 12288, nullptr, 4, nullptr, 0) ==
               pdPASS);
}

#pragma once
#include "cJSON.h"
#include "mqtt_policy.h"
#include <cctype>
namespace pbx_mqtt {
struct Entity {
  const char *domain, *key, *group;
  std::string name;
  const char *unit = nullptr, *device_class = nullptr;
  bool diagnostic = false;
};
inline constexpr unsigned ENTITY_COUNT = 61;
inline Entity entity(unsigned i) {
  if (i < 14) {
    std::string name = temperatures[i];
    for (char &c : name)
      c = char(toupper(c));
    return {"sensor", temperatures[i], i < 10 ? "main" : "ext", name, "°C", "temperature"};
  }
  i -= 14;
  if (i < 15) {
    std::string name = outputs[i];
    for (char &c : name)
      c = char(toupper(c));
    return {"sensor", outputs[i], i < 11 ? "main" : "ext", name, "%"};
  }
  i -= 15;
  if (i < 15) {
    std::string name = outputs[i];
    for (char &c : name)
      c = char(toupper(c));
    return {"binary_sensor", outputs[i], i < 11 ? "main" : "ext", name + " aktiv"};
  }
  i -= 15;
  if (i == 0)
    return {"camera", "display", "display", "Display"};
  --i;
  if (i < 5) {
    const char *names[] = {"Hoch", "Rechts", "Runter", "Links", "Dreitastendruck"};
    return {"button", buttons[i], "controls", names[i]};
  }
  i -= 5;
  const char *names[] = {"WLAN-Signal",       "Laufzeit",         "Freier Heap",
                         "MQTT-Verbindungen", "MQTT-Sendefehler", "Abgelehnte MQTT-Tasten",
                         "CRC gültig",        "CRC fehlerhaft",   "UART-Fehler",
                         "Sendefehler Bus",   "Tasten verfallen"};
  const char *ids[] = {"wifi_signal", "uptime",        "free_heap",  "mqtt_connections",
                       "mqtt_errors", "mqtt_rejected", "crc_ok",     "crc_bad",
                       "uart_errors", "tx_errors",     "key_expired"};
  return {"sensor",
          ids[i],
          i >= 6 ? "analysis" : "bridge",
          names[i],
          i == 0   ? "dBm"
          : i == 1 ? "s"
          : i == 2 ? "B"
                   : nullptr,
          i == 0   ? "signal_strength"
          : i == 1 ? "duration"
                   : nullptr,
          true};
}
inline std::string entity_key(const Entity &e) {
  return std::string(e.key) + (std::string(e.domain) == "binary_sensor" ? "_aktiv" : "");
}
inline cJSON *discovery(unsigned i, const std::string &root, const std::string &mac,
                        const std::string &session, const char *version) {
  auto e = entity(i);
  auto key = entity_key(e);
  auto *o = cJSON_CreateObject();
  cJSON_AddStringToObject(o, "name", e.name.c_str());
  cJSON_AddStringToObject(o, "unique_id", ("probusx_" + mac + "_" + key).c_str());
  // Ältere HA-Versionen lesen object_id. Neuere Versionen lesen default_entity_id.
  cJSON_AddStringToObject(o, "object_id", ("probusx_" + key).c_str());
  cJSON_AddStringToObject(o, "default_entity_id",
                          (std::string(e.domain) + ".probusx_" + key).c_str());
  auto *dev = cJSON_AddObjectToObject(o, "device");
  auto *ids = cJSON_AddArrayToObject(dev, "identifiers");
  cJSON_AddItemToArray(ids, cJSON_CreateString(("probusx_" + mac).c_str()));
  cJSON_AddStringToObject(dev, "name", "ProBusX");
  cJSON_AddStringToObject(dev, "manufacturer", "ProBusX Bridge");
  cJSON_AddStringToObject(dev, "model", "Waveshare ESP32-S3 ProBusX Bridge");
  cJSON_AddStringToObject(dev, "sw_version", version);
  auto *a = cJSON_AddArrayToObject(o, "availability");
  for (const auto &topic : {root + "/availability", root + "/available/" + e.group}) {
    auto *v = cJSON_CreateObject();
    cJSON_AddStringToObject(v, "topic", topic.c_str());
    cJSON_AddItemToArray(a, v);
  }
  cJSON_AddStringToObject(o, "availability_mode", "all");
  if (e.unit)
    cJSON_AddStringToObject(o, "unit_of_measurement", e.unit);
  if (e.device_class)
    cJSON_AddStringToObject(o, "device_class", e.device_class);
  if (e.diagnostic)
    cJSON_AddStringToObject(o, "entity_category", "diagnostic");
  if (!strcmp(e.domain, "camera")) {
    cJSON_AddStringToObject(o, "topic", (root + "/display").c_str());
    // HA liest das Bildtopic immer binär. Verfügbarkeit und JSON-Metadaten brauchen UTF-8.
    cJSON_AddStringToObject(o, "encoding", "utf-8");
    cJSON_AddStringToObject(o, "json_attributes_topic", (root + "/display_meta").c_str());
  } else if (!strcmp(e.domain, "button")) {
    cJSON_AddStringToObject(o, "command_topic", (root + "/key").c_str());
    unsigned k = i - 45;
    auto *cmd = cJSON_CreateObject();
    cJSON_AddStringToObject(cmd, "key", keys[k]);
    cJSON_AddStringToObject(cmd, "session", session.c_str());
    char *s = cJSON_PrintUnformatted(cmd);
    cJSON_AddStringToObject(o, "payload_press", s);
    cJSON_free(s);
    cJSON_Delete(cmd);
    cJSON_AddBoolToObject(o, "retain", false);
    cJSON_AddNumberToObject(o, "qos", 0);
  } else {
    std::string group = e.diagnostic                         ? "diagnostics"
                        : !strcmp(e.domain, "binary_sensor") ? "outputs"
                        : i < 14                             ? "temperatures"
                                                             : "outputs";
    cJSON_AddStringToObject(o, "state_topic", (root + "/" + group).c_str());
    cJSON_AddStringToObject(o, "value_template", ("{{ value_json." + key + " }}").c_str());
    if (i < 44) {
      cJSON_AddStringToObject(o, "json_attributes_topic", (root + "/" + group).c_str());
      std::string attributes =
          "{{ {'raw': value_json." + std::string(e.key) + "_raw, 'validation': 'crc16'";
      if (!strcmp(e.group, "ext") && i >= 14) {
        const bool observed = !strcmp(e.key, "f1r1") || !strcmp(e.key, "f1r3");
        attributes += observed ? ", 'mapping': 'observed_active'" : ", 'mapping': 'provisional_zero_only'";
        attributes += ", 'source': 'main_61'";
      }
      attributes += "} | tojson }}";
      cJSON_AddStringToObject(o, "json_attributes_template", attributes.c_str());
    }
    if (!strcmp(e.domain, "binary_sensor")) {
      cJSON_AddStringToObject(o, "payload_on", "ON");
      cJSON_AddStringToObject(o, "payload_off", "OFF");
    } else if (i < 29)
      cJSON_AddStringToObject(o, "state_class", "measurement");
  }
  return o;
}
} // Namensraum pbx_mqtt

// Prüft die tatsächlich verwendeten MQTT-Hilfen ohne ESP32 und ohne Bus.
#include "mqtt_discovery.h"
#include <cassert>
#include <fstream>
#include <filesystem>
#include <vector>
int main(int argc, char **argv) {
  assert(argc == 3);
  using namespace pbx_mqtt;
  Settings cfg;
  assert(cfg.valid());
  cfg.enabled = 1;
  assert(!cfg.valid());
  strcpy(cfg.host, "mqtt.example.org");
  assert(cfg.valid());
  cfg.port = 0;
  assert(!cfg.valid());
  cfg.port = 8883;
  strcpy(cfg.host, "mqtt://host");
  assert(!cfg.valid());
  strcpy(cfg.host, "192.168.178.20");
  assert(cfg.valid());
  cfg.version = 42;
  assert(!cfg.valid());
  cfg.version = 1;
  memset(cfg.password, 'x', sizeof(cfg.password));
  assert(!cfg.valid());
  assert(fresh(2000000, 1000000));
  assert(!fresh(4000000, 1000000));
  assert(!fresh(2000000, 0));
  assert(!fresh(1, 2));
  assert(command_allowed(true, false, false, 0, 30, 30, "fresh", "fresh"));
  assert(!command_allowed(false, false, false, 0, 30, 30, "fresh", "fresh"));
  assert(!command_allowed(true, true, false, 0, 30, 30, "fresh", "fresh"));
  assert(!command_allowed(true, false, true, 0, 30, 30, "fresh", "fresh"));
  assert(!command_allowed(true, false, false, 0, 30, 60, "fresh", "fresh"));
  assert(!command_allowed(true, false, false, 1, 30, 30, "fresh", "fresh"));
  assert(!command_allowed(true, false, false, 0, 129, 129, "fresh", "fresh"));
  assert(!command_allowed(true, false, false, 0, 30, 30, "old", "fresh"));
  assert(!command_allowed(true, false, false, 0, 30, 30, nullptr, "fresh"));
  assert(!command_allowed(true, false, false, 0, 30, 30, "", ""));
  assert(output(200, 0) == 100);
  assert(output(1500, 8) == 15);
  assert(output(200, 14) == 100);
  std::filesystem::create_directories(argv[2]);
  auto *array = cJSON_CreateArray();
  for (unsigned i = 0; i < ENTITY_COUNT; ++i) {
    auto *o = discovery(i, "probusx/aabbccddeeff", "aabbccddeeff", "fresh", "3.5.0");
    assert(o);
    auto e = entity(i);
    auto *record = cJSON_CreateObject();
    std::string topic = "homeassistant/" + std::string(e.domain) + "/probusx_aabbccddeeff/" +
                        entity_key(e) + "/config";
    cJSON_AddStringToObject(record, "topic", topic.c_str());
    cJSON_AddItemToObject(record, "payload", o);
    cJSON_AddItemToArray(array, record);
    assert(cJSON_GetArraySize(cJSON_GetObjectItem(o, "availability")) == 2);
    if (!strcmp(e.group, "ext") && (!strcmp(e.domain,"binary_sensor") || i >= 14)) {
      const auto *attributes=cJSON_GetObjectItem(o,"json_attributes_template");
      assert(cJSON_IsString(attributes) && strstr(attributes->valuestring,"main_61"));
      const bool observed=!strcmp(e.key,"f1r1") || !strcmp(e.key,"f1r3");
      assert(strstr(attributes->valuestring,observed?"observed_active":"provisional_zero_only"));
    }
    if (!strcmp(e.domain, "button")) {
      assert(!cJSON_IsTrue(cJSON_GetObjectItem(o, "retain")));
      auto *cmd = cJSON_Parse(cJSON_GetObjectItem(o, "payload_press")->valuestring);
      assert(!strcmp(cJSON_GetObjectItem(cmd, "session")->valuestring, "fresh"));
      assert(!strcmp(cJSON_GetObjectItem(cmd, "key")->valuestring, keys[i - 45]));
      cJSON_Delete(cmd);
    }
  }
  char *s = cJSON_Print(array);
  std::ofstream(std::string(argv[2]) + "/discovery.json") << s;
  cJSON_free(s);
  cJSON_Delete(array);
  std::ifstream input(argv[1], std::ios::binary);
  std::vector<uint8_t> raw{std::istreambuf_iterator<char>(input), {}};
  assert(raw.size() == 5120);
  auto image = png(raw.data());
  assert(image.size() == 5316);
  std::ofstream(std::string(argv[2]) + "/display.png", std::ios::binary)
      .write(image.data(), image.size());
}

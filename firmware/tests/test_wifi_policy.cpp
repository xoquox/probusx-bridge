// Prüft die tatsächlich vom Gerät verwendeten WLAN-Regeln ohne ESP32-Hardware.
#include "wifi_policy.h"
#include <cassert>
#include <cstring>
#include <string>
using namespace wifi_policy;

struct Station {
  bool bssid_set = false;
  uint8_t bssid[6]{};
  uint8_t channel = 11;
  unsigned rm_enabled = 1, btm_enabled = 1, mbo_enabled = 1;
};
static StoredWifi fixed() {
  StoredWifi wifi{};
  const web_logic::Form form{{"ssid", "Mesh"},
                             {"password", "12345678"},
                             {"ap_mode", "fixed"},
                             {"bssid", "a2:b3:c4:d5:e6:f7"}};
  assert(from_form(form, wifi));
  return wifi;
}
int main(int argc, char **argv) {
  assert(argc == 2);
  const std::string test = argv[1];
  if (test == "bssid") {
    uint8_t bytes[6]{};
    assert(parse_bssid("02:00:00:00:00:01", bytes));
    assert(parse_bssid("a2:b3:c4:d5:e6:f7", bytes));
    char text[18]{};
    format_bssid(bytes, text);
    assert(std::string(text) == "A2:B3:C4:D5:E6:F7");
    for (const auto *bad :
         {"", "00:00:00:00:00:00", "FF:FF:FF:FF:FF:FF", "01:22:33:44:55:66", "A2-B3-C4-D5-E6-F7",
          "A2:B3:C4:D5:E6:GG", "A2:B3:C4:D5:E6:F7x", "A2:B3:C4:D5:E6:F", " A2:B3:C4:D5:E6:F7"})
      assert(!parse_bssid(bad, bytes));
  } else if (test == "legacy") {
    LegacyWifi legacy{};
    legacy.version = 1;
    strcpy(legacy.ssid, "Mesh");
    strcpy(legacy.password, "12345678");
    StoredWifi wifi = fixed();
    assert(decode(&legacy, sizeof(legacy), wifi));
    assert(wifi.version == 2 && wifi.mode == AUTO && !wifi.bssid[0]);
    assert(std::string(wifi.ssid) == "Mesh" && std::string(wifi.password) == "12345678");
    legacy.ssid[32] = 'x';
    assert(!decode(&legacy, sizeof(legacy), wifi) && !wifi.ssid[0]);
  } else if (test == "persist") {
    const auto wifi = fixed();
    uint8_t nvs[sizeof(StoredWifi)]{};
    memcpy(nvs, &wifi, sizeof(wifi));
    StoredWifi reboot{};
    assert(decode(nvs, sizeof(nvs), reboot));
    assert(reboot.mode == FIXED && !memcmp(reboot.bssid, wifi.bssid, 6));
    Station station;
    apply_binding(station, reboot);
    assert(station.bssid_set && !memcmp(station.bssid, wifi.bssid, 6));
    assert(station.channel == 0 && !station.btm_enabled && !station.mbo_enabled &&
           !station.rm_enabled);
    // Wiederverbindungen setzen exakt dieselbe Bindung erneut. Kein Rückfall auf eine fremde BSSID.
    for (unsigned retry = 0; retry < 5; ++retry) {
      station.bssid_set = false;
      memset(station.bssid, 0, 6);
      apply_binding(station, reboot);
      assert(station.bssid_set && !memcmp(station.bssid, wifi.bssid, 6));
    }
    assert(web_logic::want_ap(false, true, false, 15000, false));
  } else if (test == "automatic") {
    StoredWifi wifi{};
    assert(from_form({{"ssid", "Mesh"}, {"password", "12345678"}}, wifi));
    assert(wifi.mode == AUTO);
    Station station;
    apply_binding(station, fixed());
    apply_binding(station, wifi);
    assert(!station.bssid_set);
    for (auto byte : station.bssid)
      assert(!byte);
    assert(from_form(
        {{"ssid", "Offen"}, {"password", ""}, {"ap_mode", "auto"}, {"bssid", "alte Auswahl"}},
        wifi));
    assert(valid(wifi) && wifi.mode == AUTO);
  } else if (test == "reject") {
    StoredWifi wifi = fixed(), decoded{};
    wifi.mode = 3;
    assert(!decode(&wifi, sizeof(wifi), decoded) && !decoded.ssid[0]);
    wifi = fixed();
    wifi.bssid[0] = 1;
    assert(!decode(&wifi, sizeof(wifi), decoded) && !decoded.ssid[0]);
    wifi = fixed();
    wifi.version = 9;
    assert(!decode(&wifi, sizeof(wifi), decoded));
    wifi = fixed();
    assert(!decode(&wifi, sizeof(wifi) - 1, decoded));
    for (const web_logic::Form &form :
         {web_logic::Form{{"ssid", "Mesh"}, {"password", "12345678"}, {"ap_mode", "fixed"}},
          web_logic::Form{{"ssid", "Mesh"}, {"password", "12345678"}, {"ap_mode", "anderer"}},
          web_logic::Form{{"ssid", "Mesh"}, {"password", "kurz"}, {"ap_mode", "auto"}}})
      assert(!from_form(form, decoded));
  } else if (test == "bounds") {
    StoredWifi wifi{};
    assert(from_form({{"ssid", std::string(32, 's')},
                      {"password", std::string(64, 'a')},
                      {"ap_mode", "fixed"},
                      {"bssid", "02:00:00:00:00:01"}},
                     wifi));
    StoredWifi loaded{};
    assert(decode(&wifi, sizeof(wifi), loaded));
    assert(strlen(loaded.ssid) == 32 && strlen(loaded.password) == 64);
    assert(!from_form({{"ssid", std::string(33, 's')}, {"password", "12345678"}}, wifi));
    assert(web_logic::want_ap(true, false, false, 0, false));
    assert(!web_logic::want_ap(false, true, false, 14999, false));
    assert(web_logic::want_ap(false, true, false, 15000, false));
  } else
    return 2;
}

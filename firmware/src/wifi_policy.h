#pragma once
#include "web_logic.h"
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <type_traits>

// Plattformunabhängige WLAN-Regeln. Gerät und Tests verwenden dieselbe Umsetzung.
namespace wifi_policy {
enum Mode : uint8_t { AUTO = 0, FIXED = 1 };

// Version 1 entspricht genau dem bisher gespeicherten NVS-Datensatz.
struct LegacyWifi {
  uint32_t version;
  char ssid[33];
  char password[65];
};
struct StoredWifi {
  uint32_t version;
  char ssid[33];
  char password[65];
  uint8_t mode;
  uint8_t bssid[6];
  uint8_t reserved[3];
};
static_assert(sizeof(LegacyWifi) == 104, "NVS-Layout der Version 1 muss erhalten bleiben");
static_assert(sizeof(StoredWifi) == 112, "NVS-Layout der Version 2 muss stabil bleiben");
static_assert(std::is_trivially_copyable<StoredWifi>::value,
              "FreeRTOS-Auftrag braucht kopierbare Daten");

inline bool valid_bssid(const uint8_t *bssid) {
  uint8_t nonzero = 0;
  for (unsigned i = 0; i < 6; ++i)
    nonzero |= bssid[i];
  // Multicast, Broadcast und die Nulladresse sind keine wählbaren Zugangspunkte.
  return nonzero && !(bssid[0] & 1);
}
inline bool parse_bssid(const std::string &text, uint8_t *out) {
  if (text.size() != 17)
    return false;
  uint8_t value[6]{};
  for (unsigned i = 0; i < 6; ++i) {
    const unsigned at = i * 3;
    const int high = web_logic::hex(text[at]), low = web_logic::hex(text[at + 1]);
    if (high < 0 || low < 0 || (i < 5 && text[at + 2] != ':'))
      return false;
    value[i] = uint8_t(high * 16 + low);
  }
  if (!valid_bssid(value))
    return false;
  memcpy(out, value, sizeof(value));
  return true;
}
inline void format_bssid(const uint8_t *bssid, char (&out)[18]) {
  snprintf(out, sizeof(out), "%02X:%02X:%02X:%02X:%02X:%02X", bssid[0], bssid[1], bssid[2],
           bssid[3], bssid[4], bssid[5]);
}
inline bool valid(const StoredWifi &wifi) {
  if (wifi.version != 2 || wifi.ssid[32] || wifi.password[64] ||
      !web_logic::credentials(wifi.ssid, wifi.password) || wifi.mode > FIXED)
    return false;
  if (wifi.mode == FIXED)
    return valid_bssid(wifi.bssid);
  for (auto byte : wifi.bssid)
    if (byte)
      return false;
  return true;
}
// Falsche oder beschädigte Daten werden nicht auf automatische Verbindung zurückgesetzt.
// Stattdessen bleibt die SSID leer. Dann steht nur der Ersatzzugangspunkt zur Verfügung.
inline bool decode(const void *data, size_t size, StoredWifi &out) {
  out = {};
  StoredWifi value{};
  if (size == sizeof(LegacyWifi)) {
    LegacyWifi legacy{};
    memcpy(&legacy, data, sizeof(legacy));
    if (legacy.version != 1)
      return false;
    value.version = 2;
    memcpy(value.ssid, legacy.ssid, sizeof(value.ssid));
    memcpy(value.password, legacy.password, sizeof(value.password));
    value.mode = AUTO;
  } else if (size == sizeof(StoredWifi)) {
    memcpy(&value, data, sizeof(value));
  } else
    return false;
  if (!valid(value))
    return false;
  out = value;
  return true;
}
inline bool from_form(const web_logic::Form &form, StoredWifi &out) {
  const auto get = [&form](const char *key) -> std::string {
    const auto it = form.find(key);
    return it == form.end() ? std::string{} : it->second;
  };
  const auto ssid = get("ssid"), password = get("password");
  if (!web_logic::credentials(ssid, password))
    return false;
  StoredWifi value{};
  value.version = 2;
  memcpy(value.ssid, ssid.data(), ssid.size());
  memcpy(value.password, password.data(), password.size());
  const auto mode = get("ap_mode");
  // Ein alter Client ohne Modusfeld behält das bisherige automatische Verhalten.
  if (mode.empty() || mode == "auto")
    value.mode = AUTO;
  else if (mode == "fixed") {
    value.mode = FIXED;
    if (!parse_bssid(get("bssid"), value.bssid))
      return false;
  } else
    return false;
  out = value;
  return true;
}
// BTM/MBO können laut ESP-IDF eine feste BSSID löschen. Deshalb hier ausdrücklich abschalten.
// Den Kanal nicht festschreiben: Derselbe Zugangspunkt darf seinen Funkkanal wechseln.
template <class Station> inline void apply_binding(Station &station, const StoredWifi &wifi) {
  station.bssid_set = wifi.mode == FIXED;
  memset(station.bssid, 0, sizeof(station.bssid));
  if (station.bssid_set)
    memcpy(station.bssid, wifi.bssid, sizeof(wifi.bssid));
  station.channel = 0;
  station.rm_enabled = 0;
  station.btm_enabled = 0;
  station.mbo_enabled = 0;
}
} // Namensraum wifi_policy

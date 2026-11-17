#pragma once
#include "schema_store.h"
#include "cJSON.h"
#include <cmath>
#include <memory>
#include <string>
#include <set>
namespace schema_validation {
using namespace schema_store;
inline bool number(cJSON *value, double minimum, double maximum, bool integer = false) {
  return cJSON_IsNumber(value) && std::isfinite(value->valuedouble) &&
         value->valuedouble >= minimum && value->valuedouble <= maximum &&
         (!integer || std::floor(value->valuedouble) == value->valuedouble);
}
// Kanäle und Grenzen auf dem Gerät prüfen. Bezeichnungen bleiben gewöhnlicher Text.
inline bool normalize_layout(const char *body, size_t size, uint32_t &revision, std::string &out) {
  const char *end = nullptr;
  auto *json = cJSON_ParseWithLengthOpts(body, size + 1, &end, true);
  if (!json) return false;
  if (end != body + size) { cJSON_Delete(json); return false; }
  std::unique_ptr<cJSON, decltype(&cJSON_Delete)> owner(json, cJSON_Delete);
  auto *version = cJSON_GetObjectItemCaseSensitive(json, "version");
  auto *rev = cJSON_GetObjectItemCaseSensitive(json, "revision");
  auto *items = cJSON_GetObjectItemCaseSensitive(json, "items");
  if (!number(version, 1, 1, true) || !number(rev, 0, UINT32_MAX, true) ||
      !cJSON_IsArray(items) || cJSON_GetArraySize(items) > 45) return false;
  revision = uint32_t(rev->valuedouble);
  std::set<std::string> seen;
  auto *clean = cJSON_CreateObject();
  cJSON_AddNumberToObject(clean, "version", 1);
  auto *array = cJSON_AddArrayToObject(clean, "items");
  std::unique_ptr<cJSON, decltype(&cJSON_Delete)> clean_owner(clean, cJSON_Delete);
  cJSON *item;
  cJSON_ArrayForEach(item, items) {
    auto *id = cJSON_GetObjectItemCaseSensitive(item, "id");
    auto *x = cJSON_GetObjectItemCaseSensitive(item, "x");
    auto *y = cJSON_GetObjectItemCaseSensitive(item, "y");
    auto *visible = cJSON_GetObjectItemCaseSensitive(item, "visible");
    auto *label = cJSON_GetObjectItemCaseSensitive(item, "label");
    if (!cJSON_IsString(id) || !channel(id->valuestring) || !seen.emplace(id->valuestring).second ||
        !number(x, 0, 100) || !number(y, 0, 100) || !cJSON_IsBool(visible) ||
        !cJSON_IsString(label) || strlen(label->valuestring) > 80) return false;
    for (const unsigned char *p = reinterpret_cast<const unsigned char *>(label->valuestring); *p; ++p)
      if (*p < 32 || *p == 127) return false;
    auto *entry = cJSON_CreateObject();
    cJSON_AddStringToObject(entry, "id", id->valuestring);
    cJSON_AddNumberToObject(entry, "x", x->valuedouble);
    cJSON_AddNumberToObject(entry, "y", y->valuedouble);
    cJSON_AddBoolToObject(entry, "visible", cJSON_IsTrue(visible));
    cJSON_AddStringToObject(entry, "label", label->valuestring);
    cJSON_AddItemToArray(array, entry);
  }
  char *printed = cJSON_PrintUnformatted(clean);
  if (!printed) return false;
  out = printed;
  cJSON_free(printed);
  return out.size() <= layout_limit;
}
inline uint32_t big32(const uint8_t *p) { return uint32_t(p[0]) << 24 | uint32_t(p[1]) << 16 | uint32_t(p[2]) << 8 | p[3]; }
// Der Browser wandelt das Bild vor dem Senden in PNG oder JPEG um. Der Server
// prüft zusätzlich Typ und Maße. SVG und ausführbare Bildformate werden nicht gespeichert.
inline bool image_info(const uint8_t *data, size_t size, uint32_t &format, uint16_t &width, uint16_t &height) {
  if (size >= 45 && !memcmp(data, "\x89PNG\r\n\x1a\n", 8) && !memcmp(data + 12, "IHDR", 4) &&
      big32(data + 8) == 13 && !memcmp(data + size - 8, "IEND", 4)) {
    uint32_t w = big32(data + 16), h = big32(data + 20);
    if (!w || !h || w > 4096 || h > 4096) return false;
    format = 1; width = w; height = h; return true;
  }
  if (size < 16 || data[0] != 0xff || data[1] != 0xd8 || data[size - 2] != 0xff || data[size - 1] != 0xd9) return false;
  size_t at = 2;
  while (at + 4 <= size) {
    if (data[at++] != 0xff) return false;
    while (at < size && data[at] == 0xff) ++at;
    if (at >= size) return false;
    uint8_t marker = data[at++];
    if (marker == 0xda || marker == 0xd9) return false;
    if (marker == 0x01 || (marker >= 0xd0 && marker <= 0xd7)) continue;
    if (at + 2 > size) return false;
    size_t n = size_t(data[at]) * 256 + data[at + 1];
    if (n < 2 || n > size - at) return false;
    if (marker == 0xc0 || marker == 0xc1 || marker == 0xc2) {
      if (n < 8) return false;
      height = uint16_t(data[at + 3]) * 256 + data[at + 4];
      width = uint16_t(data[at + 5]) * 256 + data[at + 6];
      format = 2;
      return width && height && width <= 4096 && height <= 4096;
    }
    at += n;
  }
  return false;
}

} // Namensraum schema_validation

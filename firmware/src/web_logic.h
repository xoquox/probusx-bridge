#pragma once
#include <cstdint>
#include <cstring>
#include <map>
#include <string>

namespace web_logic {
inline int hex(char c) {
  if (c >= '0' && c <= '9')
    return c - '0';
  if (c >= 'a' && c <= 'f')
    return c - 'a' + 10;
  if (c >= 'A' && c <= 'F')
    return c - 'A' + 10;
  return -1;
}
inline bool unquote(const std::string &in, std::string &out) {
  out.clear();
  for (size_t i = 0; i < in.size(); ++i) {
    unsigned char c = in[i];
    if (c == '+')
      c = ' ';
    else if (c == '%') {
      if (i + 2 >= in.size() || hex(in[i + 1]) < 0 || hex(in[i + 2]) < 0)
        return false;
      c = hex(in[i + 1]) * 16 + hex(in[i + 2]);
      i += 2;
    }
    if (!c || c < 32 || c == 127)
      return false;
    out += char(c);
  }
  return true;
}
using Form = std::map<std::string, std::string>;
inline bool form(const std::string &body, Form &out, size_t max_bytes = 768) {
  out.clear();
  if (body.empty() || body.size() > max_bytes)
    return false;
  size_t start = 0;
  while (start < body.size()) {
    size_t end = body.find('&', start);
    if (end == std::string::npos)
      end = body.size();
    const auto pair = body.substr(start, end - start);
    const auto eq = pair.find('=');
    std::string key, value;
    if (eq == std::string::npos || !unquote(pair.substr(0, eq), key) || key.empty() ||
        !unquote(pair.substr(eq + 1), value) || out.count(key) || out.size() >= 8)
      return false;
    out.emplace(key, value);
    start = end + 1;
  }
  return true;
}
inline bool seconds(const std::string &s, uint32_t &milliseconds) {
  if (s.empty() || s.size() > 4)
    return false;
  unsigned n = 0;
  for (char c : s) {
    if (c < '0' || c > '9')
      return false;
    n = n * 10 + c - '0';
  }
  if (n < 1 || n > 3600)
    return false;
  milliseconds = n * 1000;
  return true;
}
inline bool credentials(const std::string &ssid, const std::string &password) {
  if (ssid.empty() || ssid.size() > 32)
    return false;
  if (password.empty())
    return true; // Ein leeres Passwort bezeichnet ausdrücklich ein offenes WLAN.
  if (password.size() == 64) {
    for (char c : password)
      if (hex(c) < 0)
        return false;
    return true;
  }
  return password.size() >= 8 && password.size() <= 63;
}
// Zugangspunkt beim Start und bis 60 Sekunden nach Erhalt einer IP-Adresse aktiv lassen.
// Nach 15 Sekunden ohne IP-Adresse wieder aktivieren. Während einer Aufnahme nicht umschalten.
inline bool want_ap(bool current, bool configured, bool connected, uint64_t state_age_ms,
                    bool busy) {
  if (busy)
    return current;
  if (!configured)
    return true;
  if (connected)
    return current && state_age_ms < 60000;
  return current || state_age_ms >= 15000;
}
} // Namensraum web_logic

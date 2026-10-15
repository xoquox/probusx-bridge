#include "web_logic.h"
#include <cassert>
#include <string>
using namespace web_logic;
int main(int argc, char **argv) {
  assert(argc == 2);
  std::string test = argv[1];
  if (test == "forms") {
    Form f;
    assert(form("ssid=K%C3%BCche+%26+Hof&password=A%2BB%3D%25%26abcdefgh&token=123", f));
    assert(f.at("ssid") == "Küche & Hof");
    assert(f.at("password") == "A+B=%&abcdefgh");
    assert(form("ssid=Open&password=&token=123", f));
    assert(f.at("password").empty());
  } else if (test == "invalid_forms") {
    Form f;
    for (auto b :
         {"ssid=a&ssid=b", "=x", "x=%00", "x=%0D%0A", "x=%GG", "x=%2", "token=1&token=2", "x"})
      assert(!form(b, f));
    assert(!form(std::string(769, 'a'), f));
    assert(!form(std::string("x=a\0b", 5), f));
  } else if (test == "credentials") {
    assert(credentials("Open", ""));
    assert(credentials("SSID", "12345678"));
    assert(!credentials("SSID", "1234567"));
    assert(credentials(std::string(32, 'x'), std::string(63, 'p')));
    assert(!credentials(std::string(33, 'x'), std::string(63, 'p')));
    assert(credentials("SSID", std::string(64, 'a')));
    assert(!credentials("SSID", std::string(64, 'g')));
    assert(!credentials("", "12345678"));
    std::string name;
    for (int i = 0; i < 16; ++i)
      name += "ü";
    assert(credentials(name, "12345678"));
    assert(!credentials(name + "ü", "12345678"));
  } else if (test == "duration") {
    uint32_t ms;
    assert(seconds("1", ms) && ms == 1000);
    assert(seconds("3600", ms) && ms == 3600000);
    for (auto s : {"", "0", "3601", "-1", "1.5", "10000", "NaN", "12x"})
      assert(!seconds(s, ms));
  } else if (test == "fallback") {
    assert(want_ap(true, false, false, 0, false));
    assert(want_ap(true, true, true, 59999, false));
    assert(!want_ap(true, true, true, 60000, false));
    assert(!want_ap(false, true, false, 14999, false));
    assert(want_ap(false, true, false, 15000, false));
    assert(want_ap(true, true, false, 0, false));
    assert(want_ap(true, true, true, 90000,
                   true)); // Aufnahme oder verbundener PC hält den Zugangspunkt aktiv.
    assert(!want_ap(false, true, false, 90000, true)); // Freigabe der Aufnahme abwarten.
    assert(want_ap(false, true, false, 90000, false));
    assert(!want_ap(false, true, true, 1000,
                    false)); // Bei kurzem Ausfall den Zugangspunkt noch nicht neu starten.
  } else
    return 2;
}

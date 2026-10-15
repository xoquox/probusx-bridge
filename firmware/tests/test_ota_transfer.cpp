#include "ota_transfer.h"
#include <cassert>
#include <vector>
#include <string>
#include <iostream>
struct Fake {
  std::vector<uint8_t> input, flash;
  size_t pos = 0, chunk = 37, progress_value = 0;
  bool begin_ok = true, write_ok = true, end_ok = true, select_ok = true;
  std::string events;
  Fake() : input(10000, 0) {
    input[0] = 0xe9;
    input[1] = 4;
    input[12] = 9;
    input[23] = 1;
    input[32] = 0x32;
    input[33] = 0x54;
    input[34] = 0xcd;
    input[35] = 0xab;
    memcpy(input.data() + 80, ota_policy::project, sizeof(ota_policy::project));
    memcpy(input.data() + 48, "2.2.0", 6);
  }
  int read(uint8_t *p, size_t n) {
    n = std::min({n, chunk, input.size() - pos});
    memcpy(p, input.data() + pos, n);
    pos += n;
    return n;
  }
  bool begin(size_t) {
    events += 'B';
    return begin_ok;
  }
  bool write(const uint8_t *p, size_t n) {
    events += 'W';
    flash.insert(flash.end(), p, p + n);
    return write_ok;
  }
  void progress(size_t n) {
    progress_value = n;
  }
  void abort() {
    events += 'A';
  }
  bool end() {
    events += 'E';
    return end_ok;
  }
  bool select() {
    events += 'S';
    return select_ok;
  }
};
int main() {
  using ota_policy::transfer;
  Fake good;
  assert(!transfer(good, 10000, 0x300000));
  assert(good.flash == good.input);
  assert(good.events.substr(good.events.size() - 2) == "ES");
  assert(good.progress_value == 10000);
  for (size_t cut : {size_t(0), size_t(100), size_t(287), size_t(288), size_t(400), size_t(9999)}) {
    Fake f;
    f.input.resize(cut);
    assert(transfer(f, 10000, 0x300000));
    assert(f.events.find('S') == std::string::npos);
    assert(f.events.find('E') == std::string::npos);
    if (cut >= 288)
      assert(f.events.back() == 'A');
    else
      assert(f.events.empty());
  }
  for (size_t byte : {size_t(0), size_t(12), size_t(13), size_t(23), size_t(32), size_t(80)}) {
    Fake f;
    f.input[byte] ^= 0xff;
    assert(transfer(f, 10000, 0x300000));
    assert(f.events.empty());
  }
  Fake big;
  assert(transfer(big, 0x300001, 0x300000));
  assert(big.events.empty());
  Fake small;
  assert(transfer(small, 319, 0x300000));
  assert(small.events.empty());
  Fake capacity;
  assert(transfer(capacity, 10000, 0));
  assert(capacity.events.empty());
  Fake erase;
  erase.begin_ok = false;
  assert(transfer(erase, 10000, 0x300000));
  assert(erase.events == "B");
  Fake write;
  write.write_ok = false;
  assert(transfer(write, 10000, 0x300000));
  assert(write.events == "BWA");
  Fake digest;
  digest.end_ok = false;
  assert(transfer(digest, 10000, 0x300000));
  assert(digest.events.back() == 'E');
  assert(digest.events.find('S') == std::string::npos);
  Fake select;
  select.select_ok = false;
  assert(transfer(select, 10000, 0x300000));
  assert(select.events.substr(select.events.size() - 2) == "ES");
  std::cout << "Bestanden: blockweise Übertragung, Abbruch vor und nach Löschen, falsches Ziel und "
               "falscher Kopf sowie Schreib-, Prüf- und Aktivierungsfehler\n";
}

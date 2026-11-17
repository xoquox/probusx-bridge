#pragma once
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <deque>
#include <limits>
#include <vector>
namespace pbx_history {
constexpr unsigned channels = 29, chunk_bytes = 65536;
constexpr int16_t missing = std::numeric_limits<int16_t>::min();
struct Settings {
  uint32_t version = 1, interval_s = 10;
  uint8_t enabled = 1, flex1 = 1, reserved[6]{};
  bool valid() const {
    return version == 1 && interval_s >= 1 && interval_s <= 3600 && enabled <= 1 && flex1 <= 1;
  }
};
// Rohwerte bewahren auch halbe Prozentpunkte. Zeit gehört zum aktuellen Gerätestart.
struct Record {
  uint64_t ms{};
  int16_t values[channels]{};
  uint16_t flags{};
  uint32_t reserved{};
};
static_assert(sizeof(Record) == 72);
struct Bucket {
  uint64_t first_ms{}, last_ms{};
  int16_t minimum[channels], maximum[channels], first[channels], last[channels];
  uint32_t gap_mask{}, count{};
};
static_assert(sizeof(Bucket) == 256);
struct Header {
  char magic[4] = {'P', 'B', 'H', '1'};
  uint32_t version = 1, count{}, stride = sizeof(Bucket);
  uint64_t from{}, to{};
};
static_assert(sizeof(Header) == 32);
// Feste PSRAM-Blöcke lassen sich ohne Kopieren des ganzen Verlaufs freigeben.
// Ungenutzte Blöcke zuerst abgeben; danach ausschließlich die ältesten Datenblöcke.
class Store {
  struct Chunk {
    Record *data;
    size_t count;
  };
  std::deque<Chunk> used;
  std::vector<Record *> spare;
  void (*release)(void *);
  uint64_t removed_ = 0, samples_ = 0;

public:
  static constexpr size_t per_chunk = chunk_bytes / sizeof(Record);
  explicit Store(void (*free_fn)(void *)) : release(free_fn) {}
  ~Store() {
    while (release_one()) {
    };
  }
  Store(const Store &) = delete;
  Store &operator=(const Store &) = delete;
  void add_block(void *p) {
    spare.push_back(static_cast<Record *>(p));
  }
  size_t blocks() const {
    return used.size() + spare.size();
  }
  size_t bytes() const {
    return blocks() * chunk_bytes;
  }
  size_t count() const {
    return samples_;
  }
  uint64_t removed() const {
    return removed_;
  }
  uint64_t oldest() const {
    return used.empty() ? 0 : used.front().data[0].ms;
  }
  uint64_t newest() const {
    return used.empty() ? 0 : used.back().data[used.back().count - 1].ms;
  }
  bool release_one() {
    if (!spare.empty()) {
      release(spare.back());
      spare.pop_back();
      return true;
    }
    if (used.empty())
      return false;
    auto c = used.front();
    used.pop_front();
    samples_ -= c.count;
    removed_ += c.count;
    release(c.data);
    return true;
  }
  bool append(const Record &r) {
    if (!blocks())
      return false;
    if (used.empty() || used.back().count == per_chunk) {
      Record *p;
      if (!spare.empty()) {
        p = spare.back();
        spare.pop_back();
      } else {
        auto c = used.front();
        used.pop_front();
        samples_ -= c.count;
        removed_ += c.count;
        p = c.data;
      }
      used.push_back({p, 0});
    }
    used.back().data[used.back().count++] = r;
    ++samples_;
    return true;
  }
  template <class F> void each(F f) const {
    for (const auto &c : used)
      for (size_t i = 0; i < c.count; ++i)
        f(c.data[i]);
  }
  void aggregate(uint64_t from, uint64_t to, Bucket *out, size_t n) const {
    for (size_t i = 0; i < n; ++i) {
      out[i] = {};
      for (unsigned c = 0; c < channels; ++c)
        out[i].minimum[c] = out[i].maximum[c] = out[i].first[c] = out[i].last[c] = missing;
    }
    if (!n || from >= to)
      return;
    each([&](const Record &r) {
      if (r.ms < from || r.ms > to)
        return;
      size_t i = std::min(n - 1, size_t((r.ms - from) * n / (to - from)));
      auto &b = out[i];
      if (!b.count)
        b.first_ms = r.ms;
      b.last_ms = r.ms;
      ++b.count;
      for (unsigned c = 0; c < channels; ++c) {
        int16_t v = r.values[c];
        if (v == missing) {
          b.gap_mask |= uint32_t(1) << c;
          continue;
        }
        if (b.minimum[c] == missing) {
          b.minimum[c] = b.maximum[c] = b.first[c] = v;
        } else {
          b.minimum[c] = std::min(b.minimum[c], v);
          b.maximum[c] = std::max(b.maximum[c], v);
        }
        b.last[c] = v;
      }
    });
  }
};
} // Namensraum pbx_history

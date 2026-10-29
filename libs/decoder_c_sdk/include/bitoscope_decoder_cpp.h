/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <array>
#include <bit>
#include <cstdint>
#include <cstring>
#include <expected>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

#include "bitoscope_decoder.h"
#include "bitoscope_decoder_layers.h"
#include "bitoscope_decoder_testing.h"

namespace bitoscope::plugin {

enum class Edge : int {
  rising = BITOSCOPE_EDGE_RISING,
  falling = BITOSCOPE_EDGE_FALLING,
  any = BITOSCOPE_EDGE_ANY,
};

namespace detail {

static_assert(std::endian::native == std::endian::little, "the span kernels read planes as little-endian words");

inline constexpr uint64_t kNoSample = UINT64_MAX;

inline uint64_t planeWord(const uint8_t* plane, uint64_t k) {
  uint64_t w = 0;
  std::memcpy(&w, plane + k * 8, 8);
  return w;
}

inline uint64_t validMask(uint64_t count, uint64_t k) {
  const uint64_t rest = count - k * 64;
  return rest >= 64 ? ~uint64_t{0} : (uint64_t{1} << rest) - 1;
}

inline bool flatLevel(const bitoscope_span& s, uint32_t channel) {
  return ((s.levels[channel / 8] >> (channel % 8)) & 1u) != 0;
}

inline bool spanLevel(const bitoscope_span& s, uint32_t channel, uint64_t r) {
  if (s.planes == nullptr) return flatLevel(s, channel);
  const uint8_t* p = s.planes + channel * s.plane_stride;
  return ((p[r / 8] >> (r % 8)) & 1u) != 0;
}

inline bool kindMatches(int kind, bool new_level) {
  return kind == BITOSCOPE_EDGE_ANY || (kind == BITOSCOPE_EDGE_RISING) == new_level;
}

inline uint64_t edgeMask(uint64_t w, uint64_t carry, int kind) {
  const uint64_t changed = w ^ ((w << 1) | carry);
  if (kind == BITOSCOPE_EDGE_RISING) return changed & w;
  if (kind == BITOSCOPE_EDGE_FALLING) return changed & ~w;
  return changed;
}

// WHY: planeNextEdge(): prev is the carry of r0's word and is right only when r0 % 64 == 0;
// otherwise the edge it makes is before r0 and is masked off.
inline uint64_t planeNextEdge(const bitoscope_span& s, uint32_t channel, uint64_t r0, bool prev, int kind) {
  const uint64_t n = s.num_samples;
  if (r0 >= n) return kNoSample;
  const uint8_t* p = s.planes + channel * s.plane_stride;
  const uint64_t words = (n + 63) / 64;
  uint64_t k = r0 / 64;
  uint64_t w = planeWord(p, k);
  uint64_t x = edgeMask(w, prev ? 1u : 0u, kind) & (~uint64_t{0} << (r0 % 64)) & validMask(n, k);
  if (x != 0) return k * 64 + static_cast<uint64_t>(std::countr_zero(x));
  uint64_t carry = w >> 63;
  for (++k; k < words; ++k) {
    w = planeWord(p, k);
    if (w == 0 - carry) continue;
    x = edgeMask(w, carry, kind) & validMask(n, k);
    if (x != 0) return k * 64 + static_cast<uint64_t>(std::countr_zero(x));
    carry = w >> 63;
  }
  return kNoSample;
}

inline uint64_t planeNextChange2(const bitoscope_span& s, uint32_t a, uint32_t b, uint64_t r0, bool prev_a,
                                 bool prev_b, uint32_t* which) {
  const uint64_t n = s.num_samples;
  if (r0 >= n) return kNoSample;
  const uint8_t* pa = s.planes + a * s.plane_stride;
  const uint8_t* pb = s.planes + b * s.plane_stride;
  const uint64_t words = (n + 63) / 64;
  uint64_t carry_a = prev_a ? 1u : 0u;
  uint64_t carry_b = prev_b ? 1u : 0u;
  uint64_t low = ~uint64_t{0} << (r0 % 64);
  for (uint64_t k = r0 / 64; k < words; ++k) {
    const uint64_t wa = planeWord(pa, k);
    const uint64_t wb = planeWord(pb, k);
    const uint64_t valid = validMask(n, k) & low;
    const uint64_t xa = edgeMask(wa, carry_a, BITOSCOPE_EDGE_ANY) & valid;
    const uint64_t xb = edgeMask(wb, carry_b, BITOSCOPE_EDGE_ANY) & valid;
    if ((xa | xb) != 0) {
      const unsigned j = static_cast<unsigned>(std::countr_zero(xa | xb));
      *which = static_cast<uint32_t>(((xa >> j) & 1u) | (((xb >> j) & 1u) << 1));
      return k * 64 + j;
    }
    carry_a = wa >> 63;
    carry_b = wb >> 63;
    low = ~uint64_t{0};
  }
  return kNoSample;
}

inline uint64_t spanBits(const bitoscope_span& s, uint32_t channel, uint64_t r, uint32_t m) {
  const uint64_t mask = m >= 64 ? ~uint64_t{0} : (uint64_t{1} << m) - 1;
  if (s.planes == nullptr) return flatLevel(s, channel) ? mask : 0;
  const uint8_t* p = s.planes + channel * s.plane_stride;
  const uint64_t k = r / 64;
  const unsigned shift = static_cast<unsigned>(r % 64);
  uint64_t v = planeWord(p, k) >> shift;
  if (shift != 0 && shift + m > 64) v |= planeWord(p, k + 1) << (64 - shift);
  return v & mask;
}

inline uint64_t scannedWordsOf(uint64_t n, uint64_t r0, uint64_t found) {
  const uint64_t last = found != kNoSample ? found / 64 : (n - 1) / 64;
  return last - r0 / 64 + 1;
}

template <typename Get>
int nextEdgeAcross(Get& get, uint32_t channel, uint64_t from, int kind, uint64_t limit, uint64_t* at) {
  bitoscope_span s{};
  if (from >= limit || !get(from, s)) return BITOSCOPE_NONE;
  bool prev = spanLevel(s, channel, from - s.first_sample);
  uint64_t pos = from + 1;
  for (;;) {
    if (pos >= limit) return BITOSCOPE_NONE;
    if (pos - s.first_sample >= s.num_samples && !get(pos, s)) return BITOSCOPE_NONE;
    const uint64_t r0 = pos - s.first_sample;
    if (s.planes != nullptr) {
      // WHY: Search only up to limit: reading on to the next edge first makes limit-sized steps rescan an idle stretch.
      bitoscope_span cut;
      const bitoscope_span* view = &s;
      if (limit - s.first_sample < s.num_samples) {
        cut = s;
        cut.num_samples = limit - s.first_sample;
        view = &cut;
      }
      const uint64_t r = planeNextEdge(*view, channel, r0, prev, kind);
      get.noteWords(scannedWordsOf(view->num_samples, r0, r));
      if (r != kNoSample) {
        *at = s.first_sample + r;
        return BITOSCOPE_OK;
      }
      prev = spanLevel(s, channel, s.num_samples - 1);
    } else {
      const bool level = flatLevel(s, channel);
      if (r0 == 0 && level != prev && kindMatches(kind, level)) {
        *at = s.first_sample;
        return BITOSCOPE_OK;
      }
      prev = level;
    }
    pos = s.first_sample + s.num_samples;
  }
}

template <typename Get>
int nextChange2Across(Get& get, uint32_t a, uint32_t b, uint64_t from, uint64_t limit, uint64_t* at,
                      uint32_t* which) {
  bitoscope_span s{};
  if (from >= limit || !get(from, s)) return BITOSCOPE_NONE;
  bool prev_a = spanLevel(s, a, from - s.first_sample);
  bool prev_b = spanLevel(s, b, from - s.first_sample);
  uint64_t pos = from + 1;
  for (;;) {
    if (pos >= limit) return BITOSCOPE_NONE;
    if (pos - s.first_sample >= s.num_samples && !get(pos, s)) return BITOSCOPE_NONE;
    const uint64_t r0 = pos - s.first_sample;
    if (s.planes != nullptr) {
      bitoscope_span cut;
      const bitoscope_span* view = &s;
      if (limit - s.first_sample < s.num_samples) {
        cut = s;
        cut.num_samples = limit - s.first_sample;
        view = &cut;
      }
      const uint64_t r = planeNextChange2(*view, a, b, r0, prev_a, prev_b, which);
      get.noteWords(scannedWordsOf(view->num_samples, r0, r));
      if (r != kNoSample) {
        *at = s.first_sample + r;
        return BITOSCOPE_OK;
      }
      prev_a = spanLevel(s, a, s.num_samples - 1);
      prev_b = spanLevel(s, b, s.num_samples - 1);
    } else {
      const bool level_a = flatLevel(s, a);
      const bool level_b = flatLevel(s, b);
      if (r0 == 0 && (level_a != prev_a || level_b != prev_b)) {
        *at = s.first_sample;
        *which = (level_a != prev_a ? 1u : 0u) | (level_b != prev_b ? 2u : 0u);
        return BITOSCOPE_OK;
      }
      prev_a = level_a;
      prev_b = level_b;
    }
    pos = s.first_sample + s.num_samples;
  }
}

template <typename Get>
int bitsAcross(Get& get, uint32_t channel, uint64_t first, uint32_t n, uint64_t* value) {
  uint64_t v = 0;
  uint32_t got = 0;
  bitoscope_span s{};
  while (got < n) {
    const uint64_t pos = first + got;
    if (!get(pos, s)) return BITOSCOPE_NONE;
    const uint64_t r = pos - s.first_sample;
    const uint64_t left = s.num_samples - r;
    const uint32_t m = left < n - got ? static_cast<uint32_t>(left) : n - got;
    v |= spanBits(s, channel, r, m) << got;
    got += m;
  }
  *value = v;
  return BITOSCOPE_OK;
}

}

class StreamReader;
struct DecodeJob;
class DecodeInput;

namespace detail {
struct InputAccess {
  static int spanAt(DecodeInput& input, uint64_t sample, bitoscope_span& view);
  static int reportAt(DecodeInput& input, uint64_t position);
};
}

class DecodeInput {
 public:
  explicit DecodeInput(bitoscope_decode_input* in, bitoscope_annotations* out = nullptr)
      : in_(in), out_(out), scanned_(bitoscope_test_scan_counter(in)) {}
  DecodeInput(const DecodeInput&) = delete;
  DecodeInput& operator=(const DecodeInput&) = delete;

  uint32_t numChannels() const { return bitoscope_input_channels(in_); }
  struct SampleCount {
    uint64_t samples = 0;
    bool final = false;
  };
  SampleCount sampleCount() const;
  bool releaseBefore(uint64_t sample) { return bitoscope_release_before(in_, sample) == BITOSCOPE_OK; }
  std::optional<uint64_t> sampleAt(uint32_t channel, uint64_t start, uint64_t num, uint64_t den, uint64_t k0,
                                   uint32_t n);
  struct Clocked {
    uint32_t bits = 0;
    uint64_t value = 0;
    uint64_t end = 0;
  };
  Clocked clocked(uint32_t clock, uint32_t data, uint64_t from, Edge edge, uint32_t n, uint64_t limit);

  bool failed() const { return bitoscope_decode_error(in_) != nullptr; }
  bool stopped() const { return stopped_; }
  void noteScanned(uint64_t words) {
    if (scanned_ != nullptr) *scanned_ += words;
  }
  bool* stopFlag() { return &stopped_; }
  std::expected<StreamReader, std::string> input(const DecodeJob& job, std::string_view name);
  std::string error() const;
  void fail(std::string_view reason);

  template <typename Fn>
  void onProgress(Fn& fn) {
    if constexpr (std::is_invocable_v<Fn&, uint64_t>) {
      setProgress([](void* user, uint64_t position) -> uint64_t { return (*static_cast<Fn*>(user))(position); }, &fn);
    } else {
      setProgress([](void* user, uint64_t) -> uint64_t { return (*static_cast<Fn*>(user))(); }, &fn);
    }
  }

  bitoscope_decode_input* get() const { return in_; }

 private:
  friend struct detail::InputAccess;

  void setProgress(uint64_t (*ask)(void*, uint64_t), void* user);
  int progressSpanAt(uint64_t sample, bitoscope_span& view);
  int report(uint64_t position);
  int reportIfDue(uint64_t position) {
    return ask_ != nullptr && position >= nextReport_ ? report(position) : BITOSCOPE_OK;
  }

  bitoscope_decode_input* in_;
  bitoscope_annotations* out_ = nullptr;
  bool stopped_ = false;
  uint64_t* scanned_ = nullptr;
  uint64_t (*ask_)(void*, uint64_t) = nullptr;
  void* askUser_ = nullptr;
  uint64_t viewSamples_ = 0;
  uint64_t nextReport_ = UINT64_MAX;
};

inline int detail::InputAccess::spanAt(DecodeInput& input, uint64_t sample, bitoscope_span& view) {
  if (input.ask_ == nullptr) return bitoscope_span_at(input.in_, sample, &view);
  return input.progressSpanAt(sample, view);
}

inline int detail::InputAccess::reportAt(DecodeInput& input, uint64_t position) { return input.reportIfDue(position); }

class Cursor {
 public:
  explicit Cursor(DecodeInput& input) : input_(&input), in_(input.get()), channels_(input.numChannels()) {}

  bool level(uint32_t channel, uint64_t sample) {
    if (!usable(channel) || !hold(sample)) return false;
    return detail::spanLevel(span_, channel, sample - span_.first_sample);
  }
  std::optional<uint64_t> nextEdge(uint32_t channel, uint64_t from, Edge kind, uint64_t limit = UINT64_MAX) {
    if (!usable(channel)) return std::nullopt;
    Get get{this};
    uint64_t at = 0;
    if (detail::nextEdgeAcross(get, channel, from, static_cast<int>(kind), limit, &at) != BITOSCOPE_OK) {
      return std::nullopt;
    }
    return at;
  }
  struct Change {
    uint64_t at = 0;
    uint32_t which = 0;
  };
  std::optional<Change> nextChange2(uint32_t a, uint32_t b, uint64_t from, uint64_t limit = UINT64_MAX) {
    if (!usable(a) || !usable(b)) return std::nullopt;
    Get get{this};
    Change c;
    if (detail::nextChange2Across(get, a, b, from, limit, &c.at, &c.which) != BITOSCOPE_OK) return std::nullopt;
    return c;
  }
  std::optional<uint64_t> bits(uint32_t channel, uint64_t first, uint32_t n) {
    if (!usable(channel) || n > 64) {
      uint64_t ignored = 0;
      (void)bitoscope_bits(in_, channel, first, n, &ignored);
      return std::nullopt;
    }
    Get get{this};
    uint64_t value = 0;
    if (detail::bitsAcross(get, channel, first, n, &value) != BITOSCOPE_OK) return std::nullopt;
    return value;
  }

 private:
  struct Get {
    Cursor* self;
    bool operator()(uint64_t sample, bitoscope_span& out) const {
      if (!self->hold(sample)) return false;
      out = self->span_;
      return true;
    }
    void noteWords(uint64_t words) const { self->input_->noteScanned(words); }
  };

  bool usable(uint32_t channel) {
    if (input_->stopped()) return false;
    if (channel < channels_) return true;
    int ignored = 0;
    (void)bitoscope_level(in_, channel, 0, &ignored);
    return false;
  }
  bool hold(uint64_t sample) {
    if (sample - span_.first_sample < span_.num_samples && sample >= *span_.released_mark) return true;
    return detail::InputAccess::spanAt(*input_, sample, span_) == BITOSCOPE_OK;
  }

  DecodeInput* input_;
  bitoscope_decode_input* in_;
  uint32_t channels_;
  bitoscope_span span_{};
};

// IMPLEMENT: One line a forEachChange() watches: its channel (from DecodeJob::channel()) and the edges that count.
struct WatchedLine {
  uint32_t channel = 0;
  Edge edge = Edge::any;
};

// IMPLEMENT: One sample where a watched line has an edge of its kind, as forEachChange() hands it to you: line i is
// watched[i], and sampled[i] for sampled(i).
template <size_t W, size_t S>
class LineChange {
 public:
  LineChange(uint64_t at, unsigned bit, const std::array<uint64_t, W>& now, const std::array<uint64_t, W>& was,
             const std::array<uint64_t, W>& edges, const std::array<uint64_t, S>& data)
      : at_(at), bit_(bit), now_(&now), was_(&was), edges_(&edges), data_(&data) {}

  uint64_t at() const { return at_; }
  bool changed(size_t line) const { return (((*edges_)[line] >> bit_) & 1u) != 0; }
  bool before(size_t line) const { return (((*was_)[line] >> bit_) & 1u) != 0; }
  bool after(size_t line) const { return (((*now_)[line] >> bit_) & 1u) != 0; }
  bool sampled(size_t line) const { return (((*data_)[line] >> bit_) & 1u) != 0; }

 private:
  uint64_t at_;
  unsigned bit_;
  const std::array<uint64_t, W>* now_;
  const std::array<uint64_t, W>* was_;
  const std::array<uint64_t, W>* edges_;
  const std::array<uint64_t, S>* data_;
};

namespace detail {

inline uint32_t levelsAt(const bitoscope_span& s, const uint32_t* channels, size_t n, uint64_t r) {
  uint32_t bits = 0;
  for (size_t i = 0; i < n; ++i) bits |= (spanLevel(s, channels[i], r) ? 1u : 0u) << i;
  return bits;
}

struct CApiWalk {
  explicit CApiWalk(bitoscope_decode_input* input) : in(input), scanned(bitoscope_test_scan_counter(input)) {}
  bitoscope_decode_input* in;
  uint64_t* scanned;
  void noteWords(uint64_t words) const {
    if (scanned != nullptr) *scanned += words;
  }
  uint32_t numChannels() const { return bitoscope_input_channels(in); }
  bool stopped() const { return false; }
  int spanAt(uint64_t sample, bitoscope_span& s) const { return bitoscope_span_at(in, sample, &s); }
  int release(uint64_t end) const { return bitoscope_release_before(in, end); }
  void badChannel(uint32_t channel) const {
    int ignored = 0;
    (void)bitoscope_level(in, channel, 0, &ignored);
  }
};

struct InputWalk {
  DecodeInput& input;
  uint32_t numChannels() const { return input.numChannels(); }
  bool stopped() const { return input.stopped(); }
  void noteWords(uint64_t words) const { input.noteScanned(words); }
  int spanAt(uint64_t sample, bitoscope_span& s) const { return InputAccess::spanAt(input, sample, s); }
  int release(uint64_t end) const { return bitoscope_release_before(input.get(), end); }
  void badChannel(uint32_t channel) const {
    int ignored = 0;
    (void)bitoscope_level(input.get(), channel, 0, &ignored);
  }
};

template <size_t MaxW, size_t MaxS, bool Dynamic, typename Env, typename Start, typename On>
std::optional<uint64_t> walkChanges(Env& env, const std::array<WatchedLine, MaxW>& watched, size_t watchedCount,
                                    const std::array<uint32_t, MaxS>& sampled, size_t sampledCount, uint64_t from,
                                    uint32_t previous, Start&& start, On&& on) {
  static_assert(MaxW >= 1 && MaxW <= 32 && MaxS <= 32, "a change walk watches 1 to 32 lines and samples at most 32");
  const size_t w = Dynamic ? watchedCount : MaxW;
  const size_t sn = Dynamic ? sampledCount : MaxS;
  std::array<uint32_t, MaxW> lines{};
  std::array<uint64_t, MaxW> rises{};
  std::array<uint64_t, MaxW> falls{};
  for (size_t i = 0; i < w; ++i) {
    lines[i] = watched[i].channel;
    rises[i] = watched[i].edge == Edge::falling ? 0 : ~uint64_t{0};
    falls[i] = watched[i].edge == Edge::rising ? 0 : ~uint64_t{0};
  }
  for (size_t i = 0; i < w; ++i) {
    if (lines[i] >= env.numChannels()) {
      env.badChannel(lines[i]);
      return std::nullopt;
    }
  }
  for (size_t i = 0; i < sn; ++i) {
    if (sampled[i] >= env.numChannels()) {
      env.badChannel(sampled[i]);
      return std::nullopt;
    }
  }
  bitoscope_span s{};
  if (env.spanAt(from, s) != BITOSCOPE_OK) return std::nullopt;
  uint32_t levels = previous;
  if (from == 0) {
    levels = levelsAt(s, lines.data(), w, 0);
    if (!start(levels)) return std::nullopt;
  }
  std::array<uint64_t, MaxW> now{};
  std::array<uint64_t, MaxW> was{};
  std::array<uint64_t, MaxW> edges{};
  std::array<uint64_t, MaxS> data{};
  uint64_t end = 0;
  for (;;) {
    const uint64_t base = s.first_sample;
    const uint64_t n = s.num_samples;
    const uint64_t r0 = from > base ? from - base : 0;
    if (s.planes == nullptr) {
      const uint32_t flat = levelsAt(s, lines.data(), w, 0);
      uint64_t events = 0;
      for (size_t i = 0; i < w; ++i) {
        now[i] = (flat >> i) & 1u;
        was[i] = (levels >> i) & 1u;
        edges[i] = (now[i] ^ was[i]) & ((now[i] & rises[i]) | (~now[i] & falls[i])) & 1u;
        events |= edges[i];
      }
      if (base > 0 && events != 0) {
        const uint32_t flatData = levelsAt(s, sampled.data(), sn, 0);
        for (size_t m = 0; m < sn; ++m) data[m] = (flatData >> m) & 1u;
        if (!on(LineChange<MaxW, MaxS>(base, 0, now, was, edges, data)) || env.stopped()) return std::nullopt;
      }
      levels = flat;
    } else {
      std::array<const uint8_t*, MaxW> planes{};
      for (size_t i = 0; i < w; ++i) planes[i] = s.planes + uint64_t{lines[i]} * s.plane_stride;
      std::array<const uint8_t*, MaxS> dataPlanes{};
      for (size_t m = 0; m < sn; ++m) dataPlanes[m] = s.planes + uint64_t{sampled[m]} * s.plane_stride;
      std::array<uint64_t, MaxW> carry{};
      for (size_t i = 0; i < w; ++i) carry[i] = (levels >> i) & 1u;
      const uint64_t words = (n + 63) / 64;
      const uint64_t firstWord = r0 / 64;
      uint64_t low = ~uint64_t{0} << (r0 % 64);
      const auto flatAt = [&planes, &carry, w](uint64_t k) {
        for (size_t i = 0; i < w; ++i) {
          if (planeWord(planes[i], k) != 0 - carry[i]) return false;
        }
        return true;
      };
      for (uint64_t k = firstWord; k < words; ++k) {
        while (k < words && flatAt(k)) {
          ++k;
          low = ~uint64_t{0};
        }
        if (k == words) {
          for (size_t i = 0; i < w; ++i) now[i] = 0 - carry[i];
          break;
        }
        const uint64_t valid = validMask(n, k) & low;
        low = ~uint64_t{0};
        uint64_t events = 0;
        for (size_t i = 0; i < w; ++i) {
          now[i] = planeWord(planes[i], k);
          was[i] = (now[i] << 1) | carry[i];
          edges[i] = (now[i] ^ was[i]) & ((now[i] & rises[i]) | (~now[i] & falls[i])) & valid;
          events |= edges[i];
        }
        if (events != 0) {
          for (size_t m = 0; m < sn; ++m) data[m] = planeWord(dataPlanes[m], k);
          do {
            const unsigned j = static_cast<unsigned>(std::countr_zero(events));
            events &= events - 1;
            if (!on(LineChange<MaxW, MaxS>(base + k * 64 + j, j, now, was, edges, data)) || env.stopped()) {
              return std::nullopt;
            }
          } while (events != 0);
        }
        for (size_t i = 0; i < w; ++i) carry[i] = now[i] >> 63;
      }
      env.noteWords((words - firstWord) * w);
      const unsigned last = static_cast<unsigned>((n - 1) % 64);
      uint32_t carried = 0;
      for (size_t i = 0; i < w; ++i) carried |= static_cast<uint32_t>((now[i] >> last) & 1u) << i;
      levels = carried;
    }
    end = base + n;
    if (env.release(end) != BITOSCOPE_OK) return std::nullopt;
    const int got = env.spanAt(end, s);
    if (got == BITOSCOPE_NONE) break;
    if (got != BITOSCOPE_OK) return std::nullopt;
  }
  return end;
}

}

template <size_t W, size_t S, typename Start, typename On>
std::optional<uint64_t> forEachChange(DecodeInput& input, const std::array<WatchedLine, W>& watched,
                                      const std::array<uint32_t, S>& sampled, Start&& start, On&& on) {
  detail::InputWalk env{input};
  return detail::walkChanges<W, S, false>(env, watched, W, sampled, S, 0, 0, std::forward<Start>(start),
                                          std::forward<On>(on));
}

using StreamItem = bitoscope_stream_item;
inline constexpr uint32_t kItemData = BITOSCOPE_ITEM_DATA;
inline constexpr uint32_t kItemBoundary = BITOSCOPE_ITEM_BOUNDARY;
inline constexpr uint32_t kItemBegin = BITOSCOPE_ITEM_BEGIN;
inline constexpr uint32_t kItemDamaged = BITOSCOPE_ITEM_FLAG_DAMAGED;
inline constexpr uint32_t kItemNoMosi = BITOSCOPE_ITEM_FLAG_NO_MOSI;
inline constexpr uint32_t kItemNoMiso = BITOSCOPE_ITEM_FLAG_NO_MISO;

class StreamWriter {
 public:
  StreamWriter() = default;
  StreamWriter(bitoscope_annotations* out, uint32_t index, bool* stop = nullptr) : out_(out), index_(index), stop_(stop) {}
  explicit operator bool() const { return out_ != nullptr; }
  uint32_t index() const { return index_; }
  bool put(const StreamItem& item) const {
    if (out_ == nullptr) return true;
    if (bitoscope_put(out_, index_, item.begin, item.end, item.kind, item.value, item.flags) == BITOSCOPE_OK) return true;
    if (stop_ != nullptr) *stop_ = true;
    return false;
  }
  bool put(uint64_t begin, uint64_t end, uint32_t kind, uint64_t value = 0, uint32_t flags = 0) const {
    return put(StreamItem{begin, end, value, kind, flags});
  }

 private:
  bitoscope_annotations* out_ = nullptr;
  uint32_t index_ = 0;
  bool* stop_ = nullptr;
};

class StreamReader {
 public:
  StreamReader() = default;
  StreamReader(bitoscope_decode_input* in, uint32_t index, DecodeInput* progress = nullptr)
      : in_(in), index_(index), progress_(progress) {}
  explicit operator bool() const { return in_ != nullptr; }
  uint32_t index() const { return index_; }
  std::optional<StreamItem> next() const {
    if (in_ == nullptr || (progress_ != nullptr && progress_->stopped())) return std::nullopt;
    StreamItem item{};
    if (bitoscope_next_item(in_, index_, &item) != BITOSCOPE_OK) return std::nullopt;
    if (progress_ != nullptr && detail::InputAccess::reportAt(*progress_, item.begin) != BITOSCOPE_OK) {
      return std::nullopt;
    }
    return item;
  }

 private:
  bitoscope_decode_input* in_ = nullptr;
  uint32_t index_ = 0;
  DecodeInput* progress_ = nullptr;
};

class Annotations {
 public:
  explicit Annotations(bitoscope_annotations* out, bool* stop = nullptr)
      : out_(out), stop_(stop), room_(bitoscope_emit_room(out)) {}
  bool emit(uint64_t begin, uint64_t end, uint32_t row, uint32_t cls, uint64_t value) {
    if (end <= begin) return noted(bitoscope_emit(out_, begin, end, row, cls, value));
    if (room_->next == room_->limit && !noted(bitoscope_emit_commit(out_))) return false;
    *room_->next++ = bitoscope_annotation{begin, end, row, cls, value};
    return true;
  }
  bool emitBytes(uint64_t begin, uint64_t end, uint32_t row, uint32_t cls, std::span<const uint8_t> bytes) {
    return noted(bitoscope_emit_bytes(out_, begin, end, row, cls, bytes.data(), bytes.size()));
  }
  bool settle(uint64_t sample) { return noted(bitoscope_settle(out_, sample)); }
  std::expected<StreamWriter, std::string> output(const DecodeJob& job, std::string_view name) const;
  bitoscope_annotations* get() const { return out_; }

 private:
  bool noted(int status) {
    if (status == BITOSCOPE_OK) return true;
    if (stop_ != nullptr) *stop_ = true;
    return false;
  }

  bitoscope_annotations* out_;
  bool* stop_;
  bitoscope_emit_cursor* room_;
};

enum class OptionType : int { string = BITOSCOPE_OPTION_STRING, integer = BITOSCOPE_OPTION_INTEGER };
enum class ValueFormat : int {
  none = BITOSCOPE_VALUE_NONE,
  hex = BITOSCOPE_VALUE_HEX,
  address7 = BITOSCOPE_VALUE_ADDRESS7,
  nOfM = BITOSCOPE_VALUE_N_OF_M,
  decimal = BITOSCOPE_VALUE_DECIMAL,
  nOfMBytes = BITOSCOPE_VALUE_N_OF_M_BYTES,
  bytes = BITOSCOPE_VALUE_BYTES,
};
enum class StreamType : int { words = BITOSCOPE_STREAM_WORDS, spiTransfers = BITOSCOPE_STREAM_SPI_TRANSFERS };
enum class AnnotationRole : int {
  none = BITOSCOPE_ROLE_NONE,
  data = BITOSCOPE_ROLE_DATA,
  start = BITOSCOPE_ROLE_START,
  stop = BITOSCOPE_ROLE_STOP,
  ok = BITOSCOPE_ROLE_OK,
  error = BITOSCOPE_ROLE_ERROR,
  control = BITOSCOPE_ROLE_CONTROL,
  address = BITOSCOPE_ROLE_ADDRESS,
  bit = BITOSCOPE_ROLE_BIT,
  info = BITOSCOPE_ROLE_INFO,
};

struct DecoderChannel {
  std::string name{};
  std::string description{};
  bool optional = false;
  std::string display{};
};

struct DecoderOutput {
  std::string name{};
  std::string description{};
  StreamType type = StreamType::words;
  uint32_t width = 0;
  std::string widthOption{};      // IMPLEMENT: an integer option's name: the width is its value; empty: `width`
  std::string requiresChannel{};  // IMPLEMENT: a channel that must be bound for the output to exist; empty: none
  std::string display{};
  std::string carries{};       // IMPLEMENT: the protocol its items carry; empty: none
  std::string carriesInput{};  // IMPLEMENT: an input of this decoder whose protocol it passes on; empty: none
};

struct DecoderInput {
  std::string name{};
  std::string description{};
  StreamType type = StreamType::words;
  uint32_t width = 0;  // IMPLEMENT: 0: any
  bool optional = false;
  std::string display{};
};

struct DecoderOption {
  std::string name{};
  std::string description{};
  OptionType type = OptionType::string;
  std::string defaultValue{};
  std::vector<std::string> allowedValues{};  // IMPLEMENT: empty: any
  std::optional<int64_t> min{};              // IMPLEMENT: an integer option's limits, inclusive
  std::optional<int64_t> max{};
  std::vector<std::string> unsupportedValues{};
  std::string display{};
};

struct AnnotationClass {
  uint32_t id = 0;
  std::string name{};
  ValueFormat format = ValueFormat::none;
  uint32_t hexDigits = 0;
  std::string hexDigitsOption{};  // IMPLEMENT: an integer option's name: (its value + 3) / 4 digits; empty: hexDigits
  AnnotationRole role = AnnotationRole::none;
};

struct AnnotationRow {
  uint32_t id = 0;
  std::string name{};
};

enum class RowRule : int {
  wordList = BITOSCOPE_ROW_RULE_WORD_LIST,
  bitGrid = BITOSCOPE_ROW_RULE_BIT_GRID,
  clockEdges = BITOSCOPE_ROW_RULE_CLOCK_EDGES,
};

struct BitGrid {
  std::string rateOption{};
  uint32_t offsetBits = 0;
  std::string countOption{};
  std::string orderOption{};
};

struct ClockEdges {
  std::string clockChannel{};
  std::string dataChannel{};
  std::string countOption{};  // IMPLEMENT: empty: `count`
  uint32_t count = 0;
};

// IMPLEMENT: A row whose content comes from another row's annotations (bitoscope_row_source): `grid` is read for a bitGrid
// rule only, `edges` for a clockEdges rule only.
struct RowSource {
  uint32_t row = 0;
  uint32_t cls = 0;
  uint32_t sourceRow = 0;
  std::vector<uint32_t> sourceClasses{};
  std::string enableOption{};  // IMPLEMENT: "yes"/"no" option: row wanted or not; empty: always
  RowRule rule = RowRule::wordList;
  BitGrid grid{};
  ClockEdges edges{};
};

struct DecoderDeclaration {
  std::string name{};
  std::string version{};  // IMPLEMENT: required, X.Y.Z with an optional suffix ("1.0.0", "1.2.3-beta")
  std::vector<DecoderChannel> channels{};
  std::vector<DecoderOption> options{};
  std::vector<std::string> unsupportedOptions{};
  std::vector<AnnotationClass> classes{};
  std::vector<AnnotationRow> rows{};
  bool allowsSharedChannels = false;
  std::vector<DecoderOutput> outputs{};  // IMPLEMENT: streams it writes
  std::vector<DecoderInput> inputs{};    // IMPLEMENT: streams it reads, in place of channels or beside them
  std::string display{};
  // IMPLEMENT: The decoders whose output this one is designed to read, by their names (bitoscope_decoder.protocols):
  // {"spi", "qspi"} for a flash decoder. Empty: any. A name no decoder of the host has is no error.
  std::vector<std::string> protocols{};
  std::vector<RowSource> rowSources{};
};

struct DecodeJob {
  struct Channel {
    std::string name;
    uint32_t input = 0;
    uint32_t capture = 0;
  };
  uint64_t id = 0;
  std::string decoder;
  std::vector<Channel> channels;
  std::map<std::string, std::string, std::less<>> options;
  uint64_t sampleRateMilliHz = 0;
  uint32_t captureChannels = 0;
  uint64_t totalSamples = 0;
  // IMPLEMENT: As total_is_bound (bitoscope_decoder.h): true, totalSamples is the most a capture still being made can
  // hold; its real end is where the input ends (DecodeInput::sampleCount() says final).
  bool totalIsBound = false;
  uint64_t progressStep = 0;
  struct Output {
    std::string name;
    StreamType type = StreamType::words;
    uint32_t width = 0;
    bool wanted = false;
  };
  std::vector<Output> outputs;
  struct Input {
    std::string name;
    StreamType type = StreamType::words;
    uint32_t width = 0;
    bool bound = false;
  };
  std::vector<Input> inputs;

  DecodeJob() = default;
  explicit DecodeJob(const bitoscope_decode_job& job);

  std::optional<uint32_t> channel(std::string_view name) const;
  std::string_view option(std::string_view name) const;
  std::optional<int64_t> optionInt(std::string_view name) const;
};

class ProtocolDecoder {
 public:
  virtual ~ProtocolDecoder() = default;

  // IMPLEMENT: What the decoder is, asked once when it is added (bitoscope_decoder's fields).
  virtual DecoderDeclaration declaration() const = 0;

  // IMPLEMENT: Optional: the decoder's own refusals that depend on the capture (bitoscope_check_fn), for a job the
  // SDK has already checked against the declaration. On the SDK's main thread, possibly while decode()
  // of another job runs: it must not touch what decode() changes. The error is the reason the host
  // shows. The default accepts every job.
  virtual std::expected<void, std::string> check(const DecodeJob& job) const;

  // IMPLEMENT: Optional: the most samples, of one bound channel, decode() may need in its input's window at once for
  // this job - from the released mark to the furthest sample it asks for - for a job check() accepted
  // (bitoscope_reply_look_ahead_samples()). The host sizes the job's decode ring from it, or refuses the job before
  // the capture starts. Same thread rules as check(). The default, 0, leaves the host's default ring.
  virtual uint64_t lookAheadSamples(const DecodeJob& job) const;

  // IMPLEMENT: Decodes the job's input into `out`. An error fails the job with it as the reason; so does an
  // exception, with its what(). A job the input or an emit failed has failed whatever this returns. For progress
  // while the job runs, call out.settle(sample) as bitoscope_decode_fn says, or give input.onProgress(fn) a
  // function that answers that sample: the C++ layer then settles from it as its reads move on. fn may take the
  // sample the reads have reached (every change forEachChange() finds before it is handed to you already): a decoder
  // whose state moves only at a change moves it there first, so a line with no change still settles.
  virtual std::expected<void, std::string> decode(const DecodeJob& job, DecodeInput& input, Annotations& out) = 0;

  static int run(ProtocolDecoder& decoder, const DecodeJob& job, bitoscope_decode_input* in,
                 bitoscope_annotations* out);
};

class Decoders {
 public:
  bool add(std::unique_ptr<ProtocolDecoder> decoder);

  void setPluginVersion(std::string_view version);
  void setRunsScripts(bool runsScripts);

  bitoscope_decoders* get() const { return decoders_; }

 private:
  friend class DecoderPlugin;
  Decoders(bitoscope_decoders* decoders, std::vector<std::unique_ptr<ProtocolDecoder>>& keep)
      : decoders_(decoders), keep_(&keep) {}

  bitoscope_decoders* decoders_;
  std::vector<std::unique_ptr<ProtocolDecoder>>* keep_;
};

// IMPLEMENT: Adds every decoder and sets the plugin's version with Decoders::setPluginVersion() (required, in the form
// of bitoscope_list_decoders_fn); an error means the plugin could not list them (the host is told).
using DecoderListFunction = std::function<std::expected<void, std::string>(Decoders&)>;

class DecoderPlugin {
 public:
  explicit DecoderPlugin(DecoderListFunction list) : list_(std::move(list)) {}
  DecoderPlugin(const DecoderPlugin&) = delete;
  DecoderPlugin& operator=(const DecoderPlugin&) = delete;

  int run(int argc, char** argv);

  static int list(void* user, bitoscope_decoders* decoders);

 private:
  DecoderListFunction list_;
  std::vector<std::unique_ptr<ProtocolDecoder>> decoders_;
};

int runDecoderPlugin(int argc, char** argv, DecoderListFunction list);

}

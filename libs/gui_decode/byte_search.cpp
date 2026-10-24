/*
 * SPDX-License-Identifier: MIT
 */

#include "byte_search.h"

#include <algorithm>
#include <array>
#include <bit>
#include <charconv>
#include <cstring>
#include <new>
#include <string>

namespace bitoscope::gui_decode {

namespace {

namespace wv = bitoscope::waveform_view;

constexpr std::uint16_t kNotAByte = 256;
constexpr std::size_t kBytesBetweenStopChecks = 1 << 16;
constexpr std::size_t kBlocksBetweenStopChecks = kBytesBetweenStopChecks / wv::kBlockEntries;
constexpr int kByteBits = 8;
constexpr std::size_t kSymbolTableSize = 512;

std::uint16_t symbolOf(std::uint64_t value, bool caseSensitive) {
  if (value > 0xFF) return kNotAByte;
  if (!caseSensitive && value >= 'A' && value <= 'Z') return static_cast<std::uint16_t>(value + ('a' - 'A'));
  return static_cast<std::uint16_t>(value);
}

template <unsigned Width>
void unpackWidth(const std::uint8_t* bits, std::size_t count, const std::uint16_t* table, std::uint16_t* out) {
  constexpr std::uint64_t mask = (std::uint64_t{1} << Width) - 1;
  constexpr std::size_t perLoad = 57 / Width;
  for (std::size_t i = 0, bit = 0; i < count; i += perLoad, bit += perLoad * Width) {
    std::uint64_t word;
    std::memcpy(&word, bits + (bit >> 3), sizeof(word));
    word >>= bit & 7;
    const std::size_t take = (std::min)(perLoad, count - i);
    for (std::size_t e = 0; e < take; ++e) out[i + e] = table[(word >> (e * Width)) & mask];
  }
}

class PatternMatcher {
 public:
  PatternMatcher(std::span<const std::uint8_t> pattern, bool caseSensitive)
      : caseSensitive_(caseSensitive), failure_(pattern.size(), 0), begins_(std::bit_ceil(pattern.size()), 0),
        mask_(begins_.size() - 1) {
    symbols_.reserve(pattern.size());
    for (const std::uint8_t b : pattern) symbols_.push_back(symbolOf(b, caseSensitive));
    for (std::size_t i = 1, k = 0; i < symbols_.size(); ++i) {
      while (k > 0 && symbols_[i] != symbols_[k]) k = failure_[k - 1];
      if (symbols_[i] == symbols_[k]) ++k;
      failure_[i] = k;
    }
    for (std::size_t v = 0; v < table_.size(); ++v) table_[v] = symbolOf(v, caseSensitive);
  }

  std::size_t size() const { return symbols_.size(); }

  void unpack(const wv::BlockField& values, std::size_t count, std::uint16_t* out) const {
    const unsigned width = values.width;
    if (width == 0) {
      std::fill_n(out, count, symbolOf(values.min, caseSensitive_));
      return;
    }
    if (width <= kByteBits && values.min <= kSymbolTableSize - (std::uint64_t{1} << width)) {
      const std::uint16_t* table = table_.data() + values.min;
      switch (width) {
        case 1: unpackWidth<1>(values.bits, count, table, out); return;
        case 2: unpackWidth<2>(values.bits, count, table, out); return;
        case 3: unpackWidth<3>(values.bits, count, table, out); return;
        case 4: unpackWidth<4>(values.bits, count, table, out); return;
        case 5: unpackWidth<5>(values.bits, count, table, out); return;
        case 6: unpackWidth<6>(values.bits, count, table, out); return;
        case 7: unpackWidth<7>(values.bits, count, table, out); return;
        default: unpackWidth<8>(values.bits, count, table, out); return;
      }
    }
    for (std::size_t t = 0; t < count; ++t) out[t] = symbolOf(wv::fieldEntry(values, t), caseSensitive_);
  }

  template <class Matched>
  void feed(const std::uint16_t* symbols, std::size_t count, Matched matched) {
    const std::uint16_t first = symbols_[0];
    for (std::size_t j = 0; j < count; ++j) {
      if (state_ == 0) {
        while (j < count && symbols[j] != first) ++j;
        if (j == count) return;
      }
      const std::uint16_t symbol = symbols[j];
      while (state_ > 0 && symbols_[state_] != symbol) state_ = failure_[state_ - 1];
      if (symbols_[state_] == symbol) ++state_;
      if (state_ == symbols_.size()) {
        state_ = failure_[state_ - 1];
        matched(j);
      }
    }
  }

  void restart() {
    state_ = 0;
    fed_ = 0;
  }

  bool completes(std::uint64_t value, std::uint64_t begin) {
    const std::uint16_t symbol = symbolOf(value, caseSensitive_);
    while (state_ > 0 && symbols_[state_] != symbol) state_ = failure_[state_ - 1];
    if (symbols_[state_] == symbol) ++state_;
    begins_[fed_ & mask_] = begin;
    ++fed_;
    if (state_ < symbols_.size()) return false;
    state_ = failure_[state_ - 1];
    return true;
  }

  std::uint64_t matchBegin() const { return begins_[(fed_ - symbols_.size()) & mask_]; }

 private:
  bool caseSensitive_;
  std::vector<std::uint16_t> symbols_;
  std::vector<std::size_t> failure_;
  std::vector<std::uint64_t> begins_;
  std::size_t mask_;
  std::array<std::uint16_t, kSymbolTableSize> table_{};
  std::size_t state_ = 0;
  std::size_t fed_ = 0;
};

bool scanOneClass(wv::AnnotationReader& reader, std::uint32_t row, std::size_t g, PatternMatcher& matcher,
                  std::vector<ByteMatch>& out, const std::stop_token& stop) {
  matcher.restart();
  const std::size_t n = reader.groups().groupSize(g);
  const std::size_t length = matcher.size();
  std::array<std::uint16_t, wv::kBlockEntries> symbols{};
  wv::BlockCursor first;
  wv::BlockCursor last;
  for (std::size_t k = 0, from = 0; from < n; ++k, from += wv::kBlockEntries) {
    if (k % kBlocksBetweenStopChecks == 0 && stop.stop_requested()) return false;
    const std::size_t count = (std::min)(wv::kBlockEntries, n - from);
    matcher.unpack(reader.blockValues(g, k), count, symbols.data());
    matcher.feed(symbols.data(), count, [&](std::size_t j) {
      const std::size_t i = from + j;
      out.push_back({reader.begin(g, i + 1 - length, first), reader.begin(g, i, last) + reader.length(g, i, last), row});
    });
  }
  return true;
}

struct ClassStream {
  std::size_t group = 0;
  std::size_t next = 0;
  std::size_t size = 0;
  std::uint64_t head = 0;
  bool split = false;
  wv::BlockCursor cursor;
};

bool scanRow(wv::AnnotationReader& reader, const ByteRow& row, PatternMatcher& matcher, std::vector<ByteMatch>& out,
             const std::stop_token& stop) {
  matcher.restart();
  std::vector<ClassStream> streams;
  const auto add = [&](std::size_t g, bool split) {
    ClassStream s;
    s.group = g;
    s.size = reader.groups().groupSize(g);
    s.head = reader.begin(g, 0, s.cursor);
    s.split = split;
    streams.push_back(s);
  };
  for (const std::size_t g : row.groups) add(g, false);
  for (const std::size_t g : row.splits) add(g, true);
  std::size_t untilStopCheck = 1;
  while (!streams.empty()) {
    if (--untilStopCheck == 0) {
      if (stop.stop_requested()) return false;
      untilStopCheck = kBytesBetweenStopChecks;
    }
    std::size_t pick = 0;
    for (std::size_t k = 1; k < streams.size(); ++k) {
      if (streams[k].head < streams[pick].head) pick = k;
    }
    ClassStream& s = streams[pick];
    const std::size_t i = s.next;
    const std::uint64_t begin = s.head;
    if (s.split) {
      matcher.restart();
    } else if (matcher.completes(reader.value(s.group, i, s.cursor), begin)) {
      out.push_back({matcher.matchBegin(), begin + reader.length(s.group, i, s.cursor), row.row});
    }
    if (++s.next < s.size) {
      s.head = reader.begin(s.group, s.next, s.cursor);
    } else {
      streams.erase(streams.begin() + static_cast<std::ptrdiff_t>(pick));
    }
  }
  return true;
}

}

bool holdsBytes(const decode::AnnotationClass& cls, const decode::OptionMap& options) {
  if (cls.format.kind != decode::ValueFormatKind::Hex) return false;
  if (cls.format.hex_digits_option.empty()) return cls.format.hex_min_digits == 2;
  const auto found = options.find(cls.format.hex_digits_option);
  if (found == options.end()) return false;
  const std::string& text = found->second;
  int bits = 0;
  const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), bits);
  return error == std::errc{} && end == text.data() + text.size() && bits == kByteBits;
}

std::vector<ByteRow> byteRows(const ShownResult& shown) {
  std::vector<ByteRow> rows;
  if (!shown.annotations) return rows;
  const wv::AnnotationGroups& groups = shown.annotations->groups();
  for (const std::uint32_t row : groups.rowIds()) {
    ByteRow found{row, {}, {}};
    bool framed = false;
    const auto [first, last] = groups.rowGroups(row);
    for (std::size_t g = first; g < last; ++g) {
      const auto cls = std::find_if(shown.format.classes.begin(), shown.format.classes.end(),
                                    [&](const decode::AnnotationClass& c) { return c.id == groups.groupClass(g); });
      if (cls == shown.format.classes.end()) continue;
      if (holdsBytes(*cls, shown.format.options)) found.groups.push_back(g);
      if (cls->role == decode::AnnotationRole::Start) framed = true;
      if (cls->role == decode::AnnotationRole::Stop) found.splits.push_back(g);
    }
    if (found.groups.empty()) continue;
    // WHY: Start and Stop around every byte (UART) frame it; a lone Stop (COBS delimiter) ends a packet.
    if (framed) found.splits.clear();
    rows.push_back(std::move(found));
  }
  return rows;
}

std::expected<std::vector<ByteMatch>, ByteSearchFailure> findByteMatches(const wv::AnnotationGroups& groups,
                                                                         std::span<const ByteRow> rows,
                                                                         std::span<const std::uint8_t> pattern,
                                                                         bool caseSensitive, std::stop_token stop) {
  std::vector<ByteMatch> found;
  if (pattern.empty()) return found;
  try {
    wv::AnnotationReader reader(groups);
    PatternMatcher matcher(pattern, caseSensitive);
    for (const ByteRow& row : rows) {
      const bool scanned = row.groups.size() == 1 && row.splits.empty()
                             ? scanOneClass(reader, row.row, row.groups[0], matcher, found, stop)
                             : scanRow(reader, row, matcher, found, stop);
      if (!scanned) return std::unexpected(ByteSearchFailure::Stopped);
    }
  } catch (const std::bad_alloc&) {
    return std::unexpected(ByteSearchFailure::OutOfMemory);
  }
  return found;
}

}

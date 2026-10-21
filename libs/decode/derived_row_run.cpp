/*
 * SPDX-License-Identifier: MIT
 */

#include "derived_row_run.h"

#include <algorithm>
#include <cstddef>
#include <compare>
#include <cstdint>
#include <format>
#include <span>
#include <tuple>
#include <utility>
#include <vector>

namespace bitoscope::decode {

namespace {

bool contains(const std::vector<std::string>& names, std::string_view name) {
  return std::find(names.begin(), names.end(), name) != names.end();
}

struct RowToMake {
  const RowSource* source = nullptr;
  std::expected<ResolvedRule, std::string> rule;
  std::string name;
};

bool takesWordsFrom(const RowSource& source, const Annotation& a) {
  return a.row == source.source_row &&
         std::find(source.source_classes.begin(), source.source_classes.end(), a.cls) != source.source_classes.end();
}

bool onRowOf(std::span<const RowSource> rows, const Annotation& a) {
  return std::any_of(rows.begin(), rows.end(), [&a](const RowSource& s) { return a.row == s.row; });
}

uint32_t bitsPerWord(const ResolvedRule& rule) {
  if (const auto* grid = std::get_if<BitGrid>(&rule)) return grid->count;
  return std::get<ClockEdges>(rule).count;
}

std::string rowName(std::span<const AnnotationRow> names, uint32_t row) {
  for (const AnnotationRow& r : names) {
    if (r.id == row && !r.name.empty()) return r.name;
  }
  return std::format("row {}", row);
}

std::expected<void, std::string> appendBits(const RowToMake& row, const Annotation& word, const SignalView& view,
                                            std::vector<Annotation>& out) {
  const WordSpan span{word.begin, word.end, word.value};
  const auto put = [&](const DerivedBit& b) {
    out.push_back({b.begin, b.end, row.source->row, row.source->cls, b.value});
  };
  const std::expected<void, std::string> made =
      std::holds_alternative<BitGrid>(*row.rule) ? forEachGridBit(span, std::get<BitGrid>(*row.rule), put)
                                                 : forEachEdgeBit(span, std::get<ClockEdges>(*row.rule), view, put);
  if (!made) return std::unexpected(row.name + ": " + made.error());
  return {};
}

std::vector<RowToMake> rowsToMake(std::span<const RowSource> rows, std::span<const AnnotationRow> names,
                                  const OptionMap& options, const ChannelMap& channels, const SignalView& view) {
  std::vector<RowToMake> made;
  made.reserve(rows.size());
  for (const RowSource& source : rows) {
    made.push_back({&source, resolveRowRule(source, options, channels, view.sampleRateMilliHz()),
                    rowName(names, source.row)});
  }
  return made;
}

std::expected<std::vector<std::size_t>, std::string> countBits(const std::vector<Annotation>& annotations,
                                                               const std::vector<RowToMake>& made) {
  std::vector<std::size_t> bits(made.size(), 0);
  for (const Annotation& a : annotations) {
    for (std::size_t r = 0; r < made.size(); ++r) {
      if (!takesWordsFrom(*made[r].source, a)) continue;
      if (!made[r].rule) return std::unexpected(made[r].name + ": " + made[r].rule.error());
      bits[r] += bitsPerWord(*made[r].rule);
    }
  }
  return bits;
}

struct RowKey {
  uint32_t anchor = 0;
  uint32_t depth = 0;
  uint32_t row = 0;
  auto operator<=>(const RowKey&) const = default;
};

RowKey rowKey(uint32_t row, std::span<const RowSource> rows) {
  RowKey key{row, 0, row};
  uint32_t at = row;
  for (std::size_t step = 0; step < rows.size(); ++step) {
    const auto source = std::find_if(rows.begin(), rows.end(), [at](const RowSource& s) { return s.row == at; });
    if (source == rows.end()) break;
    at = source->source_row;
    key.anchor = (std::max)(key.anchor, at);
    ++key.depth;
  }
  return key;
}

void putInBeginOrder(std::vector<Annotation>& items) {
  const auto byBegin = [](const Annotation& a, const Annotation& b) { return a.begin < b.begin; };
  if (!std::is_sorted(items.begin(), items.end(), byBegin)) std::stable_sort(items.begin(), items.end(), byBegin);
}

struct Place {
  uint64_t begin = 0;
  uint32_t rank = 0;
  bool operator<(const Place& o) const { return begin < o.begin || (begin == o.begin && rank < o.rank); }
};

constexpr Place kNoPlace{UINT64_MAX, UINT32_MAX};

class RowOrder {
 public:
  RowOrder(const std::vector<Annotation>& annotations, std::span<const RowSource> rows,
           std::span<const RowSource> leave_out) {
    for (const Annotation& a : annotations) {
      if (last_ < ids_.size() && ids_[last_] == a.row) continue;
      const auto seen = std::find(ids_.begin(), ids_.end(), a.row);
      last_ = static_cast<std::size_t>(seen - ids_.begin());
      if (seen == ids_.end()) ids_.push_back(a.row);
    }
    for (const RowSource& r : rows) {
      if (std::find(ids_.begin(), ids_.end(), r.row) == ids_.end()) ids_.push_back(r.row);
    }
    std::sort(ids_.begin(), ids_.end(),
              [rows](uint32_t a, uint32_t b) { return rowKey(a, rows) < rowKey(b, rows); });
    for (const uint32_t id : ids_) {
      kept_.push_back(!std::any_of(leave_out.begin(), leave_out.end(), [id](const RowSource& s) { return s.row == id; }));
    }
    last_ = 0;
    ordered_ = std::is_sorted(annotations.begin(), annotations.end(),
                              [this](const Annotation& a, const Annotation& b) { return placeOf(a) < placeOf(b); });
  }

  std::size_t indexOf(uint32_t row) const {
    if (last_ < ids_.size() && ids_[last_] == row) return last_;
    last_ = static_cast<std::size_t>(std::find(ids_.begin(), ids_.end(), row) - ids_.begin());
    return last_;
  }
  Place placeOf(const Annotation& a) const { return {a.begin, 2 * static_cast<uint32_t>(indexOf(a.row))}; }
  uint32_t hostRank(uint32_t row) const { return 2 * static_cast<uint32_t>(indexOf(row)) + 1; }
  bool kept(const Annotation& a) const { return kept_[indexOf(a.row)]; }
  bool allKept() const { return std::all_of(kept_.begin(), kept_.end(), [](bool k) { return k; }); }
  bool ordered() const { return ordered_; }
  std::size_t rows() const { return ids_.size(); }

 private:
  std::vector<uint32_t> ids_;
  std::vector<bool> kept_;
  mutable std::size_t last_ = 0;
  bool ordered_ = true;
};

struct BitQueue {
  const RowToMake* row = nullptr;
  uint32_t rank = 0;
  std::vector<Annotation> items;
  std::size_t head = 0;
  Place place() const { return head < items.size() ? Place{items[head].begin, rank} : kNoPlace; }
};

std::expected<void, std::string> queueBits(BitQueue& queue, const Annotation& word, const SignalView& view,
                                           std::vector<Annotation>& scratch) {
  scratch.clear();
  if (auto made = appendBits(*queue.row, word, view, scratch); !made) return made;
  if (queue.head == queue.items.size()) {
    queue.items.clear();
    queue.head = 0;
  }
  for (const Annotation& bit : scratch) {
    if (queue.items.size() == queue.head || queue.items.back().begin <= bit.begin) {
      queue.items.push_back(bit);
      continue;
    }
    const auto at = std::upper_bound(queue.items.begin() + static_cast<std::ptrdiff_t>(queue.head), queue.items.end(),
                                     bit, [](const Annotation& a, const Annotation& b) { return a.begin < b.begin; });
    queue.items.insert(at, bit);
  }
  return {};
}

struct Source {
  const Annotation* at = nullptr;
  const Annotation* end = nullptr;
};

std::expected<std::vector<Annotation>, std::string> merged(std::span<Source> sources, std::span<BitQueue> queues,
                                                           const RowOrder& order, const SignalView* view,
                                                           std::size_t total) {
  std::vector<Annotation> out;
  out.reserve(total);
  std::vector<Annotation> scratch;
  for (;;) {
    Place best = kNoPlace;
    Place second = kNoPlace;
    Source* fromSource = nullptr;
    BitQueue* fromQueue = nullptr;
    const auto consider = [&](Place p, Source* s, BitQueue* q) {
      if (p < best) {
        second = best;
        best = p;
        fromSource = s;
        fromQueue = q;
      } else if (p < second) {
        second = p;
      }
    };
    for (Source& s : sources) {
      if (s.at != s.end) consider(order.placeOf(*s.at), &s, nullptr);
    }
    for (BitQueue& q : queues) consider(q.place(), nullptr, &q);
    if (fromQueue != nullptr) {
      do {
        out.push_back(fromQueue->items[fromQueue->head++]);
      } while (fromQueue->place() < second);
      continue;
    }
    if (fromSource == nullptr) return out;
    do {
      const Annotation& a = *fromSource->at++;
      if (order.kept(a)) out.push_back(a);
      for (BitQueue& q : queues) {
        if (!takesWordsFrom(*q.row->source, a)) continue;
        if (auto made = queueBits(q, a, *view, scratch); !made) return std::unexpected(made.error());
        second = (std::min)(second, q.place());
      }
    } while (fromSource->at != fromSource->end && order.placeOf(*fromSource->at) < second);
  }
}

std::expected<void, std::string> reordered(std::vector<Annotation>& annotations, std::span<const RowSource> rows,
                                           std::span<const RowSource> leave_out, std::span<BitQueue> queues,
                                           const SignalView* view, std::size_t bits) {
  const RowOrder order(annotations, rows, leave_out);
  if (order.ordered() && queues.empty()) {
    if (!order.allKept()) dropAnnotationsOfRows(annotations, leave_out);
    return {};
  }
  for (BitQueue& q : queues) q.rank = order.hostRank(q.row->source->row);
  std::vector<std::vector<Annotation>> byRow;
  std::vector<Source> sources;
  if (order.ordered()) {
    sources.push_back({annotations.data(), annotations.data() + annotations.size()});
  } else {
    byRow.resize(order.rows());
    for (const Annotation& a : annotations) byRow[order.indexOf(a.row)].push_back(a);
    for (std::vector<Annotation>& row : byRow) {
      putInBeginOrder(row);
      sources.push_back({row.data(), row.data() + row.size()});
    }
  }
  auto out = merged(sources, queues, order, view, annotations.size() + bits);
  if (!out) return std::unexpected(out.error());
  annotations = std::move(*out);
  return {};
}

}

const RowSource* notDerivedSharing(const std::vector<RowSource>& sources, std::string_view option) {
  for (const RowSource& s : sources) {
    if (s.enable_option == option && !derivesAnnotations(s)) return &s;
  }
  return nullptr;
}

std::vector<std::string> derivedRowEnableOptions(const std::vector<RowSource>& sources) {
  std::vector<std::string> out;
  for (const RowSource& s : sources) {
    if (!derivesAnnotations(s) || s.enable_option.empty() || contains(out, s.enable_option)) continue;
    if (notDerivedSharing(sources, s.enable_option) != nullptr) continue;
    out.push_back(s.enable_option);
  }
  return out;
}

OptionMap withDerivedRowsOff(const std::vector<RowSource>& sources, OptionMap options) {
  for (const std::string& name : derivedRowEnableOptions(sources)) options[name] = "no";
  return options;
}

std::expected<ResolvedRule, std::string> resolveRowRule(const RowSource& source, const OptionMap& options,
                                                        const ChannelMap& channels, uint64_t sample_rate_milli_hz) {
  if (const auto* grid = std::get_if<RowSource::BitGrid>(&source.rule)) {
    const BitGridRule rule{grid->rate_option, grid->offset_bits, grid->count_option, grid->order_option};
    auto resolved = bitGridFrom(rule, options, sample_rate_milli_hz);
    if (!resolved) return std::unexpected(std::move(resolved).error());
    return *resolved;
  }
  if (const auto* edges = std::get_if<RowSource::ClockEdges>(&source.rule)) {
    const ClockEdgesRule rule{edges->clock_channel, edges->data_channel, edges->count_option, edges->count};
    auto resolved = clockEdgesFrom(rule, channels, options);
    if (!resolved) return std::unexpected(std::move(resolved).error());
    return *resolved;
  }
  return std::unexpected(std::string("row source does not derive annotations"));
}

std::vector<RowSource> requestedDerivedRows(const std::vector<RowSource>& sources, const OptionMap& options) {
  const std::vector<std::string> switched = derivedRowEnableOptions(sources);
  std::vector<RowSource> requested;
  for (const RowSource& s : sources) {
    if (!derivesAnnotations(s)) continue;
    if (s.enable_option.empty()) {
      requested.push_back(s);
      continue;
    }
    const auto value = options.find(s.enable_option);
    if (contains(switched, s.enable_option) && value != options.end() && value->second == "yes") {
      requested.push_back(s);
    }
  }
  return requested;
}

std::expected<std::vector<RowSource>, std::string> takeDerivedRows(Decoder& decoder) {
  if (!decoder.isConfigured()) return std::unexpected(std::format("decoder '{}' is not configured", decoder.name()));
  const std::vector<RowSource> sources = decoder.rowSources();
  std::vector<RowSource> taken = requestedDerivedRows(sources, decoder.resolvedOptions());
  const ChannelMap channels = decoder.boundChannels();
  const InputMap inputs = decoder.boundInputs();
  if (auto forced = decoder.configure(channels, withDerivedRowsOff(sources, decoder.resolvedOptions()), inputs);
      !forced) {
    return std::unexpected(std::move(forced).error());
  }
  return taken;
}

std::vector<RowSource> hostDerivedRows(const std::vector<RowSource>& sources) {
  const std::vector<std::string> switched = derivedRowEnableOptions(sources);
  std::vector<RowSource> derived;
  for (const RowSource& s : sources) {
    if (derivesAnnotations(s) && (s.enable_option.empty() || contains(switched, s.enable_option))) {
      derived.push_back(s);
    }
  }
  return derived;
}

void orderByBeginAndRow(std::vector<Annotation>& annotations, std::span<const RowSource> rows) {
  (void)reordered(annotations, rows, {}, {}, nullptr, 0);
}

void dropAnnotationsOfRows(std::vector<Annotation>& annotations, std::span<const RowSource> rows) {
  std::erase_if(annotations, [rows](const Annotation& a) { return onRowOf(rows, a); });
}

std::expected<void, std::string> insertDerivedRowsByRow(std::vector<Annotation>& annotations,
                                                        std::span<const RowSource> rows,
                                                        std::span<const AnnotationRow> names,
                                                        const OptionMap& options, const ChannelMap& channels,
                                                        const SignalView& view, std::span<const RowSource> leave_out) {
  const std::vector<RowToMake> made = rowsToMake(rows, names, options, channels, view);
  const std::expected<std::vector<std::size_t>, std::string> counted = countBits(annotations, made);
  if (!counted) return std::unexpected(counted.error());
  std::vector<BitQueue> queues;
  std::size_t bits = 0;
  for (std::size_t r = 0; r < made.size(); ++r) {
    if ((*counted)[r] == 0) continue;
    queues.push_back({&made[r], 0, {}, 0});
    bits += (*counted)[r];
  }
  return reordered(annotations, rows, leave_out, queues, &view, bits);
}

}

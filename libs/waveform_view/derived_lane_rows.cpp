/*
 * SPDX-License-Identifier: MIT
 */

#include "derived_lane_rows.h"

#include <algorithm>
#include <utility>

namespace bitoscope::waveform_view {

namespace {

// WHY: quotient() repeats layoutAnnotationRow()'s and summarizeRow()'s expression on the same doubles: no
// rounding disagreement.
double quotient(std::uint64_t sample, double first, double spp) {
  return (static_cast<double>(sample) - first) / spp;
}

// WHY: Row is in the high half so keys sort as result groups do, by row then class: ties are broken in that
// order.
std::uint64_t keyOf(std::uint32_t row, std::uint32_t cls) { return (std::uint64_t{row} << 32) | cls; }

std::size_t beginsBelow(AnnotationReader& reader, std::size_t g, std::uint64_t sample, bool orEqual) {
  std::size_t probes = 0;
  return reader.firstBeginNotBeforeByTable(
    g, [sample, orEqual](std::uint64_t b) { return b < sample || (orEqual && b == sample); }, probes);
}

std::size_t firstEndAbove(AnnotationReader& reader, std::size_t g, std::uint64_t sample) {
  const AnnotationGroups& groups = reader.groups();
  if (groups.groupRegular(g)) {
    // WHY: Search the begins, not the ends: spans are disjoint and in order, so only the span just before can end
    // past its begin.
    std::size_t probes = 0;
    const std::size_t past =
      reader.firstBeginNotBeforeByTable(g, [sample](std::uint64_t b) { return b <= sample; }, probes);
    return past > 0 && reader.end(g, past - 1) > sample ? past - 1 : past;
  }
  std::size_t lo = 0;
  std::size_t hi = groups.groupSize(g);
  while (lo < hi) {
    const std::size_t mid = lo + (hi - lo) / 2;
    if (groups.maxEndThrough(g, mid) > sample) {
      hi = mid;
    } else {
      lo = mid + 1;
    }
  }
  return lo;
}

struct WordAt {
  std::size_t group = 0;
  std::size_t index = 0;
  std::uint64_t begin = 0;
  bool operator==(const WordAt&) const = default;
};

decode::WordSpan spanOf(AnnotationReader& reader, const WordAt& w) {
  return {w.begin, w.begin + reader.length(w.group, w.index), reader.value(w.group, w.index)};
}

// WHY: Words never overlap, so the first word ending after sample is also the one beginning first.
std::optional<WordAt> firstWordEndingAfter(AnnotationReader& reader, const DerivedRowSource& source,
                                           std::uint64_t sample) {
  std::optional<WordAt> best;
  for (const std::size_t g : source.sourceGroups) {
    const std::size_t i = firstEndAbove(reader, g, sample);
    if (i >= reader.groups().groupSize(g)) continue;
    const std::uint64_t b = reader.begin(g, i);
    if (!best || b < best->begin) best = WordAt{g, i, b};
  }
  return best;
}

std::expected<void, std::string> deriveSpan(const DerivedRows& derived, const DerivedRowSource& source,
                                            const decode::WordSpan& word, WordBits& out) {
  ++derived.wordsDerived;
  out.count = 0;
  const auto put = [&](const decode::DerivedBit& bit) {
    out.bits[out.count++] = decode::Annotation{bit.begin, bit.end, source.row, source.cls, bit.value};
  };
  std::expected<void, std::string> made;
  if (const auto* grid = std::get_if<decode::BitGrid>(&source.rule)) {
    // WHY: Check the bit count here: bitGridFrom() caps grids at 64 bits, a grid made any other way must not write
    // past out.
    if (grid->count > decode::kMaxEdgeBits) {
      return std::unexpected(std::string("declared bit count must be 1 to 64"));
    }
    made = decode::forEachGridBit(word, *grid, put);
  } else {
    const EdgeRule& edges = std::get<EdgeRule>(source.rule);
    if (edges.view == nullptr) return std::unexpected(std::string("no capture to read clock edges from"));
    made = decode::forEachEdgeBit(word, edges.edges, *edges.view, put);
  }
  return made;
}

bool hasWords(const DerivedRowSource& source) { return !source.sourceGroups.empty(); }

struct First {
  std::uint64_t begin = 0;
  std::uint64_t key = 0;
};

bool earlier(const First& a, const First& b) { return a.begin != b.begin ? a.begin < b.begin : a.key < b.key; }

std::size_t derivedCount(AnnotationReader& reader, const DerivedRows& derived, const DerivedRowSource& source,
                         const First& first, std::uint64_t end) {
  const std::uint64_t key = keyOf(source.row, source.cls);
  const auto counted = [&](const decode::Annotation& bit) {
    return (bit.begin > first.begin || (bit.begin == first.begin && key >= first.key)) && bit.begin < end;
  };
  std::size_t words = 0;
  std::optional<WordAt> a;
  std::optional<WordAt> z;
  for (const std::size_t g : source.sourceGroups) {
    const std::size_t lo = firstEndAbove(reader, g, first.begin);
    if (lo >= reader.groups().groupSize(g)) continue;
    const std::uint64_t loBegin = reader.begin(g, lo);
    const std::size_t hi = beginsBelow(reader, g, end, false);
    if (lo >= hi) continue;
    const std::uint64_t hiBegin = reader.begin(g, hi - 1);
    words += hi - lo;
    if (!a || loBegin < a->begin) a = WordAt{g, lo, loBegin};
    if (!z || hiBegin > z->begin) z = WordAt{g, hi - 1, hiBegin};
  }
  if (words == 0) return 0;
  const std::size_t ends = *a == *z ? 1 : 2;
  std::size_t count = bitsPerWord(source.rule) * (words - ends);
  WordBits bits;
  for (const WordAt& w : {*a, *z}) {
    if (deriveSpan(derived, source, spanOf(reader, w), bits)) {
      for (std::uint32_t k = 0; k < bits.count; ++k) count += counted(bits.bits[k]) ? 1 : 0;
    }
    if (ends == 1) break;
  }
  return count;
}

}

const DerivedRowSource* DerivedRows::find(std::uint32_t row) const {
  for (const DerivedRowSource& s : rows) {
    if (s.row == row) return &s;
  }
  return nullptr;
}

std::uint32_t bitsPerWord(const DerivedRule& rule) {
  if (const auto* grid = std::get_if<decode::BitGrid>(&rule)) return grid->count;
  return std::get<EdgeRule>(rule).edges.count;
}

std::expected<void, std::string> deriveWord(const AnnotationGroups& groups, const DerivedRows& derived,
                                            const DerivedRowSource& source, std::size_t group, std::size_t index,
                                            WordBits& out) {
  AnnotationReader reader(groups);
  return deriveWord(reader, derived, source, group, index, out);
}

std::expected<void, std::string> deriveWord(AnnotationReader& reader, const DerivedRows& derived,
                                            const DerivedRowSource& source, std::size_t group, std::size_t index,
                                            WordBits& out) {
  return deriveSpan(derived, source, spanOf(reader, WordAt{group, index, reader.begin(group, index)}), out);
}

std::expected<void, std::string> appendDerivedBits(const AnnotationGroups& groups, const AnnotationRange& range,
                                                   const DerivedRows& derived, const DerivedRowSource& source,
                                                   std::uint64_t begin, std::uint64_t end,
                                                   std::vector<decode::Annotation>& out) {
  AnnotationReader reader(groups);
  return appendDerivedBits(reader, range, derived, source, begin, end, out);
}

std::expected<void, std::string> appendDerivedBits(AnnotationReader& reader, const AnnotationRange& range,
                                                   const DerivedRows& derived, const DerivedRowSource& source,
                                                   std::uint64_t begin, std::uint64_t end,
                                                   std::vector<decode::Annotation>& out) {
  struct Cursor {
    std::uint64_t begin;
    std::size_t group;
    std::size_t index;
    std::size_t last;
    BlockCursor own;
  };
  std::vector<Cursor> cursors;
  cursors.reserve(source.sourceGroups.size());
  for (const std::size_t g : source.sourceGroups) {
    if (g < range.groups.size() && range.groups[g].first < range.groups[g].last) {
      Cursor& c = cursors.emplace_back();
      c.group = g;
      c.index = range.groups[g].first;
      c.last = range.groups[g].last;
      c.begin = reader.begin(g, c.index, c.own);
    }
  }
  WordBits bits;
  for (;;) {
    Cursor* next = nullptr;
    for (Cursor& c : cursors) {
      if (c.index < c.last && (next == nullptr || c.begin < next->begin)) next = &c;
    }
    if (next == nullptr) return {};
    const WordAt word{next->group, next->index, next->begin};
    if (++next->index < next->last) next->begin = reader.begin(next->group, next->index, next->own);
    const decode::WordSpan span = spanOf(reader, word);
    // WHY: A range starts at the first annotation of ANY row reaching it, so it can hold words that ended before it.
    if (span.end <= begin) continue;
    if (auto made = deriveSpan(derived, source, span, bits); !made) return made;
    for (std::uint32_t k = 0; k < bits.count; ++k) {
      const decode::Annotation& bit = bits.bits[k];
      if (bit.end > begin && bit.begin < end) out.push_back(bit);
    }
  }
}

std::vector<RowClass> laneRowClassPairs(const AnnotationGroups& groups, const DerivedRows& derived) {
  std::vector<RowClass> out(groups.rowClassPairs().begin(), groups.rowClassPairs().end());
  for (const DerivedRowSource& s : derived.rows) {
    if (hasWords(s)) out.push_back({s.row, s.cls});
  }
  std::ranges::sort(out);
  out.erase(std::unique(out.begin(), out.end()), out.end());
  return out;
}

std::vector<std::uint32_t> laneRowClasses(const AnnotationGroups& groups, const DerivedRows& derived,
                                          std::uint32_t row) {
  std::vector<std::uint32_t> out = groups.rowClasses(row);
  if (const DerivedRowSource* s = derived.find(row); s != nullptr && hasWords(*s)) {
    out.push_back(s->cls);
    std::ranges::sort(out);
    out.erase(std::unique(out.begin(), out.end()), out.end());
  }
  return out;
}

std::size_t laneRangeCount(const AnnotationGroups& groups, const AnnotationRange& range, const DerivedRows& derived,
                           std::uint64_t begin, std::uint64_t end) {
  AnnotationReader reader(groups);
  return laneRangeCount(reader, range, derived, begin, end);
}

std::size_t laneRangeCount(AnnotationReader& reader, const AnnotationRange& range, const DerivedRows& derived,
                           std::uint64_t begin, std::uint64_t end) {
  const AnnotationGroups& groups = reader.groups();
  std::size_t count = range.count();
  if (range.groups.empty()) return count;
  const std::optional<AnnotationRef> stored =
    firstEndingAfter(reader, [begin](std::uint64_t annotationEnd) { return annotationEnd > begin; });
  if (!stored) return count;
  First first{reader.begin(stored->group, stored->index),
              keyOf(groups.groupRow(stored->group), groups.groupClass(stored->group))};
  bool derivedFirst = false;
  WordBits bits;
  for (const DerivedRowSource& source : derived.rows) {
    const std::optional<WordAt> word = firstWordEndingAfter(reader, source, begin);
    if (!word || !deriveSpan(derived, source, spanOf(reader, *word), bits)) continue;
    for (std::uint32_t k = 0; k < bits.count; ++k) {
      if (bits.bits[k].end <= begin) continue;
      const First candidate{bits.bits[k].begin, keyOf(source.row, source.cls)};
      if (earlier(candidate, first)) {
        first = candidate;
        derivedFirst = true;
      }
      break;
    }
  }
  if (derivedFirst) {
    // WHY: Count again: a stored group sorting between the derived bit and the stored first now starts its part at a
    // tie.
    count = 0;
    for (std::size_t g = 0; g < groups.groupCount(); ++g) {
      const std::uint64_t key = keyOf(groups.groupRow(g), groups.groupClass(g));
      const std::size_t lo = beginsBelow(reader, g, first.begin, key < first.key);
      const std::size_t hi = (std::max)(lo, beginsBelow(reader, g, end, false));
      count += hi - lo;
    }
  }
  for (const DerivedRowSource& source : derived.rows) count += derivedCount(reader, derived, source, first, end);
  return count;
}

RowSummary summarizeLaneRow(const AnnotationGroups& groups, const DerivedRows& derived, std::uint32_t row,
                            const DecoderVisibility& visibility, double firstVisibleSample, double samplesPerPixel,
                            std::uint32_t widthPixels) {
  AnnotationReader reader(groups);
  return summarizeLaneRow(reader, derived, row, visibility, firstVisibleSample, samplesPerPixel, widthPixels);
}

RowSummary summarizeLaneRow(AnnotationReader& reader, const DerivedRows& derived, std::uint32_t row,
                            const DecoderVisibility& visibility, double firstVisibleSample, double samplesPerPixel,
                            std::uint32_t widthPixels) {
  const LaneCoverage coverage(reader.groups(), firstVisibleSample, samplesPerPixel, widthPixels);
  return summarizeLaneRow(reader, coverage, derived, row, visibility, firstVisibleSample, samplesPerPixel, widthPixels);
}

RowSummary summarizeLaneRow(AnnotationReader& reader, const LaneCoverage& coverage, const DerivedRows& derived,
                            std::uint32_t row, const DecoderVisibility& visibility, double firstVisibleSample,
                            double samplesPerPixel, std::uint32_t widthPixels) {
  const DerivedRowSource* s = derived.find(row);
  if (s == nullptr) {
    return summarizeRow(reader, coverage, row, visibility, firstVisibleSample, samplesPerPixel, widthPixels);
  }
  if (classHidden(visibility, row, s->cls)) return {};
  return summarizeGroupsAs(reader, coverage, s->sourceGroups, s->cls, firstVisibleSample, samplesPerPixel,
                           widthPixels);
}

std::optional<decode::Annotation> laneRowAnnotationAt(const AnnotationGroups& groups, const DerivedRows& derived,
                                                      std::uint32_t row, const DecoderVisibility& visibility,
                                                      double firstVisibleSample, double samplesPerPixel,
                                                      std::uint32_t widthPixels, std::uint32_t column) {
  AnnotationReader reader(groups);
  return laneRowAnnotationAt(reader, derived, row, visibility, firstVisibleSample, samplesPerPixel, widthPixels,
                             column);
}

std::optional<decode::Annotation> laneRowAnnotationAt(AnnotationReader& reader, const DerivedRows& derived,
                                                      std::uint32_t row, const DecoderVisibility& visibility,
                                                      double firstVisibleSample, double samplesPerPixel,
                                                      std::uint32_t widthPixels, std::uint32_t column) {
  const AnnotationGroups& groups = reader.groups();
  const DerivedRowSource* s = derived.find(row);
  if (s == nullptr) {
    const auto at =
      rowAnnotationAt(reader, row, visibility, firstVisibleSample, samplesPerPixel, widthPixels, column);
    if (!at) return std::nullopt;
    return reader.at(*at);
  }
  if (!groups.coverageUsable() || column >= widthPixels || !(samplesPerPixel > 0.0) ||
      classHidden(visibility, row, s->cls)) {
    return std::nullopt;
  }
  const double first = firstVisibleSample;
  const double spp = samplesPerPixel;
  const double left = static_cast<double>(column);
  const double right = left + 1.0;
  std::size_t probes = 0;
  std::optional<WordAt> word;
  for (const std::size_t g : s->sourceGroups) {
    const std::size_t i = firstSpanPast(reader, g, left, first, spp, probes);
    if (i >= groups.groupSize(g)) continue;
    const std::uint64_t b = reader.begin(g, i);
    if (!word || b < word->begin) word = WordAt{g, i, b};
  }
  if (!word || !(quotient(word->begin, first, spp) < right)) return std::nullopt;
  WordBits bits;
  if (!deriveSpan(derived, *s, spanOf(reader, *word), bits)) return std::nullopt;
  for (std::uint32_t k = 0; k < bits.count; ++k) {
    const decode::Annotation& bit = bits.bits[k];
    if (!(quotient(bit.end, first, spp) > left)) continue;
    if (quotient(bit.begin, first, spp) < right) return bit;
    return std::nullopt;
  }
  return std::nullopt;
}

std::vector<RowLayout> layoutLaneRows(const AnnotationGroups& groups, const AnnotationRange& range,
                                      const DerivedRows& derived, std::span<const decode::AnnotationRow> rows,
                                      const DecoderVisibility& visibility, double firstVisibleSample,
                                      double samplesPerPixel, std::uint32_t widthPixels, std::uint64_t begin,
                                      std::uint64_t end) {
  AnnotationReader reader(groups);
  return layoutLaneRows(reader, range, derived, rows, visibility, firstVisibleSample, samplesPerPixel, widthPixels,
                        begin, end);
}

std::vector<RowLayout> layoutLaneRows(AnnotationReader& reader, const AnnotationRange& range,
                                      const DerivedRows& derived, std::span<const decode::AnnotationRow> rows,
                                      const DecoderVisibility& visibility, double firstVisibleSample,
                                      double samplesPerPixel, std::uint32_t widthPixels, std::uint64_t begin,
                                      std::uint64_t end) {
  std::vector<RowLayout> layouts =
    layoutAnnotationRows(reader, range, rows, visibility, firstVisibleSample, samplesPerPixel, widthPixels);
  if (!(samplesPerPixel > 0.0) || widthPixels == 0) return layouts;
  for (std::size_t slot = 0; slot < rows.size(); ++slot) {
    const std::uint32_t row = rows[slot].id;
    const DerivedRowSource* s = derived.find(row);
    if (s == nullptr || rowSlot(rows, row) != slot ||
        classHidden(visibility, row, s->cls)) {
      continue;
    }
    RowLayout& layout = layouts[slot];
    layout = {};
    if (auto made = appendDerivedBits(reader, range, derived, *s, begin, end, layout.annotations); !made) {
      layout.annotations.clear();
      layout.failure = std::move(made).error();
      continue;
    }
    layout.marks = layoutAnnotationRow(layout.annotations, row, firstVisibleSample, samplesPerPixel, widthPixels);
  }
  return layouts;
}

std::vector<LaneTextMark> rowTextMarks(const AnnotationGroups& groups, std::span<const LaneTextMark> marks,
                                       std::uint32_t row) {
  std::vector<LaneTextMark> out;
  for (const LaneTextMark& m : marks) {
    if (groups.groupRow(m.annotation.group) == row) out.push_back(m);
  }
  return out;
}

}

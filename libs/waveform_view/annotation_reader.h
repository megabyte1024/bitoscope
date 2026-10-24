/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <utility>
#include <vector>

#include "annotation_block.h"
#include "annotation_groups.h"

namespace bitoscope::waveform_view {

struct BlockCursor {
  static constexpr std::size_t kNone = static_cast<std::size_t>(-1);

  std::size_t group = kNone;
  std::size_t block = kNone;
  std::size_t count;
  bool summed;
  bool searched;
  std::size_t filled;
  std::uint64_t base;
  std::uint64_t step;
  StepField steps;
  BlockField lengths;
  BlockField values;
  std::array<std::uint64_t, kBlockEntries> begins;

  std::uint64_t beginAt(std::size_t j) {
    if (!summed) return base + j * step;
    if (j >= filled) fillTo(j);
    return begins[j];
  }
  bool firstSearch() {
    if (!summed || filled >= count) return false;
    if (searched) {
      fillTo(count - 1);
      return false;
    }
    searched = true;
    return true;
  }
  void fillTo(std::size_t j);
};

class AnnotationReader {
 public:
  // WHY: AnnotationReader has 16 cursor slots: groups past 16 share slot g % 16, still exact but with more
  // unpacking.
  static constexpr std::size_t kSlots = 16;

  explicit AnnotationReader(const AnnotationGroups& groups);
  ~AnnotationReader();
  AnnotationReader(const AnnotationReader&) = delete;
  AnnotationReader& operator=(const AnnotationReader&) = delete;

  const AnnotationGroups& groups() const { return *groups_; }
  std::uint64_t begin(std::size_t g, std::size_t i) {
    const std::size_t k = i / kBlockEntries;
    const std::size_t j = i % kBlockEntries;
    if (j == 0) return groups_->groups_.data()[g].blockBegin(k);
    return cursor(g, k).beginAt(j);
  }
  std::uint64_t begin(std::size_t g, std::size_t i, BlockCursor& own) {
    const std::size_t k = i / kBlockEntries;
    const std::size_t j = i % kBlockEntries;
    if (j == 0) return groups_->groups_.data()[g].blockBegin(k);
    if (own.group != g || own.block != k) load(g, k, own);
    return own.beginAt(j);
  }
  std::uint64_t length(std::size_t g, std::size_t i) { return lengthThrough(g, i, cursors_[g % kSlots]); }
  std::uint64_t length(std::size_t g, std::size_t i, BlockCursor& own) { return lengthThrough(g, i, own); }
  std::pair<std::uint64_t, std::uint64_t> span(std::size_t g, std::size_t i) {
    const std::uint64_t b = begin(g, i);
    return {b, length(g, i)};
  }
  std::uint64_t end(std::size_t g, std::size_t i) {
    const auto [b, l] = span(g, i);
    return b + l;
  }
  std::uint64_t coveredBefore(std::size_t g, std::size_t i, std::size_t& reads);
  std::uint64_t value(std::size_t g, std::size_t i) { return valueThrough(g, i, cursors_[g % kSlots]); }
  std::uint64_t value(std::size_t g, std::size_t i, BlockCursor& own) { return valueThrough(g, i, own); }
  BlockField blockValues(std::size_t g, std::size_t k) const { return groups_->groups_.data()[g].blockFields(k).values; }
  decode::Annotation at(AnnotationRef ref) {
    const auto [b, l] = span(ref.group, ref.index);
    return {b, b + l, groups_->groupRow(ref.group), groups_->groupClass(ref.group), value(ref.group, ref.index)};
  }

  template <class Before>
  std::size_t firstBeginNotBefore(std::size_t g, std::size_t from, Before before, std::size_t& probes) {
    const AnnotationGroups::Group& group = groups_->groups_.data()[g];
    const std::size_t n = group.n;
    if (from >= n) return from;
    std::size_t k = from / kBlockEntries;
    const std::size_t j = from % kBlockEntries;
    if (j == 0) {
      ++probes;
      if (!before(group.blockBegin(k))) return from;
    }
    BlockCursor* c = &cursor(g, k);
    const auto passes = [&](std::size_t e) {
      ++probes;
      return before(c->beginAt(e));
    };
    const auto firstFailing = [&](std::size_t lo, std::size_t hi) { return firstFailingIn(*c, lo, hi, before, probes); };
    if (j != 0 && !passes(j)) return from;
    std::size_t passed = j;
    for (std::size_t step = 1; step <= 2 && passed + step < c->count; step *= 2) {
      if (!passes(passed + step)) return k * kBlockEntries + firstFailing(passed + 1, passed + step);
      passed += step;
    }

    const std::size_t blocks = group.blockCount();
    const std::size_t first = k + 1;
    const auto tablePasses = [&](std::size_t m) {
      ++probes;
      return before(group.blockBegin(m));
    };
    std::size_t hit = first;
    if (first < blocks && tablePasses(first)) {
      std::size_t lo = first;
      std::size_t step = 1;
      hit = lo + step;
      while (hit < blocks && tablePasses(hit)) {
        lo = hit;
        step *= 2;
        hit = lo + step;
      }
      if (hit > blocks) hit = blocks;
      while (hit - lo > 1) {
        const std::size_t mid = lo + (hit - lo) / 2;
        if (tablePasses(mid)) {
          lo = mid;
        } else {
          hit = mid;
        }
      }
    }
    if (hit == first) return k * kBlockEntries + firstFailing(passed + 1, c->count);
    k = hit - 1;
    c = &cursor(g, k);
    return k * kBlockEntries + firstFailing(1, c->count);
  }

  template <class Before>
  std::size_t firstBeginNotBeforeByTable(std::size_t g, Before before, std::size_t& probes) {
    const AnnotationGroups::Group& group = groups_->groups_.data()[g];
    std::size_t lo = 0;
    std::size_t hi = group.blockCount();
    while (lo < hi) {
      const std::size_t mid = lo + (hi - lo) / 2;
      ++probes;
      if (before(group.blockBegin(mid))) {
        lo = mid + 1;
      } else {
        hi = mid;
      }
    }
    if (lo == 0) return 0;
    const std::size_t k = lo - 1;
    if (group.n - k * kBlockEntries == 1) return k * kBlockEntries + 1;
    BlockCursor& c = cursor(g, k);
    return k * kBlockEntries + firstFailingIn(c, 1, c.count, before, probes);
  }

  std::size_t blocksUnpacked() const { return unpacked_; }
  std::size_t blocksLoaded() const { return loaded_; }
  std::size_t lengthsReadInPlace() const { return inPlace_; }
  std::size_t valuesReadInPlace() const { return valuesInPlace_; }
  static std::size_t blocksUnpackedOnThisThread();
  static std::size_t blocksLoadedOnThisThread();

 private:
  BlockCursor& cursor(std::size_t g, std::size_t k) {
    BlockCursor& c = cursors_[g % kSlots];
    if (c.group != g || c.block != k) load(g, k, c);
    return c;
  }
  BlockField lengthsOf(std::size_t g, std::size_t k, const BlockCursor& c) {
    if (c.group == g && c.block == k) return c.lengths;
    ++inPlace_;
    return groups_->groups_.data()[g].blockFields(k).lengths;
  }
  std::uint64_t lengthThrough(std::size_t g, std::size_t i, const BlockCursor& c) {
    return groups_->groups_.data()[g].lengthIn(lengthsOf(g, i / kBlockEntries, c), i);
  }
  std::uint64_t valueThrough(std::size_t g, std::size_t i, const BlockCursor& c) {
    const std::size_t k = i / kBlockEntries;
    if (c.group == g && c.block == k) return fieldEntry(c.values, i % kBlockEntries);
    ++valuesInPlace_;
    return fieldEntry(groups_->groups_.data()[g].blockFields(k).values, i % kBlockEntries);
  }

  // WHY: Begins are searched by eights before halving: they are summed only as far as asked, so halving would sum
  // half the block.
  template <class Before>
  static std::size_t firstFailingIn(BlockCursor& c, std::size_t lo, std::size_t hi, Before& before,
                                    std::size_t& probes) {
    const auto passes = [&](std::size_t e) {
      ++probes;
      return before(c.beginAt(e));
    };
    if (hi - lo > 8 && c.firstSearch()) {
      while (hi - lo > 8) {
        if (!passes(lo + 7)) {
          hi = lo + 7;
          break;
        }
        lo += 8;
      }
    }
    while (lo < hi) {
      const std::size_t mid = lo + (hi - lo) / 2;
      if (passes(mid)) {
        lo = mid + 1;
      } else {
        hi = mid;
      }
    }
    return lo;
  }
  void load(std::size_t g, std::size_t k, BlockCursor& c);

  const AnnotationGroups* groups_;
  std::array<BlockCursor, kSlots> cursors_;
  std::size_t unpacked_ = 0;
  std::size_t loaded_ = 0;
  std::size_t inPlace_ = 0;
  std::size_t valuesInPlace_ = 0;
};

std::vector<decode::Annotation> collect(AnnotationReader& reader, const AnnotationRange& range);
std::vector<decode::Annotation> collect(AnnotationReader& reader, const AnnotationRange& range,
                                        std::span<const std::size_t> groupNumbers);
std::vector<decode::Annotation> inOrder(AnnotationReader& reader);

template <class Before>
std::size_t AnnotationGroups::firstBeginNotBefore(std::size_t g, std::size_t from, Before before,
                                                  std::size_t& probes) const {
  AnnotationReader reader(*this);
  return reader.firstBeginNotBefore(g, from, before, probes);
}

template <class EndsAfter>
std::optional<AnnotationRef> firstEndingAfter(AnnotationReader& reader, EndsAfter endsAfter) {
  const AnnotationGroups& groups = reader.groups();
  std::optional<AnnotationRef> best;
  std::uint64_t bestBegin = 0;
  std::size_t probes = 0;
  for (std::size_t g = 0; g < groups.groupCount(); ++g) {
    const std::size_t n = groups.groupSize(g);
    std::size_t lo = 0;
    if (groups.groupRegular(g)) {
      // WHY: Search the begins, not the ends: spans are disjoint and in order, so only the span just before can end
      // past its begin.
      lo = reader.firstBeginNotBeforeByTable(g, [&](std::uint64_t b) { return !endsAfter(b); }, probes);
      if (lo > 0 && endsAfter(reader.end(g, lo - 1))) --lo;
    } else {
      std::size_t hi = n;
      while (lo < hi) {
        const std::size_t mid = lo + (hi - lo) / 2;
        if (endsAfter(groups.maxEndThrough(g, mid))) {
          hi = mid;
        } else {
          lo = mid + 1;
        }
      }
    }
    if (lo == n) continue;
    // WHY: Groups are visited by row and class: an equal begin keeps the earlier group, as the result's order does.
    const std::uint64_t begin = reader.begin(g, lo);
    if (!best || begin < bestBegin) {
      best = AnnotationRef{static_cast<std::uint32_t>(g), lo};
      bestBegin = begin;
    }
  }
  return best;
}

template <class EndsAfter>
std::optional<AnnotationRef> firstEndingAfter(const AnnotationGroups& groups, EndsAfter endsAfter) {
  AnnotationReader reader(groups);
  return firstEndingAfter(reader, endsAfter);
}

}

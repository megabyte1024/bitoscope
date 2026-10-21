/*
 * SPDX-License-Identifier: MIT
 */

#include "annotation_reader.h"

#include <algorithm>
#include <memory>
#include <numeric>

namespace bitoscope::waveform_view {

namespace {

thread_local std::size_t unpackedOnThisThread = 0;
thread_local std::size_t loadedOnThisThread = 0;

}

void BlockCursor::fillTo(std::size_t j) {
  const std::size_t to = (std::min)(count, (j | 7) + 1);
  sumBegins(steps, begins.data(), filled, to);
  filled = to;
}

AnnotationReader::AnnotationReader(const AnnotationGroups& groups) : groups_(&groups) {}

AnnotationReader::~AnnotationReader() {
  unpackedOnThisThread += unpacked_;
  loadedOnThisThread += loaded_;
}

std::size_t AnnotationReader::blocksUnpackedOnThisThread() { return unpackedOnThisThread; }

std::size_t AnnotationReader::blocksLoadedOnThisThread() { return loadedOnThisThread; }

std::uint64_t AnnotationReader::coveredBefore(std::size_t g, std::size_t i, std::size_t& reads) {
  const AnnotationGroups::Group& group = groups_->groups_.data()[g];
  if (i >= group.n) return group.covered;
  const std::size_t k = i / kBlockEntries;
  if (i % kBlockEntries == 0) return group.blockCovered(k);
  return group.coveredBefore(i, lengthsOf(g, k, cursors_[g % kSlots]), reads);
}

void AnnotationReader::load(std::size_t g, std::size_t k, BlockCursor& c) {
  const AnnotationGroups::Group& group = groups_->groups_.data()[g];
  c.group = g;
  c.block = k;
  c.count = (std::min)(kBlockEntries, group.n - k * kBlockEntries);
  c.base = group.blockBegin(k);
  c.searched = false;
  ++loaded_;
  const BlockFields fields = group.blockFields(k);
  c.steps = fields.steps;
  c.lengths = fields.lengths;
  c.values = fields.values;
  if (c.steps.width == 0) {
    c.summed = false;
    c.step = c.steps.min;
    return;
  }
  c.summed = true;
  c.begins[0] = c.base;
  c.filled = 1;
  ++unpacked_;
}

std::vector<decode::Annotation> collect(AnnotationReader& reader, const AnnotationRange& range) {
  std::vector<std::size_t> all(range.groups.size());
  std::iota(all.begin(), all.end(), std::size_t{0});
  return collect(reader, range, all);
}

std::vector<decode::Annotation> collect(AnnotationReader& reader, const AnnotationRange& range,
                                        std::span<const std::size_t> groupNumbers) {
  const AnnotationGroups& groups = reader.groups();
  struct Cursor {
    std::uint64_t begin;
    std::size_t group;
    std::size_t index;
    std::size_t last;
    BlockCursor* own;
  };
  const auto later = [](const Cursor& a, const Cursor& b) {
    return a.begin != b.begin ? a.begin > b.begin : a.group > b.group;
  };
  std::vector<Cursor> heap;
  std::size_t total = 0;
  for (const std::size_t g : groupNumbers) {
    if (g >= range.groups.size() || g >= groups.groupCount()) continue;
    const GroupSpan span = range.groups[g];
    if (span.first >= span.last) continue;
    heap.push_back({0, g, span.first, span.last, nullptr});
    total += span.last - span.first;
  }
  // WHY: A cursor per group, not the reader's slots: groups sharing a slot would evict each other's blocks.
  const auto owns = std::make_unique_for_overwrite<BlockCursor[]>(heap.size());
  for (std::size_t p = 0; p < heap.size(); ++p) {
    heap[p].own = &owns[p];
    heap[p].begin = reader.begin(heap[p].group, heap[p].index, owns[p]);
  }
  std::make_heap(heap.begin(), heap.end(), later);
  std::vector<decode::Annotation> out;
  out.reserve(total);
  while (!heap.empty()) {
    std::pop_heap(heap.begin(), heap.end(), later);
    Cursor& c = heap.back();
    out.push_back({c.begin, c.begin + reader.length(c.group, c.index, *c.own), groups.groupRow(c.group),
                   groups.groupClass(c.group), reader.value(c.group, c.index, *c.own)});
    if (++c.index < c.last) {
      c.begin = reader.begin(c.group, c.index, *c.own);
      std::push_heap(heap.begin(), heap.end(), later);
    } else {
      heap.pop_back();
    }
  }
  return out;
}

std::vector<decode::Annotation> inOrder(AnnotationReader& reader) {
  const AnnotationGroups& groups = reader.groups();
  AnnotationRange all;
  all.groups.reserve(groups.groupCount());
  for (std::size_t g = 0; g < groups.groupCount(); ++g) all.groups.push_back({0, groups.groupSize(g)});
  return collect(reader, all);
}

}

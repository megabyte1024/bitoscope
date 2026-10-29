/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <expected>
#include <memory>
#include <optional>
#include <span>
#include <unordered_map>
#include <utility>
#include <vector>

#include "annotation_block.h"
#include "annotation_memory.h"
#include "coverage_pyramid.h"
#include "decoder.h"
#include "row_class.h"

namespace bitoscope::waveform_view {

class AnnotationReader;

// WHY: Chunked column, not std::vector: a doubling vector copies itself whole and holds up to 1.5 times its need.
template <class T>
class ChunkedColumn {
 public:
  static constexpr std::size_t kShift = 16;
  static constexpr std::size_t kEntries = std::size_t{1} << kShift;
  static constexpr std::size_t kMask = kEntries - 1;
  static constexpr std::size_t kFirstCapacity = 16;

  std::size_t size() const { return size_; }
  T get(std::size_t i) const { return chunks_.data()[i >> kShift].get()[i & kMask]; }

  void push(T value, MemoryCharge& charge) {
    if (size_ == capacity_) grow(charge);
    chunks_.data()[size_ >> kShift].get()[size_ & kMask] = value;
    ++size_;
  }

  void shrinkToFit(MemoryCharge& charge) {
    if (chunks_.empty()) return;
    const std::size_t c = chunks_.size() - 1;
    const std::size_t held = size_ - (c << kShift);
    if ((c << kShift) + held < capacity_) {
      charged(charge, held * sizeof(T), [&] {
        auto exact = std::make_unique_for_overwrite<T[]>(held);
        std::copy_n(chunks_[c].get(), held, exact.get());
        chunks_[c] = std::move(exact);
        capacity_ = size_;
      });
    }
    if (chunks_.size() < chunks_.capacity()) {
      charged(charge, chunks_.size() * sizeof(Chunk), [&] { chunks_.shrink_to_fit(); });
    }
  }

  std::size_t chunkCount() const { return chunks_.size(); }
  std::span<const T> chunk(std::size_t c) const {
    const std::size_t from = c << kShift;
    return {chunks_[c].get(), (std::min)(kEntries, size_ - from)};
  }
  void dropChunk(std::size_t c, MemoryCharge& charge) {
    const std::size_t freed = chunkCapacity(c) * sizeof(T);
    chunks_[c].reset();
    charge.release(freed);
  }
  void clear(MemoryCharge& charge) {
    std::size_t freed = chunks_.capacity() * sizeof(Chunk);
    for (std::size_t c = 0; c < chunks_.size(); ++c) {
      if (chunks_[c]) freed += chunkCapacity(c) * sizeof(T);
    }
    chunks_.clear();
    chunks_.shrink_to_fit();
    size_ = 0;
    capacity_ = 0;
    charge.release(freed);
  }
  std::size_t bytes() const { return capacity_ * sizeof(T) + chunks_.capacity() * sizeof(Chunk); }
  std::size_t unusedBytes() const {
    return (capacity_ - size_) * sizeof(T) + (chunks_.capacity() - chunks_.size()) * sizeof(Chunk);
  }

 private:
  using Chunk = std::unique_ptr<T[]>;

  std::size_t chunkCapacity(std::size_t c) const {
    return c + 1 < chunks_.size() ? kEntries : capacity_ - (c << kShift);
  }

  template <class Change>
  void charged(MemoryCharge& charge, std::size_t request, Change change) {
    const std::size_t before = bytes();
    charge.acquire(request);
    change();
    charge.release(request + before - bytes());
  }

  void grow(MemoryCharge& charge) {
    const std::size_t c = size_ >> kShift;
    if (c < chunks_.size()) {
      const std::size_t held = size_ - (c << kShift);
      const std::size_t bigger = c == 0 ? (std::min)((std::max)(held * 2, kFirstCapacity), kEntries) : kEntries;
      charged(charge, bigger * sizeof(T), [&] {
        auto next = std::make_unique_for_overwrite<T[]>(bigger);
        std::copy_n(chunks_[c].get(), held, next.get());
        chunks_[c] = std::move(next);
        capacity_ = (c << kShift) + bigger;
      });
      return;
    }
    const std::size_t entries = c == 0 ? kFirstCapacity : kEntries;
    const std::size_t table =
      chunks_.size() < chunks_.capacity() ? 0 : (std::max)(chunks_.capacity() * 2, std::size_t{1});
    charged(charge, entries * sizeof(T) + table * sizeof(Chunk), [&] {
      auto next = std::make_unique_for_overwrite<T[]>(entries);
      if (table != 0) chunks_.reserve(table);
      chunks_.push_back(std::move(next));
      capacity_ = (c << kShift) + entries;
    });
  }

  std::vector<Chunk> chunks_;
  std::size_t size_ = 0;
  std::size_t capacity_ = 0;
};

// WHY: ValueColumn is unused by AnnotationGroups: the compact storage benchmark compares its layout.
class ValueColumn {
 public:
  std::size_t size() const { return size_; }
  std::size_t width() const { return width_; }
  std::uint64_t get(std::size_t i) const;
  void push(std::uint64_t value, MemoryCharge& charge);
  void shrinkToFit(MemoryCharge& charge);
  std::size_t bytes() const;
  std::size_t unusedBytes() const;

 private:
  void widen(std::size_t width, MemoryCharge& charge);

  std::size_t size_ = 0;
  std::size_t width_ = 0;
  ChunkedColumn<std::uint8_t> bytes1_;
  ChunkedColumn<std::uint16_t> bytes2_;
  ChunkedColumn<std::uint32_t> bytes4_;
  ChunkedColumn<std::uint64_t> bytes8_;
};

struct AnnotationRef {
  std::uint32_t group = 0;
  std::size_t index = 0;
  bool operator==(const AnnotationRef&) const = default;
};

struct GroupSpan {
  std::size_t first = 0;
  std::size_t last = 0;
  bool operator==(const GroupSpan&) const = default;
};

struct AnnotationRange {
  std::vector<GroupSpan> groups;
  std::size_t count() const;
};

struct CoveragePolicy {
  unsigned budgetPercent = 20;
  unsigned fixedShift = 0;
};

class AnnotationGroups {
 public:
  static constexpr std::size_t kCoverageStride = 32;
  static_assert(kCoverageStride == kBlockEntries, "a checkpoint is the table entry of a block of begins");
  static constexpr std::uint32_t kLongLength = kLengthMarker;

  AnnotationGroups() = default;

  std::size_t size() const { return size_; }
  std::size_t groupCount() const { return groups_.size(); }
  std::uint32_t groupRow(std::size_t g) const { return groups_[g].row; }
  std::uint32_t groupClass(std::size_t g) const { return groups_[g].cls; }
  std::size_t groupSize(std::size_t g) const { return groups_.data()[g].n; }
  bool groupRegular(std::size_t g) const { return groups_[g].regular; }
  std::size_t valueWidth(std::size_t g) const { return groups_[g].valueBytes; }
  std::pair<std::size_t, std::size_t> rowGroups(std::uint32_t row) const;
  std::optional<std::size_t> findGroup(std::uint32_t row, std::uint32_t cls) const;

  std::uint64_t begin(std::size_t g, std::size_t i) const;
  std::uint64_t length(std::size_t g, std::size_t i) const;
  std::uint64_t end(std::size_t g, std::size_t i) const { return begin(g, i) + length(g, i); }
  std::uint64_t value(std::size_t g, std::size_t i) const;
  decode::Annotation at(AnnotationRef ref) const;
  std::span<const std::uint8_t> bytesAt(std::size_t g, std::uint64_t reference) const;

  std::uint64_t sortedEnd(std::size_t g, std::size_t k) const {
    const Group& group = groups_.data()[g];
    return group.regular ? end(g, k) : group.sortedEnds.data()[k];
  }
  std::uint64_t maxEndThrough(std::size_t g, std::size_t i) const {
    const Group& group = groups_.data()[g];
    return group.regular ? end(g, i) : group.maxEnds.data()[i];
  }
  std::uint64_t coveredBefore(std::size_t g, std::size_t i, std::size_t& reads) const;

  template <class Before>
  std::size_t firstBeginNotBefore(std::size_t g, std::size_t from, Before before, std::size_t& probes) const;

  bool usable() const { return usable_; }
  bool coverageUsable() const { return coverageUsable_; }

  std::span<const std::uint32_t> rowIds() const { return rowIds_; }
  std::vector<std::uint32_t> rowClasses(std::uint32_t row) const;
  std::span<const RowClass> rowClassPairs() const { return rowClassPairs_; }

  std::optional<AnnotationRef> firstReaching(std::uint64_t sample) const;
  AnnotationRange range(std::uint64_t begin, std::uint64_t end) const;
  std::vector<decode::Annotation> collect(const AnnotationRange& range) const;
  std::vector<decode::Annotation> collect(const AnnotationRange& range, std::span<const std::size_t> groupNumbers) const;
  std::vector<decode::Annotation> inOrder() const;

  std::size_t memoryBytes() const;
  std::size_t blockBytes(std::size_t g) const;
  std::span<const std::uint8_t> blockArray(std::size_t g) const;
  const CoveragePyramid& coverage(std::size_t g) const { return groups_[g].coverage; }

 private:
  friend class AnnotationGroupsBuilder;
  friend class AnnotationReader;

  struct Group {
    std::uint32_t row = 0;
    std::uint32_t cls = 0;
    std::unique_ptr<std::uint8_t[]> blocks;
    std::size_t n = 0;
    std::uint64_t firstBegin = 0;
    std::uint64_t usualStep = 0;
    std::uint64_t usualValue = 0;
    std::vector<std::pair<std::size_t, std::uint64_t>> longLengths;
    std::uint64_t covered = 0;
    std::uint32_t usualLength = 0;
    bool positive = true;
    bool regular = true;
    std::uint8_t beginWidth = 8;
    std::uint8_t offsetWidth = 0;
    std::uint8_t coveredWidth = 8;
    std::uint8_t valueBytes = 0;
    std::vector<std::uint64_t> maxEnds;
    std::vector<std::uint64_t> sortedEnds;
    CoveragePyramid coverage;

    std::size_t blockCount() const { return (n + kBlockEntries - 1) / kBlockEntries; }
    std::uint64_t blockBegin(std::size_t k) const {
      if (beginWidth == 4) {
        std::uint32_t distance;
        std::memcpy(&distance, blocks.get() + 4 * k, 4);
        return firstBegin + distance;
      }
      std::uint64_t begin;
      std::memcpy(&begin, blocks.get() + 8 * k, 8);
      return begin;
    }
    std::uint64_t blockCovered(std::size_t k) const;
    const std::uint8_t* blockPayload(std::size_t k) const;
    BlockFields blockFields(std::size_t k) const;
    std::uint64_t lengthIn(const BlockField& lengths, std::size_t i) const {
      const std::uint64_t stored = fieldEntry(lengths, i % kBlockEntries);
      return stored != kLongLength ? stored : longLength(i);
    }
    std::uint64_t lengthAt(std::size_t i) const;
    std::uint64_t valueAt(std::size_t i) const;
    std::uint64_t coveredBefore(std::size_t i, const BlockField& lengths, std::size_t& reads) const;
    std::size_t blockArrayBytes() const;
    std::size_t bytes() const;

   private:
    std::uint64_t longLength(std::size_t i) const;
    std::uint64_t offsetAt(std::size_t k) const;
  };

  struct BytePool {
    std::uint64_t key = 0;
    std::vector<std::uint8_t> bytes;
  };

  std::vector<Group> groups_;
  std::vector<BytePool> bytePools_;
  std::size_t size_ = 0;
  bool usable_ = true;
  bool coverageUsable_ = true;
  std::vector<std::uint32_t> rowIds_;
  std::vector<RowClass> rowClassPairs_;
};

// WHY: Another thread may read only closed blocks, via published(g) and closedBytes(g) between add() calls.
// Open block, chunk tables, long-lengths table and anything once finish() begins are unsafe to read.
class AnnotationGroupsBuilder {
 public:
  AnnotationGroupsBuilder();
  explicit AnnotationGroupsBuilder(AnnotationMemory& memory);
  explicit AnnotationGroupsBuilder(CoveragePolicy policy);
  AnnotationGroupsBuilder(AnnotationMemory& memory, CoveragePolicy policy);
  ~AnnotationGroupsBuilder();
  AnnotationGroupsBuilder(const AnnotationGroupsBuilder&) = delete;
  AnnotationGroupsBuilder& operator=(const AnnotationGroupsBuilder&) = delete;

  std::expected<void, GroupsRefusal> add(const decode::Annotation& annotation);
  std::expected<void, GroupsRefusal> add(std::span<const decode::Annotation> batch);
  std::expected<void, GroupsRefusal> addBytes(const decode::Annotation& annotation, std::span<const std::uint8_t> bytes);
  std::expected<AnnotationGroups, GroupsRefusal> finish();
  // WHY: snapshot(p) needs p to be a watermark the decoder settled: nothing with begin < p may arrive later.
  std::expected<AnnotationGroups, GroupsRefusal> snapshot(std::uint64_t p) const;
  std::size_t charged() const { return charge_.charged; }
  std::size_t openBytes() const;
  const std::atomic<std::size_t>& published(std::size_t g) const;
  std::size_t closedBlocks(std::size_t g) const;
  std::vector<std::span<const std::uint8_t>> closedBytes(std::size_t g) const;

 private:
  struct BuildGroup;

  static constexpr std::size_t kFirstCoverageLook = 1024;
  static constexpr std::size_t kLeastCoverageCap = 4096;
  static constexpr std::size_t kFixedCoverageCap = std::size_t{64} << 20;

  std::size_t groupFor(std::uint32_t row, std::uint32_t cls);
  void lookAtCoverage();
  std::size_t coverageBudget(std::size_t storeBytes) const;
  unsigned coverageShiftFor(std::span<const std::pair<std::uint64_t, std::uint64_t>> extents,
                            std::size_t storeBytes) const;
  void lookAfterAdding(const BuildGroup& build);
  void finishCoverage(AnnotationGroups& out, std::vector<std::unique_ptr<BuildGroup>>& builds);
  std::expected<void, GroupsRefusal> refuse(GroupsRefusal::Reason reason);
  void drop();
  static void complete(AnnotationGroups& out, std::vector<AnnotationGroups::Group> groups, MemoryCharge& charge);

  MemoryCharge charge_;
  CoveragePolicy policy_;
  unsigned coverageShift_ = 0;
  std::size_t coverageCap_ = 0;
  std::size_t nextCoverageLook_ = kFirstCoverageLook;
  std::optional<GroupsRefusal> refused_;
  std::size_t stored_ = 0;
  std::vector<AnnotationGroups::Group> groups_;
  std::vector<AnnotationGroups::BytePool> bytePools_;
  // WHY: Each BuildGroup is its own allocation: a reader may hold one published count while later groups are added.
  std::vector<std::unique_ptr<BuildGroup>> builds_;
  std::vector<std::uint64_t> keys_;
  std::unordered_map<std::uint64_t, std::size_t> manyKeys_;
  std::size_t last_ = 0;
};

AnnotationGroups groupAnnotations(std::span<const decode::Annotation> annotations, CoveragePolicy policy = {});

}

// WHY: Included last: reader wrappers need AnnotationGroups complete.
#include "annotation_reader.h"

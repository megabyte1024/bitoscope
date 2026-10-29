/*
 * SPDX-License-Identifier: MIT
 */

#include "annotation_groups.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cstring>
#include <new>
#include <numeric>
#include <optional>
#include <queue>

namespace bitoscope::waveform_view {

namespace {

struct ChargeRefused {};

std::size_t widthOf(std::uint64_t value) {
  if (value == 0) return 0;
  if (value <= 0xFFu) return 1;
  if (value <= 0xFFFFu) return 2;
  if (value <= 0xFFFFFFFFu) return 4;
  return 8;
}

template <class From, class To>
void moveInto(ChunkedColumn<From>& from, ChunkedColumn<To>& to, MemoryCharge& charge) {
  for (std::size_t c = 0; c < from.chunkCount(); ++c) {
    for (const From v : from.chunk(c)) to.push(static_cast<To>(v), charge);
    from.dropChunk(c, charge);
  }
  from.clear(charge);
}

template <class To>
void moveAnyInto(std::size_t width, ChunkedColumn<std::uint8_t>& b1, ChunkedColumn<std::uint16_t>& b2,
                 ChunkedColumn<std::uint32_t>& b4, ChunkedColumn<To>& to, MemoryCharge& charge) {
  if (width == 1) moveInto(b1, to, charge);
  if (width == 2) moveInto(b2, to, charge);
  if constexpr (sizeof(To) > 4) {
    if (width == 4) moveInto(b4, to, charge);
  }
}

template <class V>
std::size_t vectorBytes(const V& v) {
  return v.capacity() * sizeof(typename V::value_type);
}

template <class V, class Change>
void chargedVector(V& v, MemoryCharge& charge, std::size_t request, Change change) {
  const std::size_t before = vectorBytes(v);
  charge.acquire(request);
  change();
  charge.release(request + before - vectorBytes(v));
}

template <class V>
void roomForOneMore(V& v, MemoryCharge& charge) {
  if (v.size() < v.capacity()) return;
  const std::size_t next = (std::max)(v.capacity() * 2, std::size_t{1});
  chargedVector(v, charge, next * sizeof(typename V::value_type), [&] { v.reserve(next); });
}

template <class V>
void shrinkVector(V& v, MemoryCharge& charge) {
  if (v.size() == v.capacity()) return;
  chargedVector(v, charge, v.size() * sizeof(typename V::value_type), [&] { v.shrink_to_fit(); });
}

std::uint64_t keyOf(std::uint32_t row, std::uint32_t cls) { return (std::uint64_t{row} << 32) | cls; }

constexpr std::size_t kBytesLengthPrefix = sizeof(std::uint32_t);

constexpr std::size_t kLinearKeys = 32;

template <class T>
class AppendColumn {
 public:
  static constexpr std::size_t kFirst = 64 / sizeof(T);
  static constexpr std::size_t kLast = 4096 / sizeof(T);
  static constexpr std::size_t kDoublings = static_cast<std::size_t>(std::bit_width(kLast / kFirst)) - 1;
  static_assert(kFirst << kDoublings == kLast, "chunks double from the first size to the last");
  static constexpr std::size_t kGrowing = kFirst * ((std::size_t{1} << kDoublings) - 1);

  std::size_t size() const { return size_; }
  bool empty() const { return size_ == 0; }
  void push(T value, MemoryCharge& charge) {
    if (size_ == capacity_) addChunk(charge);
    chunks_.back()[size_ - lastStart_] = value;
    ++size_;
  }
  void append(std::span<const T> values, MemoryCharge& charge) {
    while (!values.empty()) {
      if (size_ == capacity_) addChunk(charge);
      const std::size_t take = (std::min)(values.size(), capacity_ - size_);
      std::copy_n(values.data(), take, chunks_.back().get() + (size_ - lastStart_));
      size_ += take;
      values = values.subspan(take);
    }
  }
  std::size_t chunkCount() const { return chunks_.size(); }
  std::span<const T> chunk(std::size_t c) const {
    return {chunks_[c].get(), (std::min)(capacityOf(c), size_ - startOf(c))};
  }
  T get(std::size_t i) const {
    const std::size_t c = chunkOf(i);
    return chunks_[c][i - startOf(c)];
  }
  template <class Fn>
  void forEachPiece(std::size_t from, std::size_t count, Fn fn) const {
    for (std::size_t c = chunkOf(from); count != 0; ++c) {
      const std::size_t at = from - startOf(c);
      const std::size_t take = (std::min)(count, capacityOf(c) - at);
      fn(std::span<const T>(chunks_[c].get() + at, take));
      from += take;
      count -= take;
    }
  }
  std::uint8_t* copyBytes(std::size_t from, std::size_t count, std::uint8_t* out) const {
    forEachPiece(from, count, [&out](std::span<const T> piece) {
      std::memcpy(out, piece.data(), piece.size_bytes());
      out += piece.size_bytes();
    });
    return out;
  }
  void dropChunk(std::size_t c, MemoryCharge& charge) {
    chunks_[c].reset();
    charge.release(capacityOf(c) * sizeof(T));
  }
  void clear(MemoryCharge& charge) {
    std::size_t freed = chunks_.capacity() * sizeof(Chunk);
    for (std::size_t c = 0; c < chunks_.size(); ++c) {
      if (chunks_[c]) freed += capacityOf(c) * sizeof(T);
    }
    chunks_ = std::vector<Chunk>();
    size_ = 0;
    capacity_ = 0;
    lastStart_ = 0;
    charge.release(freed);
  }
  std::size_t bytes() const { return capacity_ * sizeof(T) + chunks_.capacity() * sizeof(Chunk); }
  std::size_t unusedBytes() const { return bytes() - size_ * sizeof(T); }

 private:
  using Chunk = std::unique_ptr<T[]>;

  static std::size_t capacityOf(std::size_t c) { return c < kDoublings ? kFirst << c : kLast; }
  static std::size_t startOf(std::size_t c) {
    return c <= kDoublings ? kFirst * ((std::size_t{1} << c) - 1) : kGrowing + (c - kDoublings) * kLast;
  }
  static std::size_t chunkOf(std::size_t i) {
    if (i < kGrowing) return static_cast<std::size_t>(std::bit_width(i / kFirst + 1)) - 1;
    return kDoublings + (i - kGrowing) / kLast;
  }

  void addChunk(MemoryCharge& charge) {
    const std::size_t entries = capacityOf(chunks_.size());
    const std::size_t table =
      chunks_.size() < chunks_.capacity() ? 0 : (std::max)(chunks_.capacity() * 2, std::size_t{1});
    const std::size_t request = entries * sizeof(T) + table * sizeof(Chunk);
    const std::size_t before = bytes();
    charge.acquire(request);
    auto next = std::make_unique_for_overwrite<T[]>(entries);
    if (table != 0) chunks_.reserve(table);
    chunks_.push_back(std::move(next));
    lastStart_ = capacity_;
    capacity_ += entries;
    charge.release(request + before - bytes());
  }

  std::vector<Chunk> chunks_;
  std::size_t size_ = 0;
  std::size_t capacity_ = 0;
  std::size_t lastStart_ = 0;
};

// WHY: SplitColumn never widens in place: a reader that took earlier entries needs them unchanged.
struct SplitColumn {
  AppendColumn<std::uint32_t> narrow;
  AppendColumn<std::uint64_t> wide;

  std::size_t size() const { return narrow.size() + wide.size(); }
  std::uint64_t get(std::size_t i, std::uint64_t base) const {
    return i < narrow.size() ? base + narrow.get(i) : wide.get(i - narrow.size());
  }
  void push(std::uint64_t value, std::uint64_t base, MemoryCharge& charge) {
    const std::uint64_t distance = value - base;
    if (wide.empty() && distance <= kNarrowEntryMost) {
      narrow.push(static_cast<std::uint32_t>(distance), charge);
    } else {
      wide.push(value, charge);
    }
  }
  std::size_t unusedBytes() const { return narrow.unusedBytes() + wide.unusedBytes(); }
  void clear(MemoryCharge& charge) {
    narrow.clear(charge);
    wide.clear(charge);
  }
};

std::uint8_t* putEntry(std::uint8_t* out, std::uint64_t value, unsigned width) {
  if (width == 4) {
    const auto narrow = static_cast<std::uint32_t>(value);
    std::memcpy(out, &narrow, 4);
  } else {
    std::memcpy(out, &value, 8);
  }
  return out + width;
}

template <class T>
std::uint8_t* moveOut(AppendColumn<T>& column, std::uint8_t* out, MemoryCharge& charge) {
  for (std::size_t c = 0; c < column.chunkCount(); ++c) {
    const std::span<const T> entries = column.chunk(c);
    std::memcpy(out, entries.data(), entries.size_bytes());
    out += entries.size_bytes();
    column.dropChunk(c, charge);
  }
  return out;
}

std::uint8_t* moveOut(SplitColumn& column, std::uint64_t base, unsigned width, std::uint8_t* out,
                      MemoryCharge& charge) {
  if (width == 4) {
    out = moveOut(column.narrow, out, charge);
  } else {
    for (std::size_t c = 0; c < column.narrow.chunkCount(); ++c) {
      for (const std::uint32_t distance : column.narrow.chunk(c)) out = putEntry(out, base + distance, 8);
      column.narrow.dropChunk(c, charge);
    }
  }
  return moveOut(column.wide, out, charge);
}

std::uint8_t* copyOut(const SplitColumn& column, std::size_t count, std::uint64_t base, unsigned width,
                      std::uint8_t* out) {
  const std::size_t narrow = (std::min)(count, column.narrow.size());
  if (width == 4) return column.narrow.copyBytes(0, narrow, out);
  column.narrow.forEachPiece(0, narrow, [&](std::span<const std::uint32_t> piece) {
    for (const std::uint32_t distance : piece) out = putEntry(out, base + distance, 8);
  });
  return column.wide.copyBytes(0, count - narrow, out);
}

std::uint8_t* moveOutValues(SplitColumn& column, unsigned width, std::uint8_t* out, MemoryCharge& charge) {
  for (std::size_t c = 0; c < column.narrow.chunkCount(); ++c) {
    for (const std::uint32_t value : column.narrow.chunk(c)) out = putEntry(out, value, width);
    column.narrow.dropChunk(c, charge);
  }
  for (std::size_t c = 0; c < column.wide.chunkCount(); ++c) {
    for (const std::uint64_t value : column.wide.chunk(c)) out = putEntry(out, value, width);
    column.wide.dropChunk(c, charge);
  }
  return out;
}

}

std::uint64_t ValueColumn::get(std::size_t i) const {
  switch (width_) {
    case 1: return bytes1_.get(i);
    case 2: return bytes2_.get(i);
    case 4: return bytes4_.get(i);
    case 8: return bytes8_.get(i);
    default: return 0;
  }
}

void ValueColumn::widen(std::size_t width, MemoryCharge& charge) {
  switch (width) {
    case 1:
      for (std::size_t i = 0; i < size_; ++i) bytes1_.push(0, charge);
      break;
    case 2:
      if (width_ == 0) {
        for (std::size_t i = 0; i < size_; ++i) bytes2_.push(0, charge);
      } else {
        moveInto(bytes1_, bytes2_, charge);
      }
      break;
    case 4:
      if (width_ == 0) {
        for (std::size_t i = 0; i < size_; ++i) bytes4_.push(0, charge);
      } else {
        moveAnyInto(width_, bytes1_, bytes2_, bytes4_, bytes4_, charge);
      }
      break;
    default:
      if (width_ == 0) {
        for (std::size_t i = 0; i < size_; ++i) bytes8_.push(0, charge);
      } else {
        moveAnyInto(width_, bytes1_, bytes2_, bytes4_, bytes8_, charge);
      }
      break;
  }
  width_ = width;
}

void ValueColumn::push(std::uint64_t value, MemoryCharge& charge) {
  const std::size_t needed = widthOf(value);
  if (needed > width_) widen(needed, charge);
  switch (width_) {
    case 1: bytes1_.push(static_cast<std::uint8_t>(value), charge); break;
    case 2: bytes2_.push(static_cast<std::uint16_t>(value), charge); break;
    case 4: bytes4_.push(static_cast<std::uint32_t>(value), charge); break;
    case 8: bytes8_.push(value, charge); break;
    default: break;
  }
  ++size_;
}

void ValueColumn::shrinkToFit(MemoryCharge& charge) {
  bytes1_.shrinkToFit(charge);
  bytes2_.shrinkToFit(charge);
  bytes4_.shrinkToFit(charge);
  bytes8_.shrinkToFit(charge);
}

std::size_t ValueColumn::bytes() const { return bytes1_.bytes() + bytes2_.bytes() + bytes4_.bytes() + bytes8_.bytes(); }

std::size_t ValueColumn::unusedBytes() const {
  return bytes1_.unusedBytes() + bytes2_.unusedBytes() + bytes4_.unusedBytes() + bytes8_.unusedBytes();
}

std::size_t AnnotationRange::count() const {
  std::size_t n = 0;
  for (const GroupSpan& span : groups) n += span.last - span.first;
  return n;
}

std::uint64_t AnnotationGroups::Group::blockCovered(std::size_t k) const {
  const std::uint8_t* const at = blocks.get() + blockCount() * beginWidth + k * coveredWidth;
  switch (coveredWidth) {
    case 0: return std::uint64_t{k} * kBlockEntries * usualLength;
    case 4: {
      std::uint32_t samples;
      std::memcpy(&samples, at, 4);
      return samples;
    }
    default: {
      std::uint64_t samples;
      std::memcpy(&samples, at, 8);
      return samples;
    }
  }
}

std::uint64_t AnnotationGroups::Group::offsetAt(std::size_t k) const {
  const std::uint8_t* const at = blocks.get() + blockCount() * (beginWidth + coveredWidth) + k * offsetWidth;
  if (offsetWidth == 4) {
    std::uint32_t offset;
    std::memcpy(&offset, at, 4);
    return offset;
  }
  std::uint64_t offset;
  std::memcpy(&offset, at, 8);
  return offset;
}

const std::uint8_t* AnnotationGroups::Group::blockPayload(std::size_t k) const {
  if (offsetWidth == 0) return nullptr;
  const std::uint64_t from = offsetAt(k);
  if (offsetAt(k + 1) == from) return nullptr;
  const std::size_t count = blockCount();
  return blocks.get() + count * (beginWidth + coveredWidth) + (count + 1) * offsetWidth + from;
}

BlockFields AnnotationGroups::Group::blockFields(std::size_t k) const {
  return describeBlock(blockPayload(k), (std::min)(kBlockEntries, n - k * kBlockEntries),
                       {usualStep, usualLength, usualValue});
}

std::size_t AnnotationGroups::Group::blockArrayBytes() const {
  if (!blocks) return 0;
  const std::size_t count = blockCount();
  const std::size_t table = count * (beginWidth + coveredWidth);
  if (offsetWidth == 0) return table;
  return table + (count + 1) * offsetWidth + static_cast<std::size_t>(offsetAt(count)) + kBitPadding;
}

std::uint64_t AnnotationGroups::Group::longLength(std::size_t i) const {
  const auto found = std::lower_bound(longLengths.begin(), longLengths.end(), i,
                                      [](const std::pair<std::size_t, std::uint64_t>& e, std::size_t at) {
                                        return e.first < at;
                                      });
  return found != longLengths.end() && found->first == i ? found->second : kLongLength;
}

std::uint64_t AnnotationGroups::Group::lengthAt(std::size_t i) const {
  return lengthIn(blockFields(i / kBlockEntries).lengths, i);
}

std::uint64_t AnnotationGroups::Group::valueAt(std::size_t i) const {
  return fieldEntry(blockFields(i / kBlockEntries).values, i % kBlockEntries);
}

std::uint64_t AnnotationGroups::Group::coveredBefore(std::size_t i, const BlockField& lengths,
                                                     std::size_t& reads) const {
  const std::size_t k = i / kBlockEntries;
  const std::size_t from = k * kBlockEntries;
  if (lengths.width == 0 && lengths.min < kLongLength) return blockCovered(k) + (i - from) * lengths.min;
  const std::size_t to = (std::min)(from + kBlockEntries, n);
  if (i - from <= to - i) {
    std::uint64_t sum = blockCovered(k);
    for (std::size_t j = from; j < i; ++j) sum += lengthIn(lengths, j);
    reads += i - from;
    return sum;
  }
  std::uint64_t sum = to == n ? covered : blockCovered(k + 1);
  for (std::size_t j = i; j < to; ++j) sum -= lengthIn(lengths, j);
  reads += to - i;
  return sum;
}

std::uint64_t AnnotationGroups::begin(std::size_t g, std::size_t i) const {
  const Group& group = groups_.data()[g];
  const std::size_t k = i / kCoverageStride;
  const std::size_t j = i % kCoverageStride;
  const std::uint64_t base = group.blockBegin(k);
  if (j == 0) return base;
  return beginInBlock(group.blockFields(k).steps, base, j);
}

std::uint64_t AnnotationGroups::length(std::size_t g, std::size_t i) const { return groups_.data()[g].lengthAt(i); }

std::uint64_t AnnotationGroups::value(std::size_t g, std::size_t i) const { return groups_.data()[g].valueAt(i); }

decode::Annotation AnnotationGroups::at(AnnotationRef ref) const {
  const Group& group = groups_.data()[ref.group];
  const std::size_t k = ref.index / kBlockEntries;
  const std::size_t j = ref.index % kBlockEntries;
  const BlockFields fields = group.blockFields(k);
  const std::uint64_t base = group.blockBegin(k);
  const std::uint64_t b = j == 0 ? base : beginInBlock(fields.steps, base, j);
  return decode::Annotation{b, b + group.lengthIn(fields.lengths, ref.index), group.row, group.cls,
                            fieldEntry(fields.values, j)};
}

std::span<const std::uint8_t> AnnotationGroups::bytesAt(std::size_t g, std::uint64_t reference) const {
  const std::uint64_t key = keyOf(groups_.data()[g].row, groups_.data()[g].cls);
  const auto found = std::lower_bound(bytePools_.begin(), bytePools_.end(), key,
                                      [](const BytePool& pool, std::uint64_t wanted) { return pool.key < wanted; });
  if (found == bytePools_.end() || found->key != key) return {};
  const std::vector<std::uint8_t>& pool = found->bytes;
  if (reference + kBytesLengthPrefix > pool.size()) return {};
  std::uint32_t length;
  std::memcpy(&length, pool.data() + reference, kBytesLengthPrefix);
  if (reference + kBytesLengthPrefix + length > pool.size()) return {};
  return {pool.data() + reference + kBytesLengthPrefix, length};
}

std::uint64_t AnnotationGroups::coveredBefore(std::size_t g, std::size_t i, std::size_t& reads) const {
  const Group& group = groups_.data()[g];
  if (i >= group.n) return group.covered;
  return group.coveredBefore(i, group.blockFields(i / kCoverageStride).lengths, reads);
}

std::pair<std::size_t, std::size_t> AnnotationGroups::rowGroups(std::uint32_t row) const {
  const auto first = std::lower_bound(groups_.begin(), groups_.end(), row,
                                      [](const Group& g, std::uint32_t r) { return g.row < r; });
  const auto last = std::upper_bound(first, groups_.end(), row,
                                     [](std::uint32_t r, const Group& g) { return r < g.row; });
  return {static_cast<std::size_t>(first - groups_.begin()), static_cast<std::size_t>(last - groups_.begin())};
}

std::optional<std::size_t> AnnotationGroups::findGroup(std::uint32_t row, std::uint32_t cls) const {
  const auto [first, last] = rowGroups(row);
  for (std::size_t g = first; g < last; ++g) {
    if (groups_[g].cls == cls) return g;
  }
  return std::nullopt;
}

std::vector<std::uint32_t> AnnotationGroups::rowClasses(std::uint32_t row) const {
  std::vector<std::uint32_t> classes;
  const auto [first, last] = rowGroups(row);
  for (std::size_t g = first; g < last; ++g) classes.push_back(groups_[g].cls);
  return classes;
}

std::optional<AnnotationRef> AnnotationGroups::firstReaching(std::uint64_t sample) const {
  return firstEndingAfter(*this, [sample](std::uint64_t end) { return end > sample; });
}

AnnotationRange AnnotationGroups::range(std::uint64_t begin, std::uint64_t end) const {
  AnnotationRange out;
  if (begin >= end) return out;
  AnnotationReader reader(*this);
  const std::optional<AnnotationRef> first =
    firstEndingAfter(reader, [begin](std::uint64_t annotationEnd) { return annotationEnd > begin; });
  if (!first) return out;
  const std::uint64_t firstBegin = reader.begin(first->group, first->index);
  out.groups.resize(groups_.size());
  std::size_t probes = 0;
  for (std::size_t g = 0; g < groups_.size(); ++g) {
    const auto beginsBelow = [&](std::uint64_t sample, bool orEqual) {
      return reader.firstBeginNotBefore(
        g, 0, [sample, orEqual](std::uint64_t b) { return b < sample || (orEqual && b == sample); }, probes);
    };
    std::size_t lo = 0;
    if (g < first->group) {
      lo = beginsBelow(firstBegin, true);
    } else if (g == first->group) {
      lo = first->index;
    } else {
      lo = beginsBelow(firstBegin, false);
    }
    const std::size_t hi = (std::max)(lo, beginsBelow(end, false));
    out.groups[g] = {lo, hi};
  }
  return out;
}

std::vector<decode::Annotation> AnnotationGroups::collect(const AnnotationRange& range) const {
  AnnotationReader reader(*this);
  return waveform_view::collect(reader, range);
}

std::vector<decode::Annotation> AnnotationGroups::collect(const AnnotationRange& range,
                                                          std::span<const std::size_t> groupNumbers) const {
  AnnotationReader reader(*this);
  return waveform_view::collect(reader, range, groupNumbers);
}

std::vector<decode::Annotation> AnnotationGroups::inOrder() const {
  AnnotationReader reader(*this);
  return waveform_view::inOrder(reader);
}

std::size_t AnnotationGroups::memoryBytes() const {
  std::size_t bytes = vectorBytes(groups_) + vectorBytes(rowIds_) + vectorBytes(rowClassPairs_);
  for (const Group& g : groups_) bytes += g.bytes();
  bytes += vectorBytes(bytePools_);
  for (const BytePool& pool : bytePools_) bytes += vectorBytes(pool.bytes);
  return bytes;
}

std::size_t AnnotationGroups::blockBytes(std::size_t g) const { return groups_[g].blockArrayBytes(); }

std::span<const std::uint8_t> AnnotationGroups::blockArray(std::size_t g) const {
  return {groups_[g].blocks.get(), groups_[g].blockArrayBytes()};
}

std::size_t AnnotationGroups::Group::bytes() const {
  return blockArrayBytes() + vectorBytes(longLengths) + vectorBytes(maxEnds) + vectorBytes(sortedEnds) +
         coverage.bytes();
}

struct AnnotationGroupsBuilder::BuildGroup {
  using Group = AnnotationGroups::Group;
  static constexpr std::size_t kNone = static_cast<std::size_t>(-1);

  std::array<std::uint64_t, kBlockEntries> open;
  std::array<std::uint32_t, kBlockEntries> openLengths;
  std::array<std::uint64_t, kBlockEntries> openValues;
  std::size_t openCount = 0;
  std::uint64_t openCovered = 0;
  std::uint64_t firstBegin = 0;
  std::uint64_t largestDistance = 0;
  std::uint64_t lastBegin = 0;
  std::uint64_t lastEnd = 0;
  std::uint64_t maxEnd = 0;
  std::uint64_t largestValue = 0;
  UsualValues usual;
  bool sorted = true;
  bool lengthsField = false;
  std::size_t lengthsFrom = kNone;
  std::size_t irregularFrom = kNone;
  std::size_t nonPositiveFrom = kNone;
  std::array<std::size_t, 4> widerFrom{kNone, kNone, kNone, kNone};
  std::size_t payloadFrom = kNone;
  SplitColumn firsts;
  SplitColumn covereds;
  SplitColumn offsets;
  AppendColumn<std::uint8_t> payload;
  std::atomic<std::size_t> published{0};
  std::optional<CoveragePyramidBuilder> coverage;
  bool coverageOverCap = false;

  struct Entries {
    std::array<std::uint64_t, kBlockEntries> begins;
    std::array<std::uint32_t, kBlockEntries> lengths;
    std::array<std::uint64_t, kBlockEntries> values;
  };

  void append(Group& group, const decode::Annotation& a, MemoryCharge& charge, std::size_t coverageCap = 0);
  void close(MemoryCharge& charge);
  void flatten(Group& group, MemoryCharge& charge);
  std::size_t openBytes() const {
    return sizeof(BuildGroup) + firsts.unusedBytes() + covereds.unusedBytes() + offsets.unusedBytes() +
           payload.unusedBytes() + (coverage ? coverage->unusedBytes() : 0);
  }
  std::size_t storedBytes() const {
    const auto columnBytes = [](const SplitColumn& c) {
      return c.narrow.size() * sizeof(std::uint32_t) + c.wide.size() * sizeof(std::uint64_t);
    };
    return columnBytes(firsts) + columnBytes(covereds) + columnBytes(offsets) + payload.size() +
           openCount * sizeof(std::uint32_t);
  }
  bool coverageEligible(const Group& group) const { return sorted && group.regular && group.n != 0; }
  void startCoverage(const Group& group, unsigned shift, MemoryCharge& charge);
  void stopCoverage(MemoryCharge& charge);
  std::pair<std::uint64_t, std::uint64_t> spanAt(const Group& group, std::size_t i, Entries& cache,
                                                 std::size_t& cached) const;
  std::pair<std::uint64_t, std::uint64_t> coverageTail(const Group& group, std::size_t m, unsigned shift) const;
  void closedBytes(std::vector<std::span<const std::uint8_t>>& out) const;
  bool coveredNeeded() const { return lengthsField || usual.length == kLengthMarker; }
  std::uint64_t usualCovered(std::size_t k) const { return std::uint64_t{k} * kBlockEntries * usual.length; }
  std::uint64_t closedFirst(std::size_t k) const { return firsts.get(k, firstBegin); }
  std::uint64_t closedOffset(std::size_t k) const;
  std::uint64_t coveredBefore(const Group& group, std::size_t k) const;
  void readClosed(std::size_t k, Entries& out) const;
  template <class Fn>
  void forEachEntry(const Group& group, Fn fn) const;
  std::size_t countBelow(const Group& group, std::uint64_t p) const;
  std::size_t valueBytesBelow(std::size_t m) const;
  Group prefix(const Group& group, std::size_t m, MemoryCharge& charge) const;
  Group sortedBelow(const Group& group, std::uint64_t p, std::size_t count, unsigned coverageShift,
                    std::size_t coverageCap, MemoryCharge& charge) const;
  static std::uint64_t lengthOf(const Group& group, std::size_t i, std::uint32_t stored);

  static void sortGroup(Group& group, MemoryCharge& charge);
  static void buildCoverage(Group& group, std::uint64_t end, unsigned shift, std::size_t cap, MemoryCharge& charge);
  static void buildEnds(Group& group, MemoryCharge& charge);
  template <class Fn>
  static void forEachSpan(const Group& group, Fn fn);
};

void AnnotationGroupsBuilder::BuildGroup::append(Group& group, const decode::Annotation& a, MemoryCharge& charge,
                                                 std::size_t coverageCap) {
  const std::size_t i = group.n;
  if (i == 0) firstBegin = a.begin;
  if (openCount == 0) openCovered = group.covered;
  const std::uint64_t length = a.end - a.begin;
  const bool longLength = length >= AnnotationGroups::kLongLength;
  if (longLength) {
    roomForOneMore(group.longLengths, charge);
    group.longLengths.emplace_back(i, length);
  }
  group.covered += length;
  group.n = i + 1;
  if (i > 0 && a.begin < lastBegin) sorted = false;
  if (a.end <= a.begin) {
    group.positive = false;
    if (nonPositiveFrom == kNone) nonPositiveFrom = i;
  }
  if (a.end <= a.begin || (i > 0 && a.begin < lastEnd)) {
    group.regular = false;
    if (irregularFrom == kNone) irregularFrom = i;
  }
  lastBegin = a.begin;
  lastEnd = a.end;
  maxEnd = (std::max)(maxEnd, a.end);
  openLengths[openCount] = longLength ? AnnotationGroups::kLongLength : static_cast<std::uint32_t>(length);
  openValues[openCount] = a.value;
  if (a.value > largestValue) {
    const std::size_t width = widthOf(a.value);
    for (std::size_t w = 0; w < widerFrom.size(); ++w) {
      if ((std::size_t{1} << w) <= width && widerFrom[w] == kNone) widerFrom[w] = i;
    }
    largestValue = a.value;
  }
  open[openCount++] = a.begin;
  if (openCount == kBlockEntries) close(charge);
  if (coverage) {
    if (!coverageEligible(group)) {
      stopCoverage(charge);
    } else if (!coverage->add(a.begin, length, charge, coverageCap)) {
      stopCoverage(charge);
      coverageOverCap = true;
    }
  }
}

void AnnotationGroupsBuilder::BuildGroup::close(MemoryCharge& charge) {
  const std::span<const std::uint64_t> begins(open.data(), openCount);
  const std::span<const std::uint32_t> lengths(openLengths.data(), openCount);
  const std::span<const std::uint64_t> values(openValues.data(), openCount);
  const std::size_t k = firsts.size();
  if (k == 0) usual = {usualStepOf(begins), usualLengthOf(lengths), usualValueOf(values)};
  std::array<std::uint8_t, kMostPayloadBytes> packed;
  const std::size_t size = packBlock(begins, lengths, values, usual, packed.data());
  if (size != 0 && (packed[0] & kLengthsPresent) != 0 && !lengthsField) {
    lengthsField = true;
    lengthsFrom = k;
  }
  if (coveredNeeded()) {
    for (std::size_t m = covereds.size(); m < k; ++m) covereds.push(usualCovered(m), 0, charge);
    covereds.push(openCovered, 0, charge);
  }
  if (size != 0 && payloadFrom == kNone) payloadFrom = k;
  if (payloadFrom != kNone) offsets.push(payload.size(), 0, charge);
  payload.append(std::span<const std::uint8_t>(packed.data(), size), charge);
  largestDistance = (std::max)(largestDistance, open[0] - firstBegin);
  // WHY: Push first begin LAST: closedBlocks() counts first begins, so a block counts once all its bytes are written.
  firsts.push(open[0], firstBegin, charge);
  openCount = 0;
  published.store((k + 1) * kBlockEntries, std::memory_order_release);
}

void AnnotationGroupsBuilder::BuildGroup::flatten(Group& group, MemoryCharge& charge) {
  const std::size_t closed = firsts.size();
  const std::size_t blocks = closed + (openCount != 0 ? 1 : 0);
  std::array<std::uint8_t, kMostPayloadBytes> last;
  std::size_t lastSize = 0;
  std::uint64_t largest = largestDistance;
  if (openCount != 0) {
    const std::span<const std::uint64_t> begins(open.data(), openCount);
    const std::span<const std::uint32_t> lengths(openLengths.data(), openCount);
    const std::span<const std::uint64_t> values(openValues.data(), openCount);
    if (closed == 0) usual = {usualStepOf(begins), usualLengthOf(lengths), usualValueOf(values)};
    lastSize = packBlock(begins, lengths, values, usual, last.data());
    lengthsField = lengthsField || (lastSize != 0 && (last[0] & kLengthsPresent) != 0);
    largest = (std::max)(largest, open[0] - firstBegin);
  }
  const std::size_t closedPayload = payload.size();
  const std::size_t payloadBytes = closedPayload + lastSize;
  const BlockTableForms forms = blockTableForms(largest, payloadBytes);
  const std::uint8_t coveredWidth = coveredWidthOf(lengthsField, usual.length, group.regular, group.covered);
  const std::size_t offsetBytes = forms.offsetWidth == 0 ? 0 : (blocks + 1) * forms.offsetWidth;
  const std::size_t bytes = blocks * (forms.beginWidth + coveredWidth) + offsetBytes + payloadBytes +
                            (payloadBytes == 0 ? 0 : kBitPadding);
  charge.acquire(bytes);
  auto array = std::make_unique_for_overwrite<std::uint8_t[]>(bytes);
  std::uint8_t* out = moveOut(firsts, firstBegin, forms.beginWidth, array.get(), charge);
  if (openCount != 0) {
    out = putEntry(out, forms.beginWidth == 4 ? open[0] - firstBegin : open[0], forms.beginWidth);
  }
  if (coveredWidth != 0) {
    const std::size_t written = covereds.size();
    out = moveOutValues(covereds, coveredWidth, out, charge);
    for (std::size_t k = written; k < closed; ++k) out = putEntry(out, usualCovered(k), coveredWidth);
    if (openCount != 0) out = putEntry(out, openCovered, coveredWidth);
  }
  if (forms.offsetWidth != 0) {
    const std::size_t zeros = payloadFrom == kNone ? closed : payloadFrom;
    for (std::size_t k = 0; k < zeros; ++k) out = putEntry(out, 0, forms.offsetWidth);
    out = moveOut(offsets, 0, forms.offsetWidth, out, charge);
    if (openCount != 0) out = putEntry(out, closedPayload, forms.offsetWidth);
    out = putEntry(out, payloadBytes, forms.offsetWidth);
  }
  out = moveOut(payload, out, charge);
  if (lastSize != 0) std::memcpy(out, last.data(), lastSize);
  out += lastSize;
  if (payloadBytes != 0) std::fill_n(out, kBitPadding, std::uint8_t{0});
  firsts.clear(charge);
  covereds.clear(charge);
  offsets.clear(charge);
  payload.clear(charge);
  openCount = 0;
  group.blocks = std::move(array);
  group.firstBegin = firstBegin;
  group.usualStep = usual.step;
  group.usualLength = usual.length;
  group.usualValue = usual.value;
  group.beginWidth = forms.beginWidth;
  group.offsetWidth = forms.offsetWidth;
  group.coveredWidth = coveredWidth;
  group.valueBytes = static_cast<std::uint8_t>(widthOf(largestValue));
}

void AnnotationGroupsBuilder::BuildGroup::closedBytes(std::vector<std::span<const std::uint8_t>>& out) const {
  const auto pieces = [&out](const auto& column) {
    for (std::size_t c = 0; c < column.chunkCount(); ++c) {
      const auto entries = column.chunk(c);
      out.push_back({reinterpret_cast<const std::uint8_t*>(entries.data()), entries.size_bytes()});
    }
  };
  pieces(firsts.narrow);
  pieces(firsts.wide);
  pieces(covereds.narrow);
  pieces(covereds.wide);
  pieces(offsets.narrow);
  pieces(offsets.wide);
  pieces(payload);
}

template <class Fn>
void AnnotationGroupsBuilder::BuildGroup::forEachSpan(const Group& group, Fn fn) {
  std::array<std::uint64_t, kBlockEntries> begins;
  for (std::size_t k = 0, first = 0; first < group.n; ++k, first += kBlockEntries) {
    const std::size_t count = (std::min)(kBlockEntries, group.n - first);
    const BlockFields fields = group.blockFields(k);
    begins[0] = group.blockBegin(k);
    if (count > 1) sumBegins(fields.steps, begins.data(), 1, count);
    for (std::size_t j = 0; j < count; ++j) fn(begins[j], group.lengthIn(fields.lengths, first + j));
  }
}

void AnnotationGroupsBuilder::BuildGroup::sortGroup(Group& group, MemoryCharge& charge) {
  const std::size_t n = group.n;
  const std::size_t temporary = n * (sizeof(std::uint64_t) + 2 * sizeof(std::size_t));
  charge.acquire(temporary);
  std::vector<std::uint64_t> begins;
  begins.reserve(n);
  forEachSpan(group, [&](std::uint64_t b, std::uint64_t) { begins.push_back(b); });
  std::vector<std::size_t> order(n);
  std::iota(order.begin(), order.end(), std::size_t{0});
  std::stable_sort(order.begin(), order.end(), [&](std::size_t a, std::size_t b) { return begins[a] < begins[b]; });
  Group sorted;
  sorted.row = group.row;
  sorted.cls = group.cls;
  charge.acquire(sizeof(BuildGroup));
  auto build = std::make_unique<BuildGroup>();
  for (const std::size_t i : order) {
    const std::uint64_t b = begins[i];
    build->append(sorted, {b, b + group.lengthAt(i), group.row, group.cls, group.valueAt(i)}, charge);
  }
  build->flatten(sorted, charge);
  build.reset();
  const std::size_t unsortedBytes = group.bytes();
  group = std::move(sorted);
  charge.release(unsortedBytes + temporary + sizeof(BuildGroup));
}

void AnnotationGroupsBuilder::BuildGroup::buildEnds(Group& group, MemoryCharge& charge) {
  const std::size_t n = group.n;
  const std::size_t endsBytes = 2 * n * sizeof(std::uint64_t);
  charge.acquire(endsBytes);
  group.maxEnds.resize(n);
  group.sortedEnds.resize(n);
  charge.release(endsBytes - vectorBytes(group.maxEnds) - vectorBytes(group.sortedEnds));
  std::uint64_t running = 0;
  std::size_t i = 0;
  forEachSpan(group, [&](std::uint64_t b, std::uint64_t length) {
    const std::uint64_t e = b + length;
    running = (std::max)(running, e);
    group.maxEnds[i] = running;
    group.sortedEnds[i] = e;
    ++i;
  });
  std::sort(group.sortedEnds.begin(), group.sortedEnds.end());
}

std::uint64_t AnnotationGroupsBuilder::BuildGroup::closedOffset(std::size_t k) const {
  if (k >= firsts.size()) return payload.size();
  if (payloadFrom == kNone || k < payloadFrom) return 0;
  return offsets.get(k - payloadFrom, 0);
}

std::uint64_t AnnotationGroupsBuilder::BuildGroup::coveredBefore(const Group& group, std::size_t k) const {
  if (k < covereds.size()) return covereds.get(k, 0);
  if (k < firsts.size()) return usualCovered(k);
  return openCount != 0 ? openCovered : group.covered;
}

void AnnotationGroupsBuilder::BuildGroup::readClosed(std::size_t k, Entries& out) const {
  const std::uint64_t from = closedOffset(k);
  const std::uint64_t to = closedOffset(k + 1);
  // WHY: Zero-initialised: a block without payload reads as flags 0, every field usual.
  std::array<std::uint8_t, kMostPayloadBytes + kBitPadding> bytes{};
  payload.copyBytes(from, to - from, bytes.data());
  const BlockFields fields = describeBlock(bytes.data(), kBlockEntries, usual);
  out.begins[0] = closedFirst(k);
  sumBegins(fields.steps, out.begins.data(), 1, kBlockEntries);
  for (std::size_t t = 0; t < kBlockEntries; ++t) {
    out.lengths[t] = static_cast<std::uint32_t>(fieldEntry(fields.lengths, t));
    out.values[t] = fieldEntry(fields.values, t);
  }
}

std::uint64_t AnnotationGroupsBuilder::BuildGroup::lengthOf(const Group& group, std::size_t i, std::uint32_t stored) {
  if (stored != AnnotationGroups::kLongLength) return stored;
  const auto found = std::lower_bound(group.longLengths.begin(), group.longLengths.end(), i,
                                      [](const std::pair<std::size_t, std::uint64_t>& e, std::size_t at) {
                                        return e.first < at;
                                      });
  return found->second;
}

template <class Fn>
void AnnotationGroupsBuilder::BuildGroup::forEachEntry(const Group& group, Fn fn) const {
  Entries block;
  for (std::size_t k = 0; k < firsts.size(); ++k) {
    readClosed(k, block);
    for (std::size_t t = 0; t < kBlockEntries; ++t) {
      fn(block.begins[t], lengthOf(group, k * kBlockEntries + t, block.lengths[t]), block.values[t]);
    }
  }
  const std::size_t first = firsts.size() * kBlockEntries;
  for (std::size_t t = 0; t < openCount; ++t) fn(open[t], lengthOf(group, first + t, openLengths[t]), openValues[t]);
}

std::size_t AnnotationGroupsBuilder::BuildGroup::countBelow(const Group& group, std::uint64_t p) const {
  if (!sorted) {
    std::size_t count = 0;
    forEachEntry(group, [&](std::uint64_t b, std::uint64_t, std::uint64_t) { count += b < p ? 1 : 0; });
    return count;
  }
  const std::size_t closed = firsts.size();
  std::size_t lo = 0;
  std::size_t hi = closed;
  while (lo < hi) {
    const std::size_t mid = lo + (hi - lo) / 2;
    if (closedFirst(mid) < p) {
      lo = mid + 1;
    } else {
      hi = mid;
    }
  }
  if (lo == 0 && closed != 0) return 0;
  if (lo != 0) {
    Entries block;
    readClosed(lo - 1, block);
    const auto inBlock = static_cast<std::size_t>(std::lower_bound(block.begins.begin(), block.begins.end(), p) -
                                                  block.begins.begin());
    if (inBlock < kBlockEntries || lo < closed) return (lo - 1) * kBlockEntries + inBlock;
  }
  const auto inOpen =
    static_cast<std::size_t>(std::lower_bound(open.begin(), open.begin() + openCount, p) - open.begin());
  return closed * kBlockEntries + inOpen;
}

std::size_t AnnotationGroupsBuilder::BuildGroup::valueBytesBelow(std::size_t m) const {
  std::size_t bytes = 0;
  for (std::size_t w = 0; w < widerFrom.size(); ++w) {
    if (widerFrom[w] < m) bytes = std::size_t{1} << w;
  }
  return bytes;
}

AnnotationGroups::Group AnnotationGroupsBuilder::BuildGroup::prefix(const Group& group, std::size_t m,
                                                                   MemoryCharge& charge) const {
  const std::size_t closed = firsts.size();
  const std::size_t k = m / kBlockEntries;
  const std::size_t r = m % kBlockEntries;
  Entries cut;
  if (r != 0 && k < closed) {
    readClosed(k, cut);
  } else if (r != 0) {
    std::copy_n(open.begin(), r, cut.begins.begin());
    std::copy_n(openLengths.begin(), r, cut.lengths.begin());
    std::copy_n(openValues.begin(), r, cut.values.begin());
  }
  const std::span<const std::uint64_t> begins(cut.begins.data(), r);
  const std::span<const std::uint32_t> lengths(cut.lengths.data(), r);
  const std::span<const std::uint64_t> values(cut.values.data(), r);
  const UsualValues u = k != 0 ? usual : UsualValues{usualStepOf(begins), usualLengthOf(lengths), usualValueOf(values)};
  std::array<std::uint8_t, kMostPayloadBytes> last;
  const std::size_t lastSize = r == 0 ? 0 : packBlock(begins, lengths, values, u, last.data());
  const bool lengthsPresent = lengthsFrom < k || (lastSize != 0 && (last[0] & kLengthsPresent) != 0);
  std::uint64_t covered = coveredBefore(group, k);
  for (std::size_t t = 0; t < r; ++t) covered += lengthOf(group, k * kBlockEntries + t, cut.lengths[t]);
  const std::uint64_t largest = (r != 0 ? cut.begins[0] : k != 0 ? closedFirst(k - 1) : firstBegin) - firstBegin;
  const std::uint64_t closedPayload = closedOffset(k);
  const std::uint64_t payloadBytes = closedPayload + lastSize;
  const BlockTableForms forms = blockTableForms(largest, payloadBytes);
  const bool regular = irregularFrom >= m;
  const std::uint8_t coveredWidth = coveredWidthOf(lengthsPresent, u.length, regular, covered);
  const std::size_t blocks = k + (r != 0 ? 1 : 0);
  const std::size_t offsetBytes = forms.offsetWidth == 0 ? 0 : (blocks + 1) * forms.offsetWidth;
  const std::size_t bytes = blocks * (forms.beginWidth + coveredWidth) + offsetBytes + payloadBytes +
                            (payloadBytes == 0 ? 0 : kBitPadding);

  Group out;
  out.row = group.row;
  out.cls = group.cls;
  const auto longEnd = std::lower_bound(group.longLengths.begin(), group.longLengths.end(), m,
                                        [](const std::pair<std::size_t, std::uint64_t>& e, std::size_t at) {
                                          return e.first < at;
                                        });
  const auto longs = static_cast<std::size_t>(longEnd - group.longLengths.begin());
  if (longs != 0) {
    chargedVector(out.longLengths, charge, longs * sizeof(out.longLengths.front()),
                  [&] { out.longLengths.reserve(longs); });
    out.longLengths.assign(group.longLengths.begin(), longEnd);
  }
  charge.acquire(bytes);
  out.blocks = std::make_unique_for_overwrite<std::uint8_t[]>(bytes);
  std::uint8_t* o = copyOut(firsts, k, firstBegin, forms.beginWidth, out.blocks.get());
  if (r != 0) o = putEntry(o, forms.beginWidth == 4 ? cut.begins[0] - firstBegin : cut.begins[0], forms.beginWidth);
  if (coveredWidth != 0) {
    if (covereds.size() == 0) {
      for (std::size_t j = 0; j < k; ++j) o = putEntry(o, usualCovered(j), coveredWidth);
    } else {
      o = copyOut(covereds, k, 0, coveredWidth, o);
    }
    if (r != 0) o = putEntry(o, coveredBefore(group, k), coveredWidth);
  }
  if (forms.offsetWidth != 0) {
    const std::size_t zeros = (std::min)(payloadFrom, k);
    for (std::size_t j = 0; j < zeros; ++j) o = putEntry(o, 0, forms.offsetWidth);
    o = copyOut(offsets, k - zeros, 0, forms.offsetWidth, o);
    if (r != 0) o = putEntry(o, closedPayload, forms.offsetWidth);
    o = putEntry(o, payloadBytes, forms.offsetWidth);
  }
  o = payload.copyBytes(0, closedPayload, o);
  if (lastSize != 0) std::memcpy(o, last.data(), lastSize);
  o += lastSize;
  if (payloadBytes != 0) std::fill_n(o, kBitPadding, std::uint8_t{0});
  out.n = m;
  out.firstBegin = firstBegin;
  out.usualStep = u.step;
  out.usualLength = u.length;
  out.usualValue = u.value;
  out.covered = covered;
  out.positive = nonPositiveFrom >= m;
  out.regular = regular;
  out.beginWidth = forms.beginWidth;
  out.offsetWidth = forms.offsetWidth;
  out.coveredWidth = coveredWidth;
  out.valueBytes = static_cast<std::uint8_t>(valueBytesBelow(m));
  if (!out.regular) buildEnds(out, charge);
  if (coverage && coverage->started() && out.regular) {
    if (m == group.n) {
      out.coverage = coverage->finished(charge);
    } else {
      const auto [end, inLast] = coverageTail(group, m, coverage->shift());
      out.coverage = coverage->finishedBefore(end, inLast, covered, charge);
    }
  }
  return out;
}

AnnotationGroups::Group AnnotationGroupsBuilder::BuildGroup::sortedBelow(const Group& group, std::uint64_t p,
                                                                        std::size_t count, unsigned coverageShift,
                                                                        std::size_t coverageCap,
                                                                        MemoryCharge& charge) const {
  const std::size_t temporary = 2 * count * sizeof(decode::Annotation);
  charge.acquire(temporary);
  std::vector<decode::Annotation> below;
  below.reserve(count);
  forEachEntry(group, [&](std::uint64_t b, std::uint64_t length, std::uint64_t value) {
    if (b < p) below.push_back({b, b + length, group.row, group.cls, value});
  });
  std::stable_sort(below.begin(), below.end(),
                   [](const decode::Annotation& a, const decode::Annotation& b) { return a.begin < b.begin; });
  Group out;
  out.row = group.row;
  out.cls = group.cls;
  charge.acquire(sizeof(BuildGroup));
  auto build = std::make_unique<BuildGroup>();
  if (coverageShift != 0) build->coverage.emplace(coverageShift);
  for (const decode::Annotation& a : below) build->append(out, a, charge, coverageCap);
  build->flatten(out, charge);
  if (build->coverage && out.regular) out.coverage = build->coverage->finished(charge);
  build->stopCoverage(charge);
  build.reset();
  charge.release(sizeof(BuildGroup));
  shrinkVector(out.longLengths, charge);
  if (!out.regular) buildEnds(out, charge);
  below = std::vector<decode::Annotation>();
  charge.release(temporary);
  return out;
}

void AnnotationGroupsBuilder::BuildGroup::startCoverage(const Group& group, unsigned shift, MemoryCharge& charge) {
  stopCoverage(charge);
  if (!coverageEligible(group)) return;
  coverage.emplace(shift);
  forEachEntry(group, [&](std::uint64_t b, std::uint64_t length, std::uint64_t) { coverage->add(b, length, charge); });
}

void AnnotationGroupsBuilder::BuildGroup::stopCoverage(MemoryCharge& charge) {
  if (!coverage) return;
  coverage->clear(charge);
  coverage.reset();
}

std::pair<std::uint64_t, std::uint64_t> AnnotationGroupsBuilder::BuildGroup::spanAt(const Group& group, std::size_t i,
                                                                                    Entries& cache,
                                                                                    std::size_t& cached) const {
  const std::size_t k = i / kBlockEntries;
  const std::size_t t = i % kBlockEntries;
  if (k < firsts.size()) {
    if (cached != k) {
      readClosed(k, cache);
      cached = k;
    }
    return {cache.begins[t], lengthOf(group, i, cache.lengths[t])};
  }
  return {open[t], lengthOf(group, i, openLengths[t])};
}

std::pair<std::uint64_t, std::uint64_t> AnnotationGroupsBuilder::BuildGroup::coverageTail(const Group& group,
                                                                                          std::size_t m,
                                                                                          unsigned shift) const {
  Entries cache;
  std::size_t cached = kNone;
  const auto [lastBegin, lastLength] = spanAt(group, m - 1, cache, cached);
  const std::uint64_t end = lastBegin + lastLength;
  const std::uint64_t from = ((end - 1) >> shift) << shift;
  std::uint64_t inLast = 0;
  for (std::size_t i = m; i > 0; --i) {
    const auto [b, length] = spanAt(group, i - 1, cache, cached);
    if (b + length <= from) break;
    inLast += b + length - (std::max)(b, from);
  }
  return {end, inLast};
}

void AnnotationGroupsBuilder::BuildGroup::buildCoverage(Group& group, std::uint64_t end, unsigned shift,
                                                        std::size_t cap, MemoryCharge& charge) {
  if (!group.regular || group.n == 0 || coveragePyramidBytes(group.firstBegin, end, shift) > cap) return;
  CoveragePyramidBuilder builder(shift);
  forEachSpan(group, [&](std::uint64_t b, std::uint64_t length) { builder.add(b, length, charge); });
  group.coverage = builder.finished(charge);
  builder.clear(charge);
}

std::size_t AnnotationGroupsBuilder::groupFor(std::uint32_t row, std::uint32_t cls) {
  const std::uint64_t key = keyOf(row, cls);
  if (last_ < keys_.size() && keys_[last_] == key) return last_;
  if (keys_.size() <= kLinearKeys) {
    for (std::size_t k = 0; k < keys_.size(); ++k) {
      if (keys_[k] == key) return last_ = k;
    }
  } else if (const auto found = manyKeys_.find(key); found != manyKeys_.end()) {
    return last_ = found->second;
  }
  roomForOneMore(groups_, charge_);
  roomForOneMore(builds_, charge_);
  charge_.acquire(sizeof(BuildGroup));
  builds_.push_back(std::make_unique<BuildGroup>());
  if (coverageShift_ != 0) builds_.back()->coverage.emplace(coverageShift_);
  groups_.emplace_back();
  groups_.back().row = row;
  groups_.back().cls = cls;
  keys_.push_back(key);
  if (keys_.size() > kLinearKeys) {
    if (manyKeys_.empty()) {
      for (std::size_t k = 0; k < keys_.size(); ++k) manyKeys_.emplace(keys_[k], k);
    } else {
      manyKeys_.emplace(key, keys_.size() - 1);
    }
  }
  return last_ = keys_.size() - 1;
}

void MemoryCharge::acquire(std::size_t bytes) {
  if (memory == nullptr) return;
  if (!memory->acquire(bytes)) throw ChargeRefused{};
  charged += bytes;
}

void MemoryCharge::release(std::size_t bytes) {
  if (memory == nullptr) return;
  charged -= bytes;
  memory->release(bytes);
}

AnnotationGroupsBuilder::AnnotationGroupsBuilder() = default;

AnnotationGroupsBuilder::AnnotationGroupsBuilder(AnnotationMemory& memory) { charge_.memory = &memory; }

AnnotationGroupsBuilder::AnnotationGroupsBuilder(CoveragePolicy policy) : policy_(policy) {
  if (policy_.fixedShift != 0) {
    coverageShift_ = std::clamp(policy_.fixedShift, CoveragePyramid::kLeastShift, CoveragePyramid::kMostShift);
    coverageCap_ = kFixedCoverageCap;
  }
}

AnnotationGroupsBuilder::AnnotationGroupsBuilder(AnnotationMemory& memory, CoveragePolicy policy)
    : AnnotationGroupsBuilder(policy) {
  charge_.memory = &memory;
}

std::size_t AnnotationGroupsBuilder::coverageBudget(std::size_t storeBytes) const {
  return storeBytes / 100 * policy_.budgetPercent + storeBytes % 100 * policy_.budgetPercent / 100;
}

unsigned AnnotationGroupsBuilder::coverageShiftFor(std::span<const std::pair<std::uint64_t, std::uint64_t>> extents,
                                                   std::size_t storeBytes) const {
  if (policy_.fixedShift != 0) return coverageShift_;
  if (policy_.budgetPercent == 0 || extents.empty()) return 0;
  const std::size_t budget = coverageBudget(storeBytes);
  const auto bytesAt = [&](unsigned shift) {
    std::size_t bytes = 0;
    for (const auto& [first, end] : extents) bytes += coveragePyramidBytes(first, end, shift);
    return bytes;
  };
  unsigned best = CoveragePyramid::kLeastShift;
  while (best < CoveragePyramid::kMostShift && bytesAt(best) > budget) ++best;
  return bytesAt(best) > budget ? 0 : best;
}

void AnnotationGroupsBuilder::lookAfterAdding(const BuildGroup& build) {
  if (build.coverageOverCap || stored_ + 1 >= nextCoverageLook_) lookAtCoverage();
  while (nextCoverageLook_ <= stored_ + 1) nextCoverageLook_ *= 2;
}

void AnnotationGroupsBuilder::finishCoverage(AnnotationGroups& out, std::vector<std::unique_ptr<BuildGroup>>& builds) {
  std::vector<std::pair<std::uint64_t, std::uint64_t>> extents;
  for (std::size_t g = 0; g < out.groups_.size(); ++g) {
    const AnnotationGroups::Group& group = out.groups_[g];
    if (group.regular && group.n != 0) extents.emplace_back(group.firstBegin, builds[g]->maxEnd);
  }
  const std::size_t store = out.memoryBytes();
  const unsigned shift = coverageShiftFor(extents, store);
  const std::size_t cap = policy_.fixedShift != 0 ? kFixedCoverageCap : coverageBudget(store);
  for (std::size_t g = 0; g < out.groups_.size(); ++g) {
    AnnotationGroups::Group& group = out.groups_[g];
    BuildGroup& build = *builds[g];
    if (shift != 0 && group.regular && group.n != 0) {
      if (build.sorted && build.coverage && build.coverage->started() && build.coverage->shift() == shift) {
        group.coverage = build.coverage->finished(charge_);
      } else {
        BuildGroup::buildCoverage(group, build.maxEnd, shift, cap, charge_);
      }
    }
    build.stopCoverage(charge_);
  }
}

void AnnotationGroupsBuilder::lookAtCoverage() {
  std::vector<std::pair<std::uint64_t, std::uint64_t>> extents;
  std::size_t store = 0;
  for (std::size_t g = 0; g < builds_.size(); ++g) {
    store += builds_[g]->storedBytes() + vectorBytes(groups_[g].longLengths);
    if (builds_[g]->coverageEligible(groups_[g])) extents.emplace_back(builds_[g]->firstBegin, builds_[g]->maxEnd);
  }
  const unsigned shift = coverageShiftFor(extents, store);
  if (policy_.fixedShift == 0) coverageCap_ = (std::max)(2 * coverageBudget(store), kLeastCoverageCap);
  const bool changed = shift != coverageShift_;
  coverageShift_ = shift;
  for (std::size_t g = 0; g < builds_.size(); ++g) {
    BuildGroup& build = *builds_[g];
    build.coverageOverCap = false;
    const bool fits = shift != 0 && build.coverageEligible(groups_[g]) &&
                      coveragePyramidBytes(build.firstBegin, build.maxEnd, shift) <= coverageCap_;
    if (!fits) {
      build.stopCoverage(charge_);
    } else if (changed || !build.coverage) {
      build.startCoverage(groups_[g], shift, charge_);
    }
  }
}

AnnotationGroupsBuilder::~AnnotationGroupsBuilder() { drop(); }

std::size_t AnnotationGroupsBuilder::openBytes() const {
  std::size_t bytes = vectorBytes(builds_);
  for (std::size_t g = 0; g < builds_.size(); ++g) {
    const AnnotationGroups::Group& group = groups_[g];
    bytes += builds_[g]->openBytes() +
             (group.longLengths.capacity() - group.longLengths.size()) * sizeof(group.longLengths.front());
  }
  return bytes;
}

const std::atomic<std::size_t>& AnnotationGroupsBuilder::published(std::size_t g) const {
  return builds_[g]->published;
}

std::size_t AnnotationGroupsBuilder::closedBlocks(std::size_t g) const { return builds_[g]->firsts.size(); }

std::vector<std::span<const std::uint8_t>> AnnotationGroupsBuilder::closedBytes(std::size_t g) const {
  std::vector<std::span<const std::uint8_t>> out;
  builds_[g]->closedBytes(out);
  return out;
}

void AnnotationGroupsBuilder::drop() {
  groups_ = std::vector<AnnotationGroups::Group>();
  builds_ = std::vector<std::unique_ptr<BuildGroup>>();
  bytePools_.clear();
  keys_ = {};
  manyKeys_ = {};
  last_ = 0;
  if (charge_.charged != 0) charge_.release(charge_.charged);
}

std::expected<void, GroupsRefusal> AnnotationGroupsBuilder::refuse(GroupsRefusal::Reason reason) {
  drop();
  refused_ = GroupsRefusal{reason, stored_};
  return std::unexpected(*refused_);
}

std::expected<void, GroupsRefusal> AnnotationGroupsBuilder::add(const decode::Annotation& annotation) {
  return add(std::span<const decode::Annotation>(&annotation, 1));
}

std::expected<void, GroupsRefusal> AnnotationGroupsBuilder::add(std::span<const decode::Annotation> batch) {
  if (refused_) return std::unexpected(*refused_);
  try {
    for (const decode::Annotation& a : batch) {
      const std::size_t g = groupFor(a.row, a.cls);
      builds_[g]->append(groups_[g], a, charge_, coverageCap_);
      lookAfterAdding(*builds_[g]);
      ++stored_;
    }
  } catch (const ChargeRefused&) {
    return refuse(GroupsRefusal::Reason::OverBudget);
  } catch (const std::bad_alloc&) {
    return refuse(GroupsRefusal::Reason::OutOfMemory);
  }
  return {};
}

std::expected<void, GroupsRefusal> AnnotationGroupsBuilder::addBytes(const decode::Annotation& annotation,
                                                                    std::span<const std::uint8_t> bytes) {
  if (refused_) return std::unexpected(*refused_);
  try {
    const std::size_t g = groupFor(annotation.row, annotation.cls);
    const std::uint64_t key = keyOf(annotation.row, annotation.cls);
    auto found = std::lower_bound(bytePools_.begin(), bytePools_.end(), key,
                                  [](const AnnotationGroups::BytePool& pool, std::uint64_t wanted) {
                                    return pool.key < wanted;
                                  });
    if (found == bytePools_.end() || found->key != key) {
      const auto position = found - bytePools_.begin();
      roomForOneMore(bytePools_, charge_);
      found = bytePools_.insert(bytePools_.begin() + position, AnnotationGroups::BytePool{key, {}});
    }
    std::vector<std::uint8_t>& pool = found->bytes;
    const std::size_t at = pool.size();
    const std::size_t entry = kBytesLengthPrefix + bytes.size();
    if (pool.capacity() - at < entry) {
      const std::size_t next = (std::max)(pool.capacity() * 2, at + entry);
      chargedVector(pool, charge_, next, [&] { pool.reserve(next); });
    }
    pool.resize(at + entry);
    const auto length = static_cast<std::uint32_t>(bytes.size());
    std::memcpy(pool.data() + at, &length, kBytesLengthPrefix);
    if (!bytes.empty()) std::memcpy(pool.data() + at + kBytesLengthPrefix, bytes.data(), bytes.size());
    decode::Annotation stored = annotation;
    stored.value = at;
    builds_[g]->append(groups_[g], stored, charge_, coverageCap_);
    lookAfterAdding(*builds_[g]);
    ++stored_;
  } catch (const ChargeRefused&) {
    return refuse(GroupsRefusal::Reason::OverBudget);
  } catch (const std::bad_alloc&) {
    return refuse(GroupsRefusal::Reason::OutOfMemory);
  }
  return {};
}

std::expected<AnnotationGroups, GroupsRefusal> AnnotationGroupsBuilder::finish() {
  if (refused_) return std::unexpected(*refused_);
  using Group = AnnotationGroups::Group;
  AnnotationGroups out;
  try {
    std::vector<Group> groups = std::move(groups_);
    std::vector<std::unique_ptr<BuildGroup>> builds = std::move(builds_);
    groups_ = std::vector<Group>();
    builds_ = std::vector<std::unique_ptr<BuildGroup>>();
    keys_ = {};
    manyKeys_ = {};
    last_ = 0;
    for (std::size_t g = 0; g < groups.size(); ++g) {
      Group& group = groups[g];
      BuildGroup& build = *builds[g];
      build.flatten(group, charge_);
      if (!build.sorted) BuildGroup::sortGroup(group, charge_);
      shrinkVector(group.longLengths, charge_);
      if (!group.regular) BuildGroup::buildEnds(group, charge_);
    }
    const std::size_t buildsBytes = vectorBytes(builds) + builds.size() * sizeof(BuildGroup);
    // WHY: complete() sorts the groups by key: order the builds the same way so each still meets its group.
    std::vector<std::size_t> order(groups.size());
    std::iota(order.begin(), order.end(), std::size_t{0});
    std::ranges::sort(order, {}, [&](std::size_t g) { return keyOf(groups[g].row, groups[g].cls); });
    std::vector<std::unique_ptr<BuildGroup>> byKey;
    byKey.reserve(builds.size());
    for (const std::size_t g : order) byKey.push_back(std::move(builds[g]));
    builds = std::move(byKey);
    complete(out, std::move(groups), charge_);
    out.bytePools_ = std::move(bytePools_);
    bytePools_.clear();
    shrinkVector(out.bytePools_, charge_);
    finishCoverage(out, builds);
    builds = std::vector<std::unique_ptr<BuildGroup>>();
    charge_.release(buildsBytes);
  } catch (const ChargeRefused&) {
    out = {};
    return std::unexpected(refuse(GroupsRefusal::Reason::OverBudget).error());
  } catch (const std::bad_alloc&) {
    out = {};
    return std::unexpected(refuse(GroupsRefusal::Reason::OutOfMemory).error());
  }
  charge_.charged = 0;
  stored_ = 0;
  return out;
}

void AnnotationGroupsBuilder::complete(AnnotationGroups& out, std::vector<AnnotationGroups::Group> groups,
                                       MemoryCharge& charge) {
  using Group = AnnotationGroups::Group;
  std::sort(groups.begin(), groups.end(),
            [](const Group& a, const Group& b) { return keyOf(a.row, a.cls) < keyOf(b.row, b.cls); });
  out.groups_ = std::move(groups);
  const std::size_t count = out.groups_.size();
  const std::size_t rowBytes = count * (sizeof(std::uint32_t) + sizeof(RowClass));
  charge.acquire(rowBytes);
  out.rowIds_.reserve(count);
  out.rowClassPairs_.reserve(count);
  charge.release(rowBytes - vectorBytes(out.rowIds_) - vectorBytes(out.rowClassPairs_));
  for (std::size_t g = 0; g < out.groups_.size(); ++g) {
    const Group& group = out.groups_[g];
    out.size_ += group.n;
    out.usable_ = out.usable_ && group.positive;
    out.coverageUsable_ = out.coverageUsable_ && group.regular;
    if (out.rowIds_.empty() || out.rowIds_.back() != group.row) out.rowIds_.push_back(group.row);
    out.rowClassPairs_.push_back({group.row, group.cls});
  }
}

std::expected<AnnotationGroups, GroupsRefusal> AnnotationGroupsBuilder::snapshot(std::uint64_t p) const {
  if (refused_) return std::unexpected(*refused_);
  using Group = AnnotationGroups::Group;
  MemoryCharge charge{charge_.memory, 0};
  AnnotationGroups out;
  try {
    std::size_t present = 0;
    for (std::size_t g = 0; g < builds_.size(); ++g) present += builds_[g]->countBelow(groups_[g], p) != 0 ? 1 : 0;
    const std::size_t records = present == 0 ? 0 : std::bit_ceil(present);
    std::vector<Group> groups;
    chargedVector(groups, charge, records * sizeof(Group), [&] { groups.reserve(records); });
    for (std::size_t g = 0; g < builds_.size(); ++g) {
      const BuildGroup& build = *builds_[g];
      const std::size_t below = build.countBelow(groups_[g], p);
      if (below == 0) continue;
      groups.push_back(build.sorted ? build.prefix(groups_[g], below, charge)
                                    : build.sortedBelow(groups_[g], p, below, coverageShift_, coverageCap_, charge));
    }
    complete(out, std::move(groups), charge);
    chargedVector(out.bytePools_, charge, bytePools_.size() * sizeof(AnnotationGroups::BytePool),
                  [&] { out.bytePools_.reserve(bytePools_.size()); });
    for (const Group& group : out.groups_) {
      const std::uint64_t key = keyOf(group.row, group.cls);
      const auto pool = std::lower_bound(bytePools_.begin(), bytePools_.end(), key,
                                         [](const AnnotationGroups::BytePool& p, std::uint64_t wanted) {
                                           return p.key < wanted;
                                         });
      if (pool == bytePools_.end() || pool->key != key) continue;
      charge.acquire(pool->bytes.size());
      out.bytePools_.push_back(*pool);
    }
    shrinkVector(out.bytePools_, charge);
  } catch (const ChargeRefused&) {
    out = {};
    charge.release(charge.charged);
    return std::unexpected(GroupsRefusal{GroupsRefusal::Reason::OverBudget, stored_});
  } catch (const std::bad_alloc&) {
    out = {};
    charge.release(charge.charged);
    return std::unexpected(GroupsRefusal{GroupsRefusal::Reason::OutOfMemory, stored_});
  }
  return out;
}

AnnotationGroups groupAnnotations(std::span<const decode::Annotation> annotations, CoveragePolicy policy) {
  AnnotationGroupsBuilder builder(policy);
  if (!builder.add(annotations)) throw std::bad_alloc();
  auto groups = builder.finish();
  if (!groups) throw std::bad_alloc();
  return std::move(*groups);
}

}

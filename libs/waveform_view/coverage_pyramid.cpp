/*
 * SPDX-License-Identifier: MIT
 */

#include "coverage_pyramid.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

namespace bitoscope::waveform_view {

namespace {

constexpr std::uint32_t kEmptyCode = 0;
constexpr std::uint32_t kFullCode = 15;
constexpr std::uint64_t kPartialCodes = 14;
constexpr std::uint64_t kCoarseOnlyBuckets = 4 * CoveragePyramid::kFan;
constexpr double kModulus = 4294967296.0;

std::uint32_t codeOf(std::uint64_t covered, unsigned shift) {
  if (covered == 0) return kEmptyCode;
  if (covered >= (std::uint64_t{1} << shift)) return kFullCode;
  return 1 + static_cast<std::uint32_t>((kPartialCodes * covered) >> shift);
}

std::uint64_t bucketsBetween(std::uint64_t firstBegin, std::uint64_t lastEnd, unsigned shift) {
  if (lastEnd <= firstBegin) return 0;
  return ((lastEnd - 1) >> shift) - (firstBegin >> shift) + 1;
}

std::size_t codeBytes(std::uint64_t buckets) { return static_cast<std::size_t>((buckets + 1) / 2); }

std::size_t coarseEntries(std::uint64_t buckets) {
  return buckets == 0 ? 0 : static_cast<std::size_t>((buckets - 1) / CoveragePyramid::kFan + 1);
}

std::size_t pyramidBytes(std::uint64_t buckets) {
  return codeBytes(buckets) * sizeof(std::uint8_t) + coarseEntries(buckets) * sizeof(std::uint32_t);
}

template <class T>
void roomFor(std::vector<T>& v, std::size_t wanted, MemoryCharge& charge) {
  if (wanted <= v.capacity()) return;
  const std::size_t next = (std::max)(wanted, v.capacity() + (std::max)(v.capacity() / 8, std::size_t{64}));
  const std::size_t before = v.capacity() * sizeof(T);
  charge.acquire(next * sizeof(T));
  v.reserve(next);
  charge.release(next * sizeof(T) + before - v.capacity() * sizeof(T));
}

template <class T>
std::vector<T> exactCopy(const std::vector<T>& from, std::size_t count, MemoryCharge& charge) {
  std::vector<T> out;
  if (count == 0) return out;
  charge.acquire(count * sizeof(T));
  out.reserve(count);
  charge.release(count * sizeof(T) - out.capacity() * sizeof(T));
  const std::size_t copied = (std::min)(count, from.size());
  out.assign(from.begin(), from.begin() + static_cast<std::ptrdiff_t>(copied));
  out.resize(count, T{0});
  return out;
}

void putLastCode(std::vector<std::uint8_t>& codes, std::uint64_t last, std::uint32_t code) {
  std::uint8_t& two = codes[static_cast<std::size_t>(last / 2)];
  two = (last & 1) == 0 ? static_cast<std::uint8_t>(code) : static_cast<std::uint8_t>((two & 0xFu) | (code << 4));
}

std::uint64_t wholeSamples(double sample) {
  constexpr double kPastLast = 18446744073709551616.0;
  return sample >= kPastLast ? (std::numeric_limits<std::uint64_t>::max)() : static_cast<std::uint64_t>(sample);
}

std::uint64_t floorBucket(double sample, unsigned shift) { return wholeSamples(std::floor(sample)) >> shift; }

std::uint64_t ceilBucket(double sample, unsigned shift) {
  const std::uint64_t whole = wholeSamples(std::ceil(sample));
  return (whole >> shift) + ((whole & ((std::uint64_t{1} << shift) - 1)) != 0 ? 1 : 0);
}

}

std::size_t coveragePyramidBytes(std::uint64_t firstBegin, std::uint64_t lastEnd, unsigned shift) {
  return pyramidBytes(bucketsBetween(firstBegin, lastEnd, shift));
}

std::uint64_t CoveragePyramid::coarseAt(std::uint64_t c) const {
  return c < coarse_.size() ? coarse_[static_cast<std::size_t>(c)] : total_;
}

std::uint64_t CoveragePyramid::coarseRange(std::uint64_t from, std::uint64_t to) const {
  return static_cast<std::uint32_t>(static_cast<std::uint32_t>(coarseAt(to)) - static_cast<std::uint32_t>(coarseAt(from)));
}

std::uint32_t CoveragePyramid::code(std::uint64_t b) const {
  const std::uint8_t two = codes_[static_cast<std::size_t>(b >> 1)];
  return (b & 1) != 0 ? static_cast<std::uint32_t>(two >> 4) : static_cast<std::uint32_t>(two & 0xFu);
}

struct CoveragePyramid::CodeTable {
  std::array<std::uint64_t, kFullCode + 1> single{};
  std::array<std::uint64_t, 256> pair{};
};

struct CoveragePyramid::CodeBounds {
  CodeTable lower;
  CodeTable upper;
};

CoveragePyramid::CodeBounds CoveragePyramid::codeBounds() const {
  const std::uint64_t bucket = bucketSamples();
  CodeBounds out;
  out.lower.single[kFullCode] = bucket;
  out.upper.single[kFullCode] = bucket;
  for (std::uint64_t c = 1; c < kFullCode; ++c) {
    out.lower.single[c] = (std::max)(std::uint64_t{1}, ((c - 1) * bucket + kPartialCodes - 1) / kPartialCodes);
    out.upper.single[c] = (std::min)(bucket - 1, (c * bucket + kPartialCodes - 1) / kPartialCodes - 1);
  }
  for (CodeTable* table : {&out.lower, &out.upper}) {
    for (std::size_t two = 0; two < table->pair.size(); ++two) {
      table->pair[two] = table->single[two & 0xFu] + table->single[two >> 4];
    }
  }
  return out;
}

std::uint64_t CoveragePyramid::codeSum(std::uint64_t from, std::uint64_t to, const CodeTable& table) const {
  if (to <= from) return 0;
  std::uint64_t out = 0;
  if ((from & 1) != 0) out += table.single[code(from++)];
  for (const std::uint64_t pairsEnd = to & ~std::uint64_t{1}; from < pairsEnd; from += 2) {
    out += table.pair[codes_[static_cast<std::size_t>(from >> 1)]];
  }
  if (from < to) out += table.single[code(from)];
  return out;
}

std::uint64_t CoveragePyramid::sum(std::uint64_t from, std::uint64_t to, const CodeTable& table, bool lower) const {
  if (to <= from) return 0;
  if (to - from >= kCoarseOnlyBuckets) {
    const std::uint64_t end = to >= buckets_ ? coarseEntries(buckets_) : to / kFan;
    return lower ? coarseRange((from + kFan - 1) / kFan, end) : coarseRange(from / kFan, (to + kFan - 1) / kFan);
  }
  const std::uint64_t c0 = (from + kFan - 1) / kFan;
  const std::uint64_t c1 = to / kFan;
  if (c1 <= c0) return codeSum(from, to, table);
  return coarseRange(c0, c1) + codeSum(from, c0 * kFan, table) + codeSum(c1 * kFan, to, table);
}

CoverageBounds CoveragePyramid::around(double from, double to) const { return around(from, to, codeBounds()); }

struct CoveragePyramid::Edge {
  std::uint64_t firstInside = 0;
  std::uint64_t lastBefore = 0;
  std::uint64_t pastAfter = 0;
};

CoveragePyramid::Edge CoveragePyramid::edgeAt(double sample) const {
  const auto relative = [this](double at, std::uint64_t (*bucketOf)(double, unsigned), std::uint64_t after) {
    if (at < 0.0) return std::uint64_t{0};
    const std::uint64_t absolute = bucketOf(at, shift_);
    if (absolute < origin_) return std::uint64_t{0};
    return (std::min)((std::min)(absolute - origin_, buckets_) + after, buckets_);
  };
  return {relative(sample + 1.0, ceilBucket, 0), relative(sample - 1.0, floorBucket, 0),
          relative(sample + 1.0, floorBucket, 1)};
}

std::uint64_t CoveragePyramid::coveredInside(std::uint64_t bucket, std::uint64_t inside, const CodeTable& lower) const {
  if (bucket < origin_ || bucket - origin_ >= buckets_) return 0;
  const std::uint64_t outside = bucketSamples() - inside;
  const std::uint64_t covered = lower.single[code(bucket - origin_)];
  return covered > outside ? covered - outside : 0;
}

std::uint64_t CoveragePyramid::edgeLower(double from, double to, const CodeTable& lower) const {
  const std::uint64_t first = from + 1.0 < 0.0 ? 0 : wholeSamples(std::ceil(from + 1.0));
  const std::uint64_t end = to - 1.0 < 0.0 ? 0 : wholeSamples(std::floor(to - 1.0));
  if (end <= first) return 0;
  const std::uint64_t left = first >> shift_;
  const std::uint64_t right = (end - 1) >> shift_;
  if (left == right) return coveredInside(left, end - first, lower);
  return coveredInside(left, bucketSamples() - (first & (bucketSamples() - 1)), lower) +
         coveredInside(right, end - (right << shift_), lower);
}

CoverageBounds CoveragePyramid::between(const Edge& from, const Edge& to, double fromSample, double toSample,
                                        const CodeBounds& codes) const {
  const double outer = toSample - fromSample + 2.0 * static_cast<double>(kFan + 1) * static_cast<double>(bucketSamples());
  if (!(outer < kModulus)) return {0, CoverageBounds::kUnknown};
  const std::uint64_t inner = sum(from.firstInside, to.lastBefore, codes.lower, true);
  return {inner != 0 ? inner : edgeLower(fromSample, toSample, codes.lower),
          sum(from.lastBefore, to.pastAfter, codes.upper, false)};
}

void CoveragePyramid::columnBounds(double firstVisibleSample, double samplesPerPixel,
                                   std::span<CoverageBounds> columns) const {
  if (empty()) {
    std::ranges::fill(columns, CoverageBounds{});
    return;
  }
  const CodeBounds codes = codeBounds();
  double from = firstVisibleSample;
  Edge left = edgeAt(from);
  for (std::size_t c = 0; c < columns.size(); ++c) {
    const double to = firstVisibleSample + (static_cast<double>(c) + 1.0) * samplesPerPixel;
    const Edge right = edgeAt(to);
    columns[c] = between(left, right, from, to, codes);
    from = to;
    left = right;
  }
}

CoverageBounds CoveragePyramid::around(double from, double to, const CodeBounds& codes) const {
  if (empty()) return {};
  return between(edgeAt(from), edgeAt(to), from, to, codes);
}

std::size_t CoveragePyramid::bytes() const {
  return codes_.capacity() * sizeof(std::uint8_t) + coarse_.capacity() * sizeof(std::uint32_t);
}

bool CoveragePyramid::sameCoverage(const CoveragePyramid& other) const {
  if (shift_ != other.shift_ || origin_ != other.origin_ || buckets_ != other.buckets_ || total_ != other.total_) {
    return false;
  }
  for (std::uint64_t b = 0; b < buckets_; ++b) {
    if (code(b) != other.code(b)) return false;
  }
  for (std::uint64_t c = 0; c < coarseEntries(buckets_); ++c) {
    if (coarseAt(c) != other.coarseAt(c)) return false;
  }
  return true;
}

CoveragePyramidBuilder::CoveragePyramidBuilder(unsigned shift) : shift_(shift) {}

void CoveragePyramidBuilder::putCode(std::uint32_t code, MemoryCharge& charge) {
  const std::uint64_t b = current_;
  if ((b & 1) == 0) {
    roomFor(codes_, codes_.size() + 1, charge);
    codes_.push_back(static_cast<std::uint8_t>(code));
  } else {
    codes_.back() = static_cast<std::uint8_t>(codes_.back() | (code << 4));
  }
}

void CoveragePyramidBuilder::emit(MemoryCharge& charge) {
  putCode(codeOf(accumulated_, shift_), charge);
  before_ += accumulated_;
  accumulated_ = 0;
  ++current_;
  if (current_ % CoveragePyramid::kFan == 0) {
    roomFor(coarse_, coarse_.size() + 1, charge);
    coarse_.push_back(static_cast<std::uint32_t>(before_));
  }
}

bool CoveragePyramidBuilder::add(std::uint64_t begin, std::uint64_t length, MemoryCharge& charge, std::size_t cap) {
  if (length == 0) return true;
  const std::uint64_t end = begin + length;
  const std::uint64_t firstBucket = begin >> shift_;
  const std::uint64_t lastBucket = (end - 1) >> shift_;
  if (started_ && firstBucket == origin_ + current_ && lastBucket == firstBucket) {
    accumulated_ += length;
    longest_ = (std::max)(longest_, length);
    lastEnd_ = end;
    return true;
  }
  if (pyramidBytes(lastBucket - (started_ ? origin_ : firstBucket) + 1) > cap) return false;
  if (!started_) {
    started_ = true;
    origin_ = firstBucket;
    firstBegin_ = begin;
    roomFor(coarse_, 1, charge);
    coarse_.push_back(0);
  }
  longest_ = (std::max)(longest_, length);
  lastEnd_ = end;
  const std::uint64_t first = firstBucket - origin_;
  while (current_ < first) emit(charge);
  for (;;) {
    const std::uint64_t bucketBegin = (origin_ + current_) << shift_;
    // WHY: The bucket's last sample, not its end: the end of the top bucket is 2^64 and wraps to 0.
    const std::uint64_t bucketLast = bucketBegin + ((std::uint64_t{1} << shift_) - 1);
    const std::uint64_t from = (std::max)(begin, bucketBegin);
    if (end - 1 <= bucketLast) {
      accumulated_ += end - from;
      return true;
    }
    accumulated_ += bucketLast - from + 1;
    emit(charge);
  }
}

CoveragePyramid CoveragePyramidBuilder::finished(MemoryCharge& charge) const {
  if (!started_) return {};
  CoveragePyramid out;
  out.shift_ = shift_;
  out.origin_ = origin_;
  out.buckets_ = current_ + 1;
  out.longest_ = longest_;
  out.total_ = static_cast<std::uint32_t>(before_ + accumulated_);
  out.codes_ = exactCopy(codes_, codeBytes(out.buckets_), charge);
  putLastCode(out.codes_, current_, codeOf(accumulated_, shift_));
  out.coarse_ = exactCopy(coarse_, coarseEntries(out.buckets_), charge);
  return out;
}

CoveragePyramid CoveragePyramidBuilder::finishedBefore(std::uint64_t end, std::uint64_t coveredInLastBucket,
                                                       std::uint64_t total, MemoryCharge& charge) const {
  if (!started_ || end <= firstBegin_) return {};
  CoveragePyramid out;
  out.shift_ = shift_;
  out.origin_ = origin_;
  const std::uint64_t last = ((end - 1) >> shift_) - origin_;
  out.buckets_ = last + 1;
  out.longest_ = longest_;
  out.total_ = static_cast<std::uint32_t>(total);
  out.codes_ = exactCopy(codes_, codeBytes(out.buckets_), charge);
  putLastCode(out.codes_, last, codeOf(coveredInLastBucket, shift_));
  out.coarse_ = exactCopy(coarse_, coarseEntries(out.buckets_), charge);
  return out;
}

std::size_t CoveragePyramidBuilder::bytes() const {
  return codes_.capacity() * sizeof(std::uint8_t) + coarse_.capacity() * sizeof(std::uint32_t);
}

std::size_t CoveragePyramidBuilder::unusedBytes() const {
  return (codes_.capacity() - codes_.size()) * sizeof(std::uint8_t) +
         (coarse_.capacity() - coarse_.size()) * sizeof(std::uint32_t);
}

void CoveragePyramidBuilder::clear(MemoryCharge& charge) {
  const std::size_t held = bytes();
  codes_ = std::vector<std::uint8_t>();
  coarse_ = std::vector<std::uint32_t>();
  charge.release(held);
  started_ = false;
  origin_ = 0;
  current_ = 0;
  accumulated_ = 0;
  before_ = 0;
  longest_ = 0;
  firstBegin_ = 0;
  lastEnd_ = 0;
}

}

/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <vector>

#include "annotation_memory.h"

namespace bitoscope::waveform_view {

struct CoverageBounds {
  static constexpr std::uint64_t kUnknown = std::numeric_limits<std::uint64_t>::max();
  std::uint64_t lower = 0;
  std::uint64_t upper = 0;
  bool operator==(const CoverageBounds&) const = default;
};

std::size_t coveragePyramidBytes(std::uint64_t firstBegin, std::uint64_t lastEnd, unsigned shift);

class CoveragePyramid {
 public:
  static constexpr std::size_t kFan = 16;
  static constexpr unsigned kLeastShift = 4;
  static constexpr unsigned kMostShift = 40;

  bool empty() const { return buckets_ == 0; }
  unsigned shift() const { return shift_; }
  std::uint64_t bucketSamples() const { return std::uint64_t{1} << shift_; }
  std::uint64_t longest() const { return longest_; }
  CoverageBounds around(double from, double to) const;
  void columnBounds(double firstVisibleSample, double samplesPerPixel, std::span<CoverageBounds> columns) const;
  std::size_t bytes() const;
  bool sameCoverage(const CoveragePyramid& other) const;

 private:
  friend class CoveragePyramidBuilder;

  std::uint64_t coarseAt(std::uint64_t c) const;
  std::uint64_t coarseRange(std::uint64_t from, std::uint64_t to) const;
  struct CodeTable;
  struct CodeBounds;

  std::uint32_t code(std::uint64_t b) const;
  CodeBounds codeBounds() const;
  std::uint64_t codeSum(std::uint64_t from, std::uint64_t to, const CodeTable& table) const;
  std::uint64_t sum(std::uint64_t from, std::uint64_t to, const CodeTable& table, bool lower) const;
  struct Edge;
  Edge edgeAt(double sample) const;
  std::uint64_t coveredInside(std::uint64_t bucket, std::uint64_t inside, const CodeTable& lower) const;
  std::uint64_t edgeLower(double from, double to, const CodeTable& lower) const;
  CoverageBounds between(const Edge& from, const Edge& to, double fromSample, double toSample,
                         const CodeBounds& codes) const;
  CoverageBounds around(double from, double to, const CodeBounds& codes) const;

  unsigned shift_ = kLeastShift;
  std::uint64_t origin_ = 0;
  std::uint64_t buckets_ = 0;
  std::uint64_t longest_ = 0;
  std::uint32_t total_ = 0;
  std::vector<std::uint8_t> codes_;
  std::vector<std::uint32_t> coarse_;
};

class CoveragePyramidBuilder {
 public:
  static constexpr std::size_t kNoCap = std::numeric_limits<std::size_t>::max();

  explicit CoveragePyramidBuilder(unsigned shift);

  unsigned shift() const { return shift_; }
  bool started() const { return started_; }
  bool add(std::uint64_t begin, std::uint64_t length, MemoryCharge& charge, std::size_t cap = kNoCap);
  CoveragePyramid finished(MemoryCharge& charge) const;
  CoveragePyramid finishedBefore(std::uint64_t end, std::uint64_t coveredInLastBucket, std::uint64_t total,
                                 MemoryCharge& charge) const;
  std::size_t bytes() const;
  std::size_t unusedBytes() const;
  void clear(MemoryCharge& charge);

 private:
  void emit(MemoryCharge& charge);
  void putCode(std::uint32_t code, MemoryCharge& charge);

  unsigned shift_;
  bool started_ = false;
  std::uint64_t origin_ = 0;
  std::uint64_t current_ = 0;
  std::uint64_t accumulated_ = 0;
  std::uint64_t before_ = 0;
  std::uint64_t longest_ = 0;
  std::uint64_t firstBegin_ = 0;
  std::uint64_t lastEnd_ = 0;
  std::vector<std::uint8_t> codes_;
  std::vector<std::uint32_t> coarse_;
};

}

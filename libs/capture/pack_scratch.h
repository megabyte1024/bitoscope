/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <cstdint>
#include <expected>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace bitoscope::capture {

class RawCaptureBuffer;

enum class SegmentCodec : uint8_t { None = 0, Lz4 = 1 };

struct PackedSegment {
  SegmentCodec codec = SegmentCodec::None;
  uint32_t payload_crc32c = 0;
  std::span<const uint8_t> bytes;
};

// WHY: One PackScratch per packing thread: two threads must never share one.
class PackScratch {
 public:
  PackScratch() = default;

  static std::expected<PackScratch, std::string> create(uint32_t channels, uint32_t samples_per_window,
                                                        bool pyramid = false);

  uint32_t channels() const { return channels_; }
  uint64_t bucketBytes() const { return bucket_bytes_; }
  uint64_t heldBytes() const { return packed_bytes_ + segments_.capacity() * sizeof(PackedSegment) + bucket_bytes_; }

 private:
  friend class RawCaptureBuffer;

  uint32_t channels_ = 0;
  uint64_t region_bytes_ = 0;
  std::unique_ptr<uint8_t[]> packed_;
  uint64_t packed_bytes_ = 0;
  std::vector<PackedSegment> segments_;
  std::unique_ptr<uint8_t[]> buckets_;
  uint64_t bucket_bytes_ = 0;

  std::span<uint8_t> buckets() const { return {buckets_.get(), static_cast<size_t>(bucket_bytes_)}; }
};

}

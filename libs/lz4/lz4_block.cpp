/*
 * SPDX-License-Identifier: MIT
 */

#include "lz4_block.h"

#include <limits>

#include <lz4.h>

namespace bitoscope::host {

namespace {

constexpr size_t kMaxLz4Size = static_cast<size_t>(std::numeric_limits<int>::max());

}

std::expected<size_t, std::string> lz4CompressBound(size_t input_size) {
  if (input_size > kMaxLz4Size) {
    return std::unexpected("cannot bound LZ4 compression of " + std::to_string(input_size) +
                           " bytes: LZ4 takes sizes up to " + std::to_string(kMaxLz4Size));
  }
  // WHY: LZ4_compressBound returns 0, not an error code, for oversized input:
  // zero is a refusal, never a valid bound.
  const int bound = LZ4_compressBound(static_cast<int>(input_size));
  if (bound <= 0) {
    return std::unexpected("LZ4 cannot compress " + std::to_string(input_size) +
                           " bytes in one block: it is above LZ4's maximum input size");
  }
  return static_cast<size_t>(bound);
}

std::expected<size_t, std::string> lz4Compress(std::span<const uint8_t> src,
                                                std::span<uint8_t> dst) {
  if (src.size() > kMaxLz4Size || dst.size() > kMaxLz4Size) {
    return std::unexpected("cannot compress with a " + std::to_string(src.size()) +
                           "-byte source and a " + std::to_string(dst.size()) +
                           "-byte destination: LZ4 takes sizes up to " +
                           std::to_string(kMaxLz4Size));
  }
  const int written = LZ4_compress_default(reinterpret_cast<const char*>(src.data()),
                                           reinterpret_cast<char*>(dst.data()),
                                           static_cast<int>(src.size()),
                                           static_cast<int>(dst.size()));
  if (written <= 0) {
    return std::unexpected("LZ4 could not compress " + std::to_string(src.size()) +
                           " bytes into a " + std::to_string(dst.size()) +
                           "-byte destination: it is too small (lz4CompressBound gives a "
                           "size that always fits), or the input exceeds LZ4's maximum");
  }
  return static_cast<size_t>(written);
}

std::expected<void, std::string> lz4Decompress(std::span<const uint8_t> src,
                                                std::span<uint8_t> dst) {
  if (src.size() > kMaxLz4Size || dst.size() > kMaxLz4Size) {
    return std::unexpected("cannot decompress a " + std::to_string(src.size()) +
                           "-byte block into " + std::to_string(dst.size()) +
                           " bytes: LZ4 takes sizes up to " + std::to_string(kMaxLz4Size));
  }
  const int decoded = LZ4_decompress_safe(reinterpret_cast<const char*>(src.data()),
                                          reinterpret_cast<char*>(dst.data()),
                                          static_cast<int>(src.size()),
                                          static_cast<int>(dst.size()));
  if (decoded < 0) {
    return std::unexpected("LZ4 block of " + std::to_string(src.size()) +
                           " bytes is malformed or truncated, or does not fit in the " +
                           std::to_string(dst.size()) + "-byte destination");
  }
  // WHY: A block decoding to another length than dst is an error, not a valid shorter segment:
  // it contradicts its index entry.
  if (static_cast<size_t>(decoded) != dst.size()) {
    return std::unexpected("LZ4 block decoded to " + std::to_string(decoded) +
                           " bytes but the segment is " + std::to_string(dst.size()) +
                           " bytes: the block does not match the recorded size");
  }
  return {};
}

}

/*
 * SPDX-License-Identifier: MIT
 */

#include "pack_scratch.h"

#include <format>
#include <limits>
#include <memory>
#include <new>
#include <stdexcept>

#include "raw_capture_buffer.h"

namespace bitoscope::capture {

std::expected<PackScratch, std::string> PackScratch::create(uint32_t channels,
                                                            uint32_t samples_per_window, bool pyramid) {
  if (channels == 0) return std::unexpected("pack scratch needs at least one channel");
  auto total = captureScratchBytes(channels, samples_per_window);
  if (!total) return std::unexpected(total.error());
  if (*total > (std::numeric_limits<size_t>::max)()) {
    return std::unexpected("capture's compression scratch does not fit this platform");
  }
  PackScratch scratch;
  scratch.channels_ = channels;
  scratch.region_bytes_ = *total / channels;
  try {
    scratch.packed_ = std::make_unique_for_overwrite<uint8_t[]>(static_cast<size_t>(*total));
    scratch.packed_bytes_ = *total;
    scratch.segments_.resize(channels);
    if (pyramid) {
      scratch.bucket_bytes_ = windowBucketBytes(channels, samples_per_window);
      scratch.buckets_ = std::make_unique_for_overwrite<uint8_t[]>(static_cast<size_t>(scratch.bucket_bytes_));
    }
  } catch (const std::bad_alloc&) {
    return std::unexpected(std::format(
        "out of memory allocating {} bytes of compression scratch for {} channels", *total,
        channels));
  } catch (const std::length_error&) {
    return std::unexpected("capture's compression scratch does not fit this platform");
  }
  return scratch;
}

}

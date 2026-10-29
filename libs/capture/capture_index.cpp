/*
 * SPDX-License-Identifier: MIT
 */

#include <algorithm>
#include <limits>

#include "lz4_block.h"
#include "raw_capture_buffer.h"

namespace bitoscope::capture {

uint64_t captureIndexWindows(const CapturePlan& plan, uint64_t arena_bytes) {
  const uint64_t arena_bound = plan.channels() == 0 ? 0 : arena_bytes / plan.channels();
  return (std::min)(plan.slots(), (std::max)(arena_bound, uint64_t{1}));
}

uint64_t captureIndexBytes(uint64_t windows, uint32_t channels) {
  constexpr uint64_t kMax = (std::numeric_limits<uint64_t>::max)();
  const uint64_t per_window = sizeof(IndexRecord) + uint64_t{channels} * sizeof(ChannelSegment);
  return windows != 0 && per_window > kMax / windows ? kMax : windows * per_window;
}

std::expected<uint64_t, std::string> captureScratchBytes(uint32_t channels,
                                                         uint32_t samples_per_window) {
  const uint64_t max_channel_bytes = (static_cast<uint64_t>(samples_per_window) + 7) / 8;
  if (max_channel_bytes > (std::numeric_limits<size_t>::max)()) {
    return std::unexpected("capture's compression scratch does not fit this platform");
  }
  auto bound = bitoscope::host::lz4CompressBound(static_cast<size_t>(max_channel_bytes));
  if (!bound) return std::unexpected(bound.error());
  const uint64_t per_channel = *bound;
  if (channels != 0 && per_channel > (std::numeric_limits<uint64_t>::max)() / channels) {
    return std::unexpected("capture's compression scratch does not fit this platform");
  }
  return per_channel * channels;
}

std::expected<uint64_t, std::string> captureHeldBackBytes(const CapturePlan& plan,
                                                          uint32_t pack_workers, bool pyramid) {
  constexpr uint64_t kMax = (std::numeric_limits<uint64_t>::max)();
  const uint32_t channels = plan.channels();
  const uint32_t samples_per_window = plan.samplesPerWindow();
  auto scratch = captureScratchBytes(channels, samples_per_window);
  if (!scratch) return std::unexpected(scratch.error());
  const uint64_t pool =
      windowPoolBytes(windowPoolBuffers(pack_workers), channels, samples_per_window);
  const uint64_t scratches = 1 + packPoolDepth(pack_workers);
  const uint64_t scratch_bytes = *scratch > kMax / scratches ? kMax : *scratch * scratches;
  const uint64_t index = captureIndexBytes(captureIndexWindows(plan, kMax), channels);
  const uint64_t outside = pool > kMax - scratch_bytes ? kMax : pool + scratch_bytes;
  const uint64_t held = outside > kMax - index ? kMax : outside + index;
  if (!pyramid) return held;
  const uint64_t buckets = windowBucketBytes(channels, samples_per_window) * scratches;
  const uint64_t pass = pyramidHistoryScratchBytes(channels, samples_per_window);
  const uint64_t extra = buckets > kMax - pass ? kMax : buckets + pass;
  return held > kMax - extra ? kMax : held + extra;
}

}

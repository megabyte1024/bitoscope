/*
 * SPDX-License-Identifier: MIT
 */

#include "read_samples.h"

#include <format>
#include <limits>

namespace bitoscope::capture {

std::expected<void, std::string> readSamples(const RawCaptureBuffer& buffer, uint32_t channel,
                                             uint64_t first_sample, uint64_t count,
                                             std::span<uint8_t> out) {
  if (count > (std::numeric_limits<uint64_t>::max)() - 7) {
    return std::unexpected(std::format("sample count {} is too large to read", count));
  }
  const uint64_t expected_bytes = (count + 7) / 8;
  if (out.size() != expected_bytes) {
    return std::unexpected(std::format(
        "readSamples needs {} output bytes for {} samples, got {}", expected_bytes, count,
        out.size()));
  }
  if (count == 0) return {};

  const uint32_t k = static_cast<uint32_t>(first_sample % 8);
  const uint64_t first_byte = first_sample / 8;

  if (k == 0) {
    auto r = buffer.read(channel, first_byte, out);
    if (!r) return std::unexpected(r.error());
  } else {
    if (first_sample > (std::numeric_limits<uint64_t>::max)() - (count - 1)) {
      return std::unexpected(std::format(
          "sample range starting at {} of {} samples overflows", first_sample, count));
    }
    const uint64_t last_sample = first_sample + count - 1;
    const uint64_t last_byte = last_sample / 8;
    // WHY: Read only bytes holding a requested sample: the byte after the last one may not exist in the buffer.
    // Shift in place in `out` (no scratch, so no bad_alloc); out[i] must read out[i + 1] before it is rewritten.
    const uint64_t in_count = last_byte - first_byte + 1;

    auto r = buffer.read(channel, first_byte, out);
    if (!r) return std::unexpected(r.error());
    uint8_t extra = 0;
    if (in_count > out.size()) {
      auto e = buffer.read(channel, first_byte + out.size(), std::span<uint8_t>(&extra, 1));
      if (!e) return std::unexpected(e.error());
    }

    for (size_t i = 0; i < out.size(); ++i) {
      const uint8_t next = (i + 1 < out.size()) ? out[i + 1] : extra;
      out[i] = static_cast<uint8_t>((out[i] >> k) | (next << (8 - k)));
    }
  }

  if (const uint32_t used = static_cast<uint32_t>(count % 8); used != 0) {
    out[out.size() - 1] &= static_cast<uint8_t>((1u << used) - 1u);
  }
  return {};
}

}

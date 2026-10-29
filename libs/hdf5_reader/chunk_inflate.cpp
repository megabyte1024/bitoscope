/*
 * SPDX-License-Identifier: MIT
 */

#include "chunk_inflate.h"

#include <algorithm>
#include <cstring>
#include <format>
#include <limits>

#include <zlib.h>

namespace bitoscope::hdf5 {

std::expected<void, std::string> inflateChunk(std::span<const uint8_t> stored, bool deflated, std::span<uint8_t> out) {
  if (!deflated) {
    if (stored.size() != out.size()) {
      return std::unexpected(std::format("chunk holds {} bytes, {} expected", stored.size(), out.size()));
    }
    std::memcpy(out.data(), stored.data(), out.size());
    return {};
  }
  if (stored.empty() || stored.size() > (std::numeric_limits<uLong>::max)() ||
      out.size() > (std::numeric_limits<uLongf>::max)()) {
    return std::unexpected("deflated chunk has an unusable size");
  }
  uLongf produced = static_cast<uLongf>(out.size());
  const int status = uncompress(out.data(), &produced, stored.data(), static_cast<uLong>(stored.size()));
  if (status != Z_OK) return std::unexpected(std::format("inflate failed with zlib status {}", status));
  if (produced != out.size()) {
    return std::unexpected(std::format("chunk inflates to {} bytes, {} expected", static_cast<uint64_t>(produced), out.size()));
  }
  return {};
}

}

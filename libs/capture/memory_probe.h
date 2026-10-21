/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <cstdint>
#include <expected>
#include <string>

namespace bitoscope::capture {

class MemoryProbe {
 public:
  virtual ~MemoryProbe() = default;

  virtual std::expected<uint64_t, std::string> freePhysicalBytes() = 0;
  virtual std::expected<uint64_t, std::string> totalPhysicalBytes() = 0;
};

}

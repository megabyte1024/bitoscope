/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <span>
#include <vector>

namespace bitoscope::plugin_c_sdk {

class DeclaredIds {
 public:
  explicit DeclaredIds(std::span<const uint32_t> ids);

  bool contains(uint32_t id) const {
    return id < kDirect ? direct_[id] : std::binary_search(others_.begin(), others_.end(), id);
  }

 private:
  static constexpr uint32_t kDirect = 256;
  std::array<bool, kDirect> direct_{};
  std::vector<uint32_t> others_;
};

}

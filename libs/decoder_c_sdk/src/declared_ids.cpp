/*
 * SPDX-License-Identifier: MIT
 */

#include "declared_ids.h"

namespace bitoscope::plugin_c_sdk {

DeclaredIds::DeclaredIds(std::span<const uint32_t> ids) {
  for (const uint32_t id : ids) {
    if (id < kDirect) {
      direct_[id] = true;
    } else {
      others_.push_back(id);
    }
  }
  std::sort(others_.begin(), others_.end());
}

}

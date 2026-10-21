/*
 * SPDX-License-Identifier: MIT
 */

#include "system_resources.h"

#include <system_memory_probe.h>

namespace bitoscope::gui {

std::unique_ptr<capture::MemoryProbe> makeSystemMemoryProbe() {
  return std::make_unique<capture::SystemMemoryProbe>();
}

std::unique_ptr<capture::BlockSource> makeSystemBlockSource() {
  return std::make_unique<capture::SystemBlockSource>();
}

}

/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <memory>

#include <capture_allocator.h>
#include <memory_probe.h>

namespace bitoscope::gui {

std::unique_ptr<capture::MemoryProbe> makeSystemMemoryProbe();
std::unique_ptr<capture::BlockSource> makeSystemBlockSource();

}

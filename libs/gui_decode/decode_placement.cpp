/*
 * SPDX-License-Identifier: MIT
 */

#include "decode_placement.h"

#include <stdexcept>
#include <utility>

namespace bitoscope::gui_decode {

DecodePlacement decodePlacementFor(const cpu::ProcessorLayout& layout, std::optional<uint32_t> count) {
  cpu::DecodeThreads threads = cpu::decodeThreads(layout, count);
  return DecodePlacement{cpu::WorkPlacement{true, std::move(threads.hardware_threads)}, std::move(threads.note)};
}

std::expected<DecodePlacement, std::string> decodePlacementOfThisMachine(std::optional<uint32_t> count) {
  auto layout = cpu::readProcessorLayout();
  if (!layout) return std::unexpected(std::move(layout.error()));
  return decodePlacementFor(*layout, count);
}

DecodeThreadSetup decodeThreadSetup(const DecodePlacement& placement) {
  if constexpr (!cpu::kLowerPriorityDone && !cpu::kPlacementDone) return {};
  return [work = placement.work] {
    if (auto applied = cpu::applyToThisThread(work); !applied) throw std::runtime_error(applied.error());
  };
}

}

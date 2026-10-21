/*
 * SPDX-License-Identifier: MIT
 */

#include "option_text_buffers.h"

#include <algorithm>
#include <format>

namespace bitoscope::gui_decode {

OptionTextBuffer& optionTextBuffer(OptionTextBuffers& buffers, std::span<const DecoderConfig> configs,
                                   std::size_t index, std::string_view option, std::string_view value) {
  auto [it, inserted] = buffers.try_emplace(std::format("{}/{}", configs[index].id(), option));
  if (inserted) {
    const std::size_t n = (std::min)(value.size(), it->second.size() - 1);
    std::copy_n(value.begin(), n, it->second.begin());
    it->second[n] = '\0';
  }
  return it->second;
}

void pruneOptionTextBuffers(OptionTextBuffers& buffers, std::span<const DecoderConfig> configs) {
  // WHY: An option name holds no '/', so the id is everything before the last '/' of a key.
  std::erase_if(buffers, [&](const auto& entry) {
    const std::string_view key = entry.first;
    const std::string_view id = key.substr(0, key.rfind('/'));
    return std::none_of(configs.begin(), configs.end(), [&](const DecoderConfig& c) { return c.id() == id; });
  });
}

}

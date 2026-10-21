/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <cstdint>
#include <expected>
#include <optional>
#include <string>

#include "ipc_decoder.h"
#include "stream.h"

namespace bitoscope::decoder_host {

inline constexpr uint32_t kItemFeedBatch = 1024;

class InputItemFeed {
 public:
  InputItemFeed(std::string input, decode::StreamInput& source, uint32_t batch);

  std::expected<ipc::decoder::ItemFeed, std::string> next();

  const std::string& input() const { return input_; }
  bool ended() const { return ended_; }
  uint64_t fed() const { return fed_; }

 private:
  std::string input_;
  decode::StreamInput& source_;
  uint32_t batch_;
  std::optional<decode::StreamItem> ahead_;
  bool ended_ = false;
  uint64_t fed_ = 0;
};

}

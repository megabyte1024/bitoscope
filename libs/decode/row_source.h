/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <cstdint>
#include <expected>
#include <string>
#include <variant>
#include <vector>

namespace bitoscope::decode {

class Decoder;

struct RowSource {
  struct Unknown {
    bool operator==(const Unknown&) const = default;
  };
  struct WordList {
    bool operator==(const WordList&) const = default;
  };
  struct BitGrid {
    std::string rate_option;
    uint32_t offset_bits = 0;
    std::string count_option;
    std::string order_option;
    bool operator==(const BitGrid&) const = default;
  };
  struct ClockEdges {
    std::string clock_channel;
    std::string data_channel;
    std::string count_option;
    uint32_t count = 0;
    bool operator==(const ClockEdges&) const = default;
  };
  using Rule = std::variant<Unknown, WordList, BitGrid, ClockEdges>;

  uint32_t row = 0;
  uint32_t cls = 0;
  uint32_t source_row = 0;
  std::vector<uint32_t> source_classes;
  std::string enable_option;
  Rule rule;
  bool operator==(const RowSource&) const = default;
};

bool derivesAnnotations(const RowSource& source);

std::expected<void, std::string> checkRowSources(const Decoder& decoder);

}

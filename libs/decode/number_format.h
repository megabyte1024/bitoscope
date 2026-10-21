/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

namespace bitoscope::decode {

enum class HexStyle { Prefixed, Bare, Suffixed };

enum class DataFormat { Hex, Dec, Ascii };

struct NumberFormat {
  HexStyle style = HexStyle::Prefixed;
  DataFormat data = DataFormat::Hex;
  bool operator==(const NumberFormat&) const = default;
};

}

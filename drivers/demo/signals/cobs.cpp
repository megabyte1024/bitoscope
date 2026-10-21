/*
 * SPDX-License-Identifier: MIT
 */

#include "cobs.h"

namespace bitoscope::sim {

std::vector<uint8_t> encodeCobs(std::span<const uint8_t> data) {
  std::vector<uint8_t> out;
  std::size_t code_at = 0;
  uint8_t code = 1;
  bool open = false;
  const auto start = [&] {
    code_at = out.size();
    out.push_back(0);
    code = 1;
    open = true;
  };
  start();
  for (const uint8_t b : data) {
    if (!open) start();
    if (b == 0) {
      out[code_at] = code;
      start();
    } else {
      out.push_back(b);
      if (++code == 0xFF) {
        out[code_at] = code;
        open = false;
      }
    }
  }
  if (open) out[code_at] = code;
  return out;
}

}

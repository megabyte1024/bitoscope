/*
 * SPDX-License-Identifier: MIT
 */

#include "text_escape.h"

#include <cstdio>

namespace bitoscope::log {

std::string escapeControlChars(std::string_view text) {
  std::string out;
  out.reserve(text.size());
  for (unsigned char c : text) {
    switch (c) {
      case '\\':
        out += "\\\\";
        break;
      case '\n':
        out += "\\n";
        break;
      case '\r':
        out += "\\r";
        break;
      case '\t':
        out += "\\t";
        break;
      default:
        if (c < 0x20 || c == 0x7f) {
          char buf[5];
          std::snprintf(buf, sizeof(buf), "\\x%02x", static_cast<unsigned int>(c));
          out += buf;
        } else {
          out.push_back(static_cast<char>(c));
        }
        break;
    }
  }
  return out;
}

}

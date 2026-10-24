/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include "annotation_store.h"
#include "decoder.h"

namespace bitoscope::gui_decode {

inline constexpr std::size_t kMaxValueBytes = 4;

struct ValueBytes {
  std::array<std::uint8_t, kMaxValueBytes> values{};
  std::size_t size = 0;
  std::span<const std::uint8_t> span() const { return {values.data(), size}; }
  bool operator==(const ValueBytes&) const = default;
};

enum class ByteValueKind { U16Le, U16Be, U32Le, U32Be, F32Le, F32Be };

struct ByteValueLine {
  ByteValueKind kind = ByteValueKind::U16Le;
  std::string text;
  bool operator==(const ByteValueLine&) const = default;
};

ValueBytes bytesFrom(const ShownResult& shown, std::uint32_t row, const bitoscope::decode::Annotation& hovered,
                     std::size_t count);

std::vector<ByteValueLine> byteValueLines(std::span<const std::uint8_t> bytes);

}

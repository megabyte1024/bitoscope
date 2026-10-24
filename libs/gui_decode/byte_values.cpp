/*
 * SPDX-License-Identifier: MIT
 */

#include "byte_values.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <format>
#include <optional>

#include "byte_search.h"

namespace bitoscope::gui_decode {

namespace {

namespace wv = bitoscope::waveform_view;

constexpr std::uint64_t kLargestByte = 0xFF;
constexpr std::size_t kBytesOf16 = 2;
constexpr std::size_t kBytesOf32 = 4;
constexpr std::uint16_t kTopBit16 = 0x8000;
constexpr std::uint32_t kTopBit32 = 0x80000000u;

struct Stream {
  std::size_t group = 0;
  std::size_t next = 0;
  std::size_t size = 0;
  bool split = false;
};

std::size_t firstAtOrAfter(wv::AnnotationReader& reader, std::size_t group, std::uint64_t begin) {
  std::size_t probes = 0;
  return reader.firstBeginNotBefore(group, 0, [&](std::uint64_t b) { return b < begin; }, probes);
}

std::size_t firstAfter(wv::AnnotationReader& reader, std::size_t group, std::uint64_t begin) {
  std::size_t probes = 0;
  return reader.firstBeginNotBefore(group, 0, [&](std::uint64_t b) { return b <= begin; }, probes);
}

std::optional<std::size_t> indexOfHovered(wv::AnnotationReader& reader, std::size_t group,
                                          const decode::Annotation& hovered) {
  const std::size_t size = reader.groups().groupSize(group);
  for (std::size_t i = firstAtOrAfter(reader, group, hovered.begin); i < size && reader.begin(group, i) == hovered.begin;
       ++i) {
    if (reader.end(group, i) == hovered.end && reader.value(group, i) == hovered.value) return i;
  }
  return std::nullopt;
}

std::string integerText(std::uint32_t unsignedValue, std::int32_t signedValue, bool topBit) {
  std::string text = std::to_string(unsignedValue);
  if (topBit) text += " (" + std::to_string(signedValue) + ")";
  return text;
}

std::string floatText(std::uint32_t bits) {
  const float value = std::bit_cast<float>(bits);
  if (std::isnan(value)) return "NaN";
  if (std::isinf(value)) return value < 0 ? "-Inf" : "Inf";
  return std::format("{}", value);
}

std::uint32_t joined(std::span<const std::uint8_t> bytes, std::size_t count, bool bigEndian) {
  std::uint32_t value = 0;
  for (std::size_t k = 0; k < count; ++k) {
    const std::size_t at = bigEndian ? k : count - 1 - k;
    value = (value << 8) | bytes[at];
  }
  return value;
}

ByteValueLine integer16(std::span<const std::uint8_t> bytes, ByteValueKind kind, bool bigEndian) {
  const auto value = static_cast<std::uint16_t>(joined(bytes, kBytesOf16, bigEndian));
  return {kind, integerText(value, static_cast<std::int16_t>(value), (value & kTopBit16) != 0)};
}

ByteValueLine integer32(std::span<const std::uint8_t> bytes, ByteValueKind kind, bool bigEndian) {
  const std::uint32_t value = joined(bytes, kBytesOf32, bigEndian);
  return {kind, integerText(value, static_cast<std::int32_t>(value), (value & kTopBit32) != 0)};
}

}

ValueBytes bytesFrom(const ShownResult& shown, std::uint32_t row, const decode::Annotation& hovered, std::size_t count) {
  ValueBytes out;
  count = (std::min)(count, kMaxValueBytes);
  if (count == 0 || !shown.annotations || hovered.row != row || hovered.value > kLargestByte) return out;
  const std::vector<ByteRow> rows = byteRows(shown);
  const auto byteRow = std::find_if(rows.begin(), rows.end(), [&](const ByteRow& r) { return r.row == row; });
  if (byteRow == rows.end()) return out;
  const wv::AnnotationGroups& groups = shown.annotations->groups();
  const std::optional<std::size_t> hoveredGroup = groups.findGroup(row, hovered.cls);
  if (!hoveredGroup) return out;
  const auto hoveredStream = std::find(byteRow->groups.begin(), byteRow->groups.end(), *hoveredGroup);
  if (hoveredStream == byteRow->groups.end()) return out;

  wv::AnnotationReader reader(groups);
  const std::optional<std::size_t> hoveredIndex = indexOfHovered(reader, *hoveredGroup, hovered);
  if (!hoveredIndex) return out;
  out.values[out.size++] = static_cast<std::uint8_t>(hovered.value);

  std::vector<Stream> streams;
  const auto add = [&](std::size_t g, std::size_t next, bool split) {
    const std::size_t size = groups.groupSize(g);
    if (next < size) streams.push_back({g, next, size, split});
  };
  for (const std::size_t g : byteRow->groups) {
    if (g == *hoveredGroup) {
      add(g, *hoveredIndex + 1, false);
    } else if (g < *hoveredGroup) {
      add(g, firstAfter(reader, g, hovered.begin), false);
    } else {
      add(g, firstAtOrAfter(reader, g, hovered.begin), false);
    }
  }
  for (const std::size_t g : byteRow->splits) add(g, firstAtOrAfter(reader, g, hovered.begin), true);

  while (out.size < count && !streams.empty()) {
    std::size_t pick = 0;
    std::uint64_t pickBegin = reader.begin(streams[0].group, streams[0].next);
    for (std::size_t k = 1; k < streams.size(); ++k) {
      const std::uint64_t begin = reader.begin(streams[k].group, streams[k].next);
      if (begin < pickBegin) {
        pick = k;
        pickBegin = begin;
      }
    }
    Stream& stream = streams[pick];
    if (stream.split) break;
    const std::uint64_t value = reader.value(stream.group, stream.next);
    if (value > kLargestByte) break;
    out.values[out.size++] = static_cast<std::uint8_t>(value);
    if (++stream.next == stream.size) streams.erase(streams.begin() + static_cast<std::ptrdiff_t>(pick));
  }
  return out;
}

std::vector<ByteValueLine> byteValueLines(std::span<const std::uint8_t> bytes) {
  std::vector<ByteValueLine> lines;
  if (bytes.size() >= kBytesOf16) {
    lines.push_back(integer16(bytes, ByteValueKind::U16Le, false));
    lines.push_back(integer16(bytes, ByteValueKind::U16Be, true));
  }
  if (bytes.size() >= kBytesOf32) {
    lines.push_back(integer32(bytes, ByteValueKind::U32Le, false));
    lines.push_back(integer32(bytes, ByteValueKind::U32Be, true));
    lines.push_back({ByteValueKind::F32Le, floatText(joined(bytes, kBytesOf32, false))});
    lines.push_back({ByteValueKind::F32Be, floatText(joined(bytes, kBytesOf32, true))});
  }
  return lines;
}

}

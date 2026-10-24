/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>
#include <stop_token>
#include <vector>

#include "annotation_groups.h"
#include "annotation_store.h"
#include "decoder.h"

namespace bitoscope::gui_decode {

struct ByteRow {
  std::uint32_t row = 0;
  std::vector<std::size_t> groups;
  std::vector<std::size_t> splits;
  bool operator==(const ByteRow&) const = default;
};

struct ByteMatch {
  std::uint64_t begin = 0;
  std::uint64_t end = 0;
  std::uint32_t row = 0;
  bool operator==(const ByteMatch&) const = default;
};

enum class ByteSearchFailure { Stopped, OutOfMemory };

bool holdsBytes(const bitoscope::decode::AnnotationClass& cls, const bitoscope::decode::OptionMap& options);

std::vector<ByteRow> byteRows(const ShownResult& shown);

std::expected<std::vector<ByteMatch>, ByteSearchFailure> findByteMatches(
    const bitoscope::waveform_view::AnnotationGroups& groups, std::span<const ByteRow> rows,
    std::span<const std::uint8_t> pattern, bool caseSensitive, std::stop_token stop);

}

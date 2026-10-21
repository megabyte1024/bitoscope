/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "annotation_groups.h"
#include "annotation_reader.h"
#include "decoder.h"
#include "number_format.h"

namespace bitoscope::gui_decode {

struct WordListSource {
  std::uint32_t row = 0;
  std::uint32_t cls = 0;
  bool operator==(const WordListSource&) const = default;
};

std::optional<WordListSource> wordListSource(const std::vector<bitoscope::decode::RowSource>& sources,
                                             std::uint32_t row, std::uint32_t cls);

struct WordListText {
  std::string text;
  std::size_t probes = 0;
};

WordListText composeWordListText(const bitoscope::waveform_view::AnnotationGroups& groups, WordListSource source,
                                 std::uint64_t begin, std::uint64_t end,
                                 const std::vector<bitoscope::decode::AnnotationClass>& classes,
                                 const bitoscope::decode::OptionMap& options,
                                 const bitoscope::decode::NumberFormat& format, std::size_t maxChars);
WordListText composeWordListText(bitoscope::waveform_view::AnnotationReader& reader, WordListSource source,
                                 std::uint64_t begin, std::uint64_t end,
                                 const std::vector<bitoscope::decode::AnnotationClass>& classes,
                                 const bitoscope::decode::OptionMap& options,
                                 const bitoscope::decode::NumberFormat& format, std::size_t maxChars);

}

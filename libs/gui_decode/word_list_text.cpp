/*
 * SPDX-License-Identifier: MIT
 */

#include "word_list_text.h"

#include <optional>
#include <string_view>
#include <variant>

#include "annotation_format.h"
#include "annotation_text.h"

namespace bitoscope::gui_decode {

namespace {

constexpr std::string_view kEllipsis = "...";

}

std::optional<WordListSource> wordListSource(const std::vector<bitoscope::decode::RowSource>& sources,
                                             std::uint32_t row, std::uint32_t cls) {
  for (const bitoscope::decode::RowSource& s : sources) {
    if (s.row != row || s.cls != cls) continue;
    if (!std::holds_alternative<bitoscope::decode::RowSource::WordList>(s.rule)) continue;
    if (s.source_classes.size() != 1) return std::nullopt;
    return WordListSource{s.source_row, s.source_classes.front()};
  }
  return std::nullopt;
}

WordListText composeWordListText(const bitoscope::waveform_view::AnnotationGroups& groups, WordListSource source,
                                 std::uint64_t begin, std::uint64_t end,
                                 const std::vector<bitoscope::decode::AnnotationClass>& classes,
                                 const bitoscope::decode::OptionMap& options,
                                 const bitoscope::decode::NumberFormat& format, std::size_t maxChars) {
  bitoscope::waveform_view::AnnotationReader reader(groups);
  return composeWordListText(reader, source, begin, end, classes, options, format, maxChars);
}

WordListText composeWordListText(bitoscope::waveform_view::AnnotationReader& reader, WordListSource source,
                                 std::uint64_t begin, std::uint64_t end,
                                 const std::vector<bitoscope::decode::AnnotationClass>& classes,
                                 const bitoscope::decode::OptionMap& options,
                                 const bitoscope::decode::NumberFormat& format, std::size_t maxChars) {
  const bitoscope::waveform_view::AnnotationGroups& groups = reader.groups();
  WordListText result;
  if (begin >= end) return result;
  const std::optional<std::size_t> group = groups.findGroup(source.row, source.cls);
  if (!group) return result;
  const std::uint32_t g = static_cast<std::uint32_t>(*group);
  const std::size_t n = groups.groupSize(g);

  const std::size_t low =
    reader.firstBeginNotBeforeByTable(g, [begin](std::uint64_t b) { return b < begin; }, result.probes);

  std::string& text = result.text;
  std::size_t keep = 0;
  for (std::size_t i = low; i < n; ++i) {
    ++result.probes;
    const bitoscope::decode::Annotation a = reader.at({g, i});
    if (a.begin >= end) return result;
    if (a.end > end) continue;
    const std::string word = annotationText(groups, classes, a, options, format);
    const std::size_t separator = text.empty() ? 0 : 1;
    if (text.size() + separator + word.size() > maxChars) {
      if (maxChars < kEllipsis.size()) return {std::string(), result.probes};
      text.resize(keep);
      if (!text.empty()) text += ' ';
      text += kEllipsis;
      return result;
    }
    if (separator != 0) text += ' ';
    text += word;
    if (text.size() + 1 + kEllipsis.size() <= maxChars) keep = text.size();
  }
  return result;
}

}

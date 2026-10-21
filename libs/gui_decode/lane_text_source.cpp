/*
 * SPDX-License-Identifier: MIT
 */

#include "lane_text_source.h"

#include <utility>

#include "annotation_text.h"
#include "word_list_text.h"

namespace bitoscope::gui_decode {

namespace decode = bitoscope::decode;

AnnotationTextSource::AnnotationTextSource(const ShownResult* shown, const DecoderConfig& config,
                                           decode::HexStyle hexStyle, std::span<const decode::RowSource> rowSources,
                                           std::function<float(std::string_view)> measure)
    : shown_(shown), config_(config), hexStyle_(hexStyle), rowSources_(rowSources.begin(), rowSources.end()),
      measure_(std::move(measure)) {}

decode::NumberFormat AnnotationTextSource::formatOf(const decode::Annotation& annotation) const {
  return decode::NumberFormat{hexStyle_, config_.rowDataFormat(annotation.row)};
}

std::string AnnotationTextSource::text(const decode::Annotation& annotation) const {
  static const bitoscope::waveform_view::AnnotationGroups kNoGroups;
  static const DecoderConfig::AnnotationFormatContext kNoFormat;
  const bool held = shown_ != nullptr && shown_->annotations != nullptr;
  const DecoderConfig::AnnotationFormatContext& format = shown_ != nullptr ? shown_->format : kNoFormat;
  return annotationText(held ? shown_->annotations->groups() : kNoGroups, format.classes, annotation, format.options,
                        formatOf(annotation));
}

bool AnnotationTextSource::listsWords(const decode::Annotation& annotation) const {
  if (shown_ == nullptr || shown_->annotations == nullptr) return false;
  return wordListSource(rowSources_, annotation.row, annotation.cls).has_value();
}

std::string AnnotationTextSource::wordList(const decode::Annotation& annotation, std::size_t maxChars) const {
  if (shown_ == nullptr || shown_->annotations == nullptr) return {};
  const auto source = wordListSource(rowSources_, annotation.row, annotation.cls);
  if (!source) return {};
  return composeWordListText(shown_->annotations->groups(), *source, annotation.begin, annotation.end,
                             shown_->format.classes, shown_->format.options, formatOf(annotation), maxChars)
    .text;
}

float AnnotationTextSource::width(std::string_view text) const { return measure_(text); }

}

/*
 * SPDX-License-Identifier: MIT
 */

#include "annotation_text.h"

#include <optional>

#include "annotation_format.h"

namespace bitoscope::gui_decode {

std::string annotationText(const bitoscope::waveform_view::AnnotationGroups& groups,
                           const std::vector<bitoscope::decode::AnnotationClass>& classes,
                           const bitoscope::decode::Annotation& annotation,
                           const bitoscope::decode::OptionMap& options,
                           const bitoscope::decode::NumberFormat& format) {
  if (!bitoscope::decode::showsBytes(classes, annotation.cls)) {
    return bitoscope::decode::formatAnnotation(classes, annotation, options, format);
  }
  const std::optional<std::size_t> group = groups.findGroup(annotation.row, annotation.cls);
  const std::span<const std::uint8_t> bytes =
      group ? groups.bytesAt(*group, annotation.value) : std::span<const std::uint8_t>();
  return bitoscope::decode::formatAnnotation(classes, annotation, options, format, bytes);
}

}

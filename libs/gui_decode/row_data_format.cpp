/*
 * SPDX-License-Identifier: MIT
 */

#include "row_data_format.h"

#include <algorithm>
#include <optional>

#include "annotation_format.h"
#include "word_list_text.h"

namespace bitoscope::gui_decode {

namespace decode = bitoscope::decode;

bool rowTakesDataFormat(std::uint32_t row, std::span<const std::uint32_t> presentClasses,
                        const std::vector<decode::AnnotationClass>& classes,
                        const std::vector<decode::RowSource>& rowSources) {
  return std::ranges::any_of(presentClasses, [&](std::uint32_t cls) {
    const std::optional<WordListSource> list = wordListSource(rowSources, row, cls);
    const auto shown = std::ranges::find(classes, list ? list->cls : cls, &decode::AnnotationClass::id);
    return shown != classes.end() && decode::takesDataFormat(*shown);
  });
}

}

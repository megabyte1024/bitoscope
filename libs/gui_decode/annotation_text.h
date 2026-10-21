/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <string>
#include <vector>

#include "annotation_groups.h"
#include "decoder.h"
#include "number_format.h"

namespace bitoscope::gui_decode {

std::string annotationText(const bitoscope::waveform_view::AnnotationGroups& groups,
                           const std::vector<bitoscope::decode::AnnotationClass>& classes,
                           const bitoscope::decode::Annotation& annotation,
                           const bitoscope::decode::OptionMap& options,
                           const bitoscope::decode::NumberFormat& format);

}

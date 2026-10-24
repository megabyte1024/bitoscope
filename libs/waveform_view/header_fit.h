/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <span>
#include <vector>

namespace bitoscope::waveform_view {

struct HeaderParts {
  bool visible = true;
  double indent = 0.0;
  double leftZone = 0.0;
  double marker = 0.0;
  double markerGap = 0.0;
  double text = 0.0;
  double textGap = 0.0;
  double pin = 0.0;
  double pinGap = 0.0;
  double trailing = 0.0;
  double minimum = 0.0;
};

double headerNeed(const HeaderParts& header);

double fitHeaderWidth(std::span<const HeaderParts> headers, double padding, double minimum, double maximum);

struct DecoderHeaderMetrics {
  double indent = 0.0;
  double leftZone = 0.0;
  double textGap = 0.0;
  double pin = 0.0;
  double pinGap = 0.0;
  double trailing = 0.0;
};

std::vector<HeaderParts> decoderLaneHeaders(std::span<const double> rowTextWidths,
                                            const DecoderHeaderMetrics& metrics);

}

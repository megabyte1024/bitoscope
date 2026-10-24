/*
 * SPDX-License-Identifier: MIT
 */

#include "header_fit.h"

#include <algorithm>

namespace bitoscope::waveform_view {

double headerNeed(const HeaderParts& header) {
  double need = header.indent + header.leftZone + header.marker + header.markerGap + header.text + header.trailing;
  if (header.pin > 0.0) {
    need += header.textGap + header.pin + header.pinGap;
  }
  return (std::max)(need, header.minimum);
}

double fitHeaderWidth(std::span<const HeaderParts> headers, double padding, double minimum, double maximum) {
  double widest = 0.0;
  bool any = false;
  for (const HeaderParts& header : headers) {
    if (!header.visible) continue;
    any = true;
    widest = (std::max)(widest, headerNeed(header) + padding);
  }
  if (!any) {
    return minimum;
  }
  return (std::max)(minimum, (std::min)(widest, maximum));
}

std::vector<HeaderParts> decoderLaneHeaders(std::span<const double> rowTextWidths,
                                            const DecoderHeaderMetrics& metrics) {
  std::vector<HeaderParts> headers;
  headers.reserve(rowTextWidths.size());
  for (std::size_t row = 0; row < rowTextWidths.size(); ++row) {
    HeaderParts parts;
    if (row == 0) {
      parts.indent = metrics.indent;
      parts.leftZone = metrics.leftZone;
    } else {
      parts.indent = metrics.indent + metrics.leftZone;
    }
    parts.text = rowTextWidths[row];
    parts.textGap = metrics.textGap;
    parts.pin = metrics.pin;
    parts.pinGap = metrics.pinGap;
    parts.trailing = metrics.trailing;
    headers.push_back(parts);
  }
  return headers;
}

}

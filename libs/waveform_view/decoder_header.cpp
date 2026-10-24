/*
 * SPDX-License-Identifier: MIT
 */

#include "decoder_header.h"

#include <algorithm>
#include <cmath>

namespace bitoscope::waveform_view {

DecoderHeaderLayout decoderHeaderLayout(const DecoderHeaderSizes& sizes) {
  DecoderHeaderLayout layout;
  const double top = std::floor((sizes.rowHeight - sizes.box) * 0.5);
  layout.square = HeaderRect{sizes.indent, top, sizes.box, sizes.box};
  const double minimum = std::round(sizes.em * kDecoderNameBarMinimumEm);
  const double maximum = (std::max)(minimum, std::round(sizes.em * kDecoderNameBarMaximumEm));
  const double wanted = std::ceil(sizes.nameText + 2.0 * sizes.namePadding);
  const double preferred = std::clamp(wanted, minimum, maximum);
  const double room = std::floor(sizes.columnRight - sizes.indent - sizes.box - sizes.boxGap - sizes.zoneGap -
                                 sizes.widestRowText);
  const double barWidth = (std::max)(sizes.box, (std::min)(preferred, room));
  layout.bar = HeaderRect{sizes.indent + sizes.box + sizes.boxGap, top, barWidth, sizes.box};
  layout.leftZone = sizes.box + sizes.boxGap + barWidth;
  return layout;
}

DecoderRowPlace decoderRowPlace(const DecoderHeaderSizes& sizes, const DecoderHeaderLayout& layout) {
  const double left = sizes.indent + layout.leftZone + sizes.zoneGap;
  DecoderRowPlace place;
  place.textX = (std::max)(left, sizes.columnRight - sizes.widestRowText);
  return place;
}

std::optional<HeaderSpan> clipToHeaderColumn(HeaderSpan control, double columnRight) {
  const HeaderSpan visible{(std::max)(control.left, 0.0), (std::min)(control.right, columnRight)};
  if (visible.right <= visible.left) {
    return std::nullopt;
  }
  return visible;
}

std::optional<HeaderSpan> decoderNameFieldSpan(const DecoderHeaderLayout& layout, double left, double columnRight) {
  return clipToHeaderColumn(HeaderSpan{left, layout.bar.x + layout.bar.width}, columnRight);
}

}

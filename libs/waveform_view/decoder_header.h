/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <cstddef>
#include <optional>

namespace bitoscope::waveform_view {

inline constexpr double kDecoderNameBarMinimumEm = 6.0;
inline constexpr double kDecoderNameBarMaximumEm = 14.0;

struct HeaderRect {
  double x = 0.0;
  double y = 0.0;
  double width = 0.0;
  double height = 0.0;
  bool operator==(const HeaderRect&) const = default;
};

struct DecoderHeaderSizes {
  double rowHeight = 0.0;
  double em = 0.0;
  double indent = 0.0;
  double box = 0.0;
  double boxGap = 0.0;
  double nameText = 0.0;
  double namePadding = 0.0;
  double zoneGap = 0.0;
  double triangle = 0.0;
  double triangleGap = 0.0;
  double columnRight = 0.0;
  double widestRowText = 0.0;
};

struct DecoderHeaderLayout {
  HeaderRect square;
  HeaderRect bar;
  double leftZone = 0.0;
};

DecoderHeaderLayout decoderHeaderLayout(const DecoderHeaderSizes& sizes);

struct DecoderRowPlace {
  double triangleX = 0.0;
  double textX = 0.0;
};

DecoderRowPlace decoderRowPlace(const DecoderHeaderSizes& sizes, const DecoderHeaderLayout& layout);

struct HeaderSpan {
  double left = 0.0;
  double right = 0.0;
  bool operator==(const HeaderSpan&) const = default;
};

std::optional<HeaderSpan> clipToHeaderColumn(HeaderSpan control, double columnRight);

std::optional<HeaderSpan> decoderNameFieldSpan(const DecoderHeaderLayout& layout, double left, double columnRight);

}

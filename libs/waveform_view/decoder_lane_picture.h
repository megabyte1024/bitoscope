/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "annotation_colors.h"
#include "annotation_groups.h"
#include "color_choice.h"
#include "decoder.h"
#include "annotation_row_layout.h"
#include "decoder_visibility.h"
#include "decoding_prefix.h"
#include "derived_lane_rows.h"
#include "viewport.h"

namespace bitoscope::waveform_view {

class LaneTextSource {
 public:
  virtual ~LaneTextSource() = default;
  virtual std::string text(const decode::Annotation& annotation) const = 0;
  virtual bool listsWords(const decode::Annotation& annotation) const = 0;
  virtual std::string wordList(const decode::Annotation& annotation, std::size_t maxChars) const = 0;
  virtual float width(std::string_view text) const = 0;
};

using PackColor = std::uint32_t (*)(Rgb8 color);

struct LanePictureStyle {
  float em = 0.0f;
  float decoderPitch = 0.0f;
  float lineWidth = 1.0f;
  float framebufferScale = 1.0f;
  bool dark = false;
  Rgb8 neutralFill;
  std::uint32_t ink = 0;
  std::uint32_t hatchFill = 0;
  std::uint32_t hatchStripe = 0;
  bool operator==(const LanePictureStyle&) const = default;
};

struct LanePictureGeometry {
  float traceLeft = 0.0f;
  float traceRight = 0.0f;
  std::vector<float> rowTops;
  bool operator==(const LanePictureGeometry&) const = default;
};

struct LanePictureInput {
  const AnnotationGroups* groups = nullptr;
  const DerivedRows* derived = nullptr;
  std::span<const decode::AnnotationRow> rows;
  const DecoderVisibility* visibility = nullptr;
  std::span<const decode::AnnotationClass> classes;
  LaneDrawing drawing = LaneDrawing::Nothing;
  std::uint64_t watermark = 0;
  Viewport columns{1, 1};
  std::uint32_t columnCount = 1;
  LanePictureGeometry geometry;
  LanePictureStyle style;
  PackColor pack = nullptr;
  const LaneTextSource* text = nullptr;
};

enum class LanePrimitiveKind : std::uint8_t { Rect, Polygon, Text, Line, PushClip, PopClip };

struct LanePoint {
  float x = 0.0f;
  float y = 0.0f;
  bool operator==(const LanePoint&) const = default;
};

struct LanePrimitive {
  LanePrimitiveKind kind = LanePrimitiveKind::Rect;
  std::uint8_t pointCount = 0;
  std::uint32_t color = 0;
  std::uint32_t outline = 0;
  std::array<LanePoint, 6> points{};
  std::uint32_t textBegin = 0;
  std::uint32_t textEnd = 0;
  bool operator==(const LanePrimitive&) const = default;
};

struct LanePicture {
  std::vector<LanePrimitive> primitives;
  std::string text;
  float lineWidth = 1.0f;
  bool summarized = false;
  std::vector<RowLayout> rowLayouts;
  std::optional<HatchedColumns> hatched;
  bool operator==(const LanePicture&) const = default;
  std::string_view textOf(const LanePrimitive& primitive) const;
};

LanePicture drawLanePicture(const LanePictureInput& input);

}

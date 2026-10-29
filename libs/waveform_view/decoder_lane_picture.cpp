/*
 * SPDX-License-Identifier: MIT
 */

#include "decoder_lane_picture.h"

#include <algorithm>
#include <cmath>

#include "annotation_lane_index.h"
#include "annotation_row_summary.h"
#include "annotation_shape.h"
#include "lane_coverage.h"

namespace bitoscope::waveform_view {

std::string_view LanePicture::textOf(const LanePrimitive& primitive) const {
  return std::string_view(text).substr(primitive.textBegin, primitive.textEnd - primitive.textBegin);
}

namespace {

constexpr float kBitMarkHeightFraction = 0.5f;

struct MarkBand {
  float top = 0.0f;
  float bottom = 0.0f;
};

class PictureBuilder {
 public:
  PictureBuilder(const LanePictureInput& in, LanePicture& out)
      : in_(in), style_(in.style), out_(out), markInset_(in.style.em * 0.15f),
        minTextWidth_(in.text != nullptr ? in.text->width("0") : 0.0f) {}

  float columnX(double column) const {
    return static_cast<float>(overlayColumnX(in_.geometry.traceLeft, style_.framebufferScale, column));
  }

  float snapped(float v) const { return static_cast<float>(filledEdge(v, style_.framebufferScale)); }

  void rect(LanePoint min, LanePoint max, std::uint32_t color) {
    // WHY: Snap fill edges to the pixels they cover: Dear ImGui anti-aliases a fractional edge, which blurs it.
    LanePrimitive p;
    p.kind = LanePrimitiveKind::Rect;
    p.pointCount = 2;
    p.color = color;
    p.points[0] = LanePoint{snapped(min.x), snapped(min.y)};
    p.points[1] = LanePoint{snapped(max.x), snapped(max.y)};
    out_.primitives.push_back(p);
  }

  void polygon(const AnnotationShape& shape, std::uint32_t fill, std::uint32_t outline) {
    LanePrimitive p;
    p.kind = LanePrimitiveKind::Polygon;
    p.pointCount = static_cast<std::uint8_t>(shape.count);
    p.color = fill;
    p.outline = outline;
    for (std::size_t i = 0; i < shape.count; ++i) {
      p.points[i] = LanePoint{static_cast<float>(shape.points[i].x), static_cast<float>(shape.points[i].y)};
    }
    out_.primitives.push_back(p);
  }

  void text(LanePoint at, std::uint32_t color, std::string_view s) {
    LanePrimitive p;
    p.kind = LanePrimitiveKind::Text;
    p.pointCount = 1;
    p.color = color;
    p.points[0] = at;
    p.textBegin = static_cast<std::uint32_t>(out_.text.size());
    out_.text.append(s);
    p.textEnd = static_cast<std::uint32_t>(out_.text.size());
    out_.primitives.push_back(p);
  }

  void line(LanePoint a, LanePoint b, std::uint32_t color) {
    LanePrimitive p;
    p.kind = LanePrimitiveKind::Line;
    p.pointCount = 2;
    p.color = color;
    p.points[0] = a;
    p.points[1] = b;
    out_.primitives.push_back(p);
  }

  void pushClip(LanePoint min, LanePoint max) {
    LanePrimitive p;
    p.kind = LanePrimitiveKind::PushClip;
    p.pointCount = 2;
    p.points[0] = min;
    p.points[1] = max;
    out_.primitives.push_back(p);
  }

  void popClip() {
    LanePrimitive p;
    p.kind = LanePrimitiveKind::PopClip;
    out_.primitives.push_back(p);
  }

  std::uint32_t pack(Rgb8 color) const { return in_.pack(color); }

  Rgb8 fillOf(std::uint32_t row, std::uint32_t cls) const { return classFill(fills_, row, cls, style_.neutralFill); }

  bool bitClass(std::uint32_t cls) const { return std::ranges::find(bitClasses_, cls) != bitClasses_.end(); }

  MarkBand markBand(float bandTop, std::uint32_t cls) const {
    const MarkBand full{bandTop + markInset_, bandTop + style_.decoderPitch - markInset_};
    if (!bitClass(cls)) return full;
    const float margin = (full.bottom - full.top) * (1.0f - kBitMarkHeightFraction) * 0.5f;
    return MarkBand{snapped(full.top + margin), snapped(full.bottom - margin)};
  }

  void addCondensed(std::vector<CondensedMark>& marks, double firstX, double lastX, const decode::Annotation& a) const {
    const double width = static_cast<double>(in_.columnCount);
    if (lastX < 0.0 || firstX >= width) return;
    const auto first = static_cast<std::uint32_t>((std::max)(0.0, std::floor(firstX)));
    const auto last = static_cast<std::uint32_t>((std::min)(width - 1.0, std::floor(lastX)));
    if (first <= last) marks.push_back({first, last, a.cls, a.end - a.begin});
  }

  std::string fittedListText(const decode::Annotation& a, float room) const {
    std::size_t maxChars = static_cast<std::size_t>(2.0f * (std::max)(0.0f, room) / minTextWidth_);
    std::string fitted = in_.text->wordList(a, maxChars);
    for (int attempt = 0; attempt < 6 && !fitted.empty(); ++attempt) {
      const float width = in_.text->width(fitted);
      if (width <= room) break;
      const auto shrunk = static_cast<std::size_t>(static_cast<float>(fitted.size()) * room / width);
      maxChars = (std::min)(shrunk, fitted.size() - 1);
      fitted = in_.text->wordList(a, maxChars);
    }
    return fitted;
  }

  void shape(const decode::Annotation& a, float bandTop, std::vector<CondensedMark>* condensed = nullptr) {
    const float traceLeft = in_.geometry.traceLeft;
    const float traceRight = in_.geometry.traceRight;
    const AnnotationSpan span =
      annotationSpanColumns(in_.columns, a.begin, a.end, static_cast<double>(in_.columnCount));
    const float x0 = columnX(span.x0);
    const float x1 = columnX(span.x1);
    const Rgb8 fill = fillOf(a.row, a.cls);
    const bool bit = bitClass(a.cls);
    const MarkBand band = markBand(bandTop, a.cls);
    const double maxPoint = style_.em * kHexagonMaxPointEm * (bit ? kBitMarkHeightFraction : 1.0f);
    const AnnotationShape outline = annotationShape(x0, x1, band.top, band.bottom, maxPoint);
    if (outline.count == 0) return;
    if (outline.depth > 0.0) {
      const std::uint32_t inside = bit && a.value == 0 ? 0u : pack(fill);
      polygon(outline, inside, pack(annotationOutlineColor(fill, style_.dark)));
    } else if (condensed != nullptr) {
      addCondensed(*condensed, span.x0, span.x1, a);
      return;
    } else {
      const float half = style_.lineWidth * 0.5f;
      rect(LanePoint{x0 - half, band.top}, LanePoint{x1 + half, band.bottom}, pack(fill));
    }
    if (in_.text == nullptr || bit) return;
    if (x1 - x0 - 2.0f * static_cast<float>(outline.depth) < minTextWidth_) return;
    std::string markText;
    if (in_.text->listsWords(a)) {
      const double room = (std::min)(x1 - outline.depth, static_cast<double>(traceRight)) -
                          (std::max)(x0 + outline.depth, static_cast<double>(traceLeft)) - 0.5;
      markText = fittedListText(a, static_cast<float>(room));
      if (markText.empty()) return;
    } else {
      markText = in_.text->text(a);
    }
    const float textWidth = in_.text->width(markText);
    const auto left = annotationTextLeft(x0, x1, outline.depth, textWidth, traceLeft, traceRight);
    if (!left) return;
    text(LanePoint{static_cast<float>(*left), bandTop + (style_.decoderPitch - style_.em) * 0.5f}, style_.ink, markText);
  }

  void runs(std::span<const ColumnRun> columnRuns, std::uint32_t row, float bandTop) {
    for (const ColumnRun& run : columnRuns) {
      const MarkBand band = markBand(bandTop, run.cls);
      rect(LanePoint{columnX(run.firstColumn), band.top}, LanePoint{columnX(run.lastColumn + 1.0), band.bottom},
           pack(fillOf(row, run.cls)));
    }
  }

  void build() {
    out_.lineWidth = style_.lineWidth;
    if (in_.groups == nullptr || in_.derived == nullptr || in_.visibility == nullptr) return;
    const AnnotationGroups& groups = *in_.groups;
    const DerivedRows& derived = *in_.derived;
    const DecoderVisibility& visibility = *in_.visibility;
    const std::span<const decode::AnnotationRow> rows = in_.rows;
    const std::vector<float>& rowTops = in_.geometry.rowTops;
    fills_ = annotationClassFills(rows, laneRowClassPairs(groups, derived), in_.classes, style_.dark,
                                  style_.neutralFill);
    for (const decode::AnnotationClass& cls : in_.classes) {
      if (cls.role == decode::AnnotationRole::Bit) bitClasses_.push_back(cls.id);
    }
    const double rangeBeginD = in_.columns.firstVisibleSample();
    const double rangeEndD = rangeBeginD + in_.columns.samplesPerPixel() * static_cast<double>(in_.columnCount);
    const std::uint64_t rangeBegin = static_cast<std::uint64_t>((std::max)(0.0, std::floor(rangeBeginD)));
    const std::uint64_t rangeEnd = rangeEndD > static_cast<double>(rangeBegin)
                                     ? static_cast<std::uint64_t>(std::ceil((std::max)(0.0, rangeEndD)))
                                     : rangeBegin + 1;
    const AnnotationRange inView = groups.range(rangeBegin, rangeEnd);
    const double first = in_.columns.firstVisibleSample();
    const double spp = in_.columns.samplesPerPixel();
    const std::uint32_t columnCount = in_.columnCount;
    out_.hatched = hatchedColumns(in_.drawing, in_.watermark, first, spp, columnCount);

    out_.summarized = groups.usable() && groups.coverageUsable() &&
                      laneNeedsSummary(laneRangeCount(groups, inView, derived, rangeBegin, rangeEnd), columnCount);
    if (out_.summarized) {
      const LaneCoverage coverage(groups, first, spp, columnCount);
      AnnotationReader reader(groups);
      const AnnotationLaneSummary summary = summarizeLane(reader, coverage, first, spp, columnCount);
      for (std::size_t slot = 0; slot < rows.size(); ++slot) {
        const RowSummary rowSummary =
          summarizeLaneRow(reader, coverage, derived, rows[slot].id, visibility, first, spp, columnCount);
        runs(solidRuns(withoutColumns(rowSummary.runs, rowTextMarks(groups, summary.textMarks, rows[slot].id))),
             rows[slot].id, rowTops[slot]);
      }
      for (const LaneTextMark& mark : summary.textMarks) {
        const decode::Annotation a = groups.at(mark.annotation);
        const auto slot = rowSlot(rows, a.row);
        if (!slot || classHidden(visibility, a.row, a.cls)) continue;
        shape(a, rowTops[*slot]);
      }
    } else {
      out_.rowLayouts = layoutLaneRows(groups, inView, derived, rows, visibility, first, spp, columnCount, rangeBegin,
                                       rangeEnd);
      for (std::size_t slot = 0; slot < rows.size(); ++slot) {
        const float bandTop = rowTops[slot];
        const RowLayout& layout = out_.rowLayouts[slot];
        std::vector<CondensedMark> condensed;
        for (const RowMark& mark : layout.marks) {
          const decode::Annotation& a = layout.annotations[mark.annotationIndex];
          if (mark.tick) {
            addCondensed(condensed, mark.x0, mark.x0, a);
          } else {
            shape(a, bandTop, &condensed);
          }
        }
        runs(solidBlocks(condensed), rows[slot].id, bandTop);
      }
    }
  }

  void hatch() {
    if (!out_.hatched) return;
    const float traceLeft = in_.geometry.traceLeft;
    const float hatchLeft = columnX(out_.hatched->first);
    const float hatchRight = columnX(out_.hatched->end);
    const float spacing = (std::max)(4.0f, std::round(style_.em * 0.5f));
    for (std::size_t slot = 0; slot < in_.geometry.rowTops.size(); ++slot) {
      const float bandTop = in_.geometry.rowTops[slot];
      const float bandBottom = in_.geometry.rowTops[slot] + style_.decoderPitch;
      pushClip(LanePoint{hatchLeft, bandTop}, LanePoint{hatchRight, bandBottom});
      rect(LanePoint{hatchLeft, bandTop}, LanePoint{hatchRight, bandBottom}, style_.hatchFill);
      const float height = bandBottom - bandTop;
      const float firstX = traceLeft + std::floor((hatchLeft - height - traceLeft) / spacing) * spacing;
      for (float x = firstX; x < hatchRight; x += spacing) {
        line(LanePoint{x + 0.5f, bandBottom + 0.5f}, LanePoint{x + height + 0.5f, bandTop + 0.5f}, style_.hatchStripe);
      }
      popClip();
    }
  }

 private:
  const LanePictureInput& in_;
  const LanePictureStyle& style_;
  LanePicture& out_;
  const float markInset_;
  const float minTextWidth_;
  std::vector<ClassFill> fills_;
  std::vector<std::uint32_t> bitClasses_;
};

}

LanePicture drawLanePicture(const LanePictureInput& input) {
  LanePicture picture;
  PictureBuilder builder(input, picture);
  builder.build();
  builder.hatch();
  return picture;
}

}

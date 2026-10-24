/*
 * SPDX-License-Identifier: MIT
 */

#include "annotation_colors.h"

#include <algorithm>
#include <array>
#include <cstddef>

namespace bitoscope::waveform_view {

namespace {

using decode::AnnotationRole;

using Palette = std::array<Rgb8, 9>;

constexpr std::array<Palette, kAnnotationColorVariants> kLight = {{
    {{
        {130, 210, 110},
        {255, 200, 60},
        {240, 110, 100},
        {80, 200, 190},
        {200, 140, 230},
        {120, 170, 250},
        {250, 150, 70},
        {160, 180, 200},
        {200, 200, 200},
    }},
    {{
        {250, 150, 200},
        {220, 220, 120},
        {200, 120, 90},
        {130, 220, 170},
        {240, 120, 240},
        {80, 210, 250},
        {230, 170, 110},
        {200, 180, 150},
        {230, 230, 230},
    }},
}};

constexpr std::array<Palette, kAnnotationColorVariants> kDark = {{
    {{
        {30, 120, 50},
        {140, 100, 0},
        {190, 50, 50},
        {0, 125, 125},
        {140, 60, 180},
        {50, 100, 200},
        {180, 85, 20},
        {80, 105, 140},
        {115, 115, 115},
    }},
    {{
        {170, 50, 120},
        {110, 120, 20},
        {150, 40, 30},
        {0, 110, 170},
        {110, 40, 140},
        {40, 70, 160},
        {150, 100, 50},
        {110, 95, 70},
        {70, 70, 70},
    }},
}};

AnnotationRole roleOf(std::span<const decode::AnnotationClass> classes, std::uint32_t cls) {
  for (const decode::AnnotationClass& c : classes) {
    if (c.id == cls) return c.role;
  }
  return AnnotationRole::None;
}

}

Rgb8 defaultAnnotationColor(AnnotationRole role, bool dark, Rgb8 neutral, std::size_t variant) {
  if (role == AnnotationRole::None) return neutral;
  const auto index = static_cast<std::size_t>(role) - 1;
  const Palette& table = (dark ? kDark : kLight)[variant % kAnnotationColorVariants];
  if (index >= table.size()) return neutral;
  return table[index];
}

Rgb8 annotationTextColor(bool dark) { return dark ? Rgb8{255, 255, 255} : Rgb8{0, 0, 0}; }

std::size_t annotationColorVariant(std::span<const decode::AnnotationRow> rows, std::span<const RowClass> rowClasses,
                                   std::span<const decode::AnnotationClass> classes, std::uint32_t row,
                                   AnnotationRole role) {
  const auto carries = [&](std::uint32_t id) {
    return std::ranges::any_of(rowClasses, [&](const RowClass& pair) {
      return pair.first == id && roleOf(classes, pair.second) == role;
    });
  };
  std::size_t variant = 0;
  for (const decode::AnnotationRow& r : rows) {
    if (r.id == row) return variant;
    if (carries(r.id)) ++variant;
  }
  return 0;
}

}

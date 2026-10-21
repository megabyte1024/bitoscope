/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

#include "channel_colors.h"
#include "color_choice.h"
#include "decoder.h"
#include "decoder_visibility.h"

namespace bitoscope::waveform_view {

inline constexpr double kMinAnnotationTextContrast = 4.5;

inline constexpr int kMinAnnotationFillDistance = 40;

inline constexpr std::size_t kAnnotationColorVariants = 2;

Rgb8 defaultAnnotationColor(decode::AnnotationRole role, bool dark, Rgb8 neutral, std::size_t variant = 0);

Rgb8 annotationTextColor(bool dark);

Rgb8 resolveAnnotationColor(decode::AnnotationRole role, bool dark, Rgb8 neutral,
                            const std::optional<ColorChoice>& override, std::size_t variant = 0);

std::size_t annotationColorVariant(std::span<const decode::AnnotationRow> rows, std::span<const RowClass> rowClasses,
                                   std::span<const decode::AnnotationClass> classes, std::uint32_t row,
                                   decode::AnnotationRole role);

}

/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <cstdint>
#include <expected>
#include <span>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include "decoder.h"
#include "derived_rows.h"
#include "row_source.h"

namespace bitoscope::decode {

std::vector<std::string> derivedRowEnableOptions(const std::vector<RowSource>& sources);

OptionMap withDerivedRowsOff(const std::vector<RowSource>& sources, OptionMap options);

const RowSource* notDerivedSharing(const std::vector<RowSource>& sources, std::string_view option);

using ResolvedRule = std::variant<BitGrid, ClockEdges>;

std::expected<ResolvedRule, std::string> resolveRowRule(const RowSource& source, const OptionMap& options,
                                                        const ChannelMap& channels, uint64_t sample_rate_milli_hz);

std::vector<RowSource> requestedDerivedRows(const std::vector<RowSource>& sources, const OptionMap& options);

std::expected<std::vector<RowSource>, std::string> takeDerivedRows(Decoder& decoder);

std::vector<RowSource> hostDerivedRows(const std::vector<RowSource>& sources);

void orderByBeginAndRow(std::vector<Annotation>& annotations, std::span<const RowSource> rows);

void dropAnnotationsOfRows(std::vector<Annotation>& annotations, std::span<const RowSource> rows);

std::expected<void, std::string> insertDerivedRowsByRow(std::vector<Annotation>& annotations,
                                                        std::span<const RowSource> rows,
                                                        std::span<const AnnotationRow> names,
                                                        const OptionMap& options, const ChannelMap& channels,
                                                        const SignalView& view,
                                                        std::span<const RowSource> leave_out = {});

}

/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

// WHY: Thousands separator is an apostrophe: the comma is the decimal separator in many countries (owner decision).

#include <cstdint>
#include <expected>
#include <string>
#include <string_view>

namespace bitoscope::gui_format {

std::string withThousands(std::uint64_t value);

std::string formatFixed(std::uint64_t value, unsigned decimals);

std::string formatFrequency(std::uint64_t milliHz);

std::string formatFrequencyRounded(std::uint64_t milliHz, unsigned maxDecimals);

std::string formatPercent(std::uint32_t basisPoints);

std::string formatWholePercent(std::uint64_t done, std::uint64_t total);

std::string formatTimePs(std::int64_t picoseconds);

std::string formatIpv4(std::uint32_t address);

std::expected<std::uint32_t, std::string> parseIpv4(std::string_view text);

}

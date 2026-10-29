/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>

namespace bitoscope::capture {

std::optional<uint64_t> cgroupAvailableBytes(const std::filesystem::path& root);

}

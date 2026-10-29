/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <ostream>

namespace bitoscope::cli {

int checkDriver(const std::filesystem::path& driver, std::ostream& out, std::ostream& err,
                std::optional<uint64_t> inactivity_timeout_ms_for_test);

}

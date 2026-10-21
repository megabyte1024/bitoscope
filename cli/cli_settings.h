/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <cstdint>
#include <expected>
#include <map>
#include <string>
#include <vector>

#include "ipc_properties.h"
#include "la_pipe_client.h"
#include "plugin_device.h"

namespace bitoscope::cli {

std::expected<std::vector<ipc::props::PropertyValue>, std::string> readAnalyzerSettings(
    const std::map<std::string, std::string>& settings, const host::pipe::PluginDevice& session);

std::string describeAnalyzerSettings(const std::vector<ipc::props::PropertyValue>& values,
                                     const host::pipe::PluginDevice& session);

std::expected<void, std::string> checkRateCeiling(uint64_t sample_rate_milli_hz, uint32_t channel_mask,
                                                   const host::pipe::LaCapabilities& capabilities);

}

/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <cstddef>
#include <expected>
#include <span>
#include <string>
#include <string_view>

#include "bitoscope_decoder.h"
#include "ipc_byte_stream.h"
#include "ipc_decoder.h"
#include "plugin_session.h"

namespace bitoscope::plugin_c_sdk {

inline constexpr const char* kDecoderDeviceId = "decoders";
inline constexpr const char* kDecoderDeviceName = "protocol decoders";
inline constexpr uint32_t kDecoderFacetMajor = 1;
inline constexpr uint32_t kDecoderFacetMinor = 0;

std::expected<void, std::string> checkPluginVersion(std::string_view version);

std::expected<ipc::decoder::DecoderInfo, std::string> declarationOf(const bitoscope_decoder* decoder, size_t size);

int runDecoderPlugin(std::span<const std::string> args, ipc::ByteSource& in, ipc::ByteSink& out,
                     bitoscope_list_decoders_fn listDecoders, void* user, SessionOptions options = {});

}

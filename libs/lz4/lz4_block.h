/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>
#include <string>

namespace bitoscope::host {

std::expected<size_t, std::string> lz4CompressBound(size_t input_size);

std::expected<size_t, std::string> lz4Compress(std::span<const uint8_t> src,
                                                std::span<uint8_t> dst);

std::expected<void, std::string> lz4Decompress(std::span<const uint8_t> src,
                                                std::span<uint8_t> dst);

}

/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <cstdint>
#include <expected>
#include <string>

#include "bitoscope_decoder_cpp.h"

namespace bitoscope::decoders_lowlevel {

class NativeI2c final : public plugin::ProtocolDecoder {
 public:
  static constexpr uint32_t kRowBytes = 0;
  static constexpr uint32_t kRowAck = 1;
  static constexpr uint32_t kRowMarkers = 2;
  static constexpr uint32_t kRowErrors = 3;
  static constexpr uint32_t kRowBits = 4;

  static constexpr uint32_t kClassAddressWrite = 0;
  static constexpr uint32_t kClassAddressRead = 1;
  static constexpr uint32_t kClassDataWrite = 2;
  static constexpr uint32_t kClassDataRead = 3;
  static constexpr uint32_t kClassAck = 4;
  static constexpr uint32_t kClassNack = 5;
  static constexpr uint32_t kClassStart = 6;
  static constexpr uint32_t kClassStartRepeat = 7;
  static constexpr uint32_t kClassStop = 8;
  static constexpr uint32_t kClassIncompleteByte = 9;
  static constexpr uint32_t kClassBit = 10;

  plugin::DecoderDeclaration declaration() const override;
  std::expected<void, std::string> decode(const plugin::DecodeJob& job, plugin::DecodeInput& input,
                                          plugin::Annotations& out) override;
};

}

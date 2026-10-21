/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <cstdint>
#include <expected>
#include <string>

#include "bitoscope_decoder_cpp.h"

namespace bitoscope::decoders_lowlevel {

class NativeUart final : public plugin::ProtocolDecoder {
 public:
  static constexpr uint32_t kRowRx = 0;
  static constexpr uint32_t kRowRxBits = 1;
  static constexpr uint32_t kRowTx = 2;
  static constexpr uint32_t kRowTxBits = 3;

  static constexpr uint32_t kClassData = 0;
  static constexpr uint32_t kClassStart = 1;
  static constexpr uint32_t kClassStop = 2;
  static constexpr uint32_t kClassFramingError = 3;
  static constexpr uint32_t kClassParityError = 4;
  static constexpr uint32_t kClassFrameCutOff = 5;
  static constexpr uint32_t kClassBit = 6;

  plugin::DecoderDeclaration declaration() const override;
  std::expected<void, std::string> check(const plugin::DecodeJob& job) const override;
  std::expected<void, std::string> decode(const plugin::DecodeJob& job, plugin::DecodeInput& input,
                                          plugin::Annotations& out) override;
};

}

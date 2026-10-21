/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <cstdint>
#include <expected>
#include <string>

#include "bitoscope_decoder_cpp.h"

namespace bitoscope::decoders_lowlevel {

class NativeSpi final : public plugin::ProtocolDecoder {
 public:
  static constexpr uint32_t kRowMosi = 0;
  static constexpr uint32_t kRowMiso = 1;
  static constexpr uint32_t kRowErrors = 3;
  static constexpr uint32_t kRowMosiBits = 4;
  static constexpr uint32_t kRowMisoBits = 5;
  static constexpr uint32_t kRowMosiTransfer = 6;
  static constexpr uint32_t kRowMisoTransfer = 7;

  static constexpr uint32_t kClassWord = 0;
  static constexpr uint32_t kClassIncompleteWord = 2;
  static constexpr uint32_t kClassBit = 3;
  static constexpr uint32_t kClassLineTransfer = 4;
  static constexpr uint64_t kMaxPieceWords = 1000;

  plugin::DecoderDeclaration declaration() const override;
  std::expected<void, std::string> check(const plugin::DecodeJob& job) const override;
  std::expected<void, std::string> decode(const plugin::DecodeJob& job, plugin::DecodeInput& input,
                                          plugin::Annotations& out) override;
};

}

/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <vector>

#include "bitoscope_decoder.h"
#include "bitoscope_decoder_cpp.h"

namespace bitoscope::plugin_c_sdk {

class CDeclaration {
 public:
  CDeclaration(plugin::DecoderDeclaration declaration, bitoscope_decode_fn decode, bitoscope_check_fn check,
               void* user);
  CDeclaration(const CDeclaration&) = delete;
  CDeclaration& operator=(const CDeclaration&) = delete;

  const bitoscope_decoder* get() const { return &decoder_; }

 private:
  plugin::DecoderDeclaration declaration_;
  std::vector<bitoscope_decoder_channel> channels_;
  std::vector<std::vector<const char*>> optionTexts_;
  std::vector<bitoscope_decoder_option> options_;
  std::vector<const char*> unsupportedOptions_;
  std::vector<const char*> protocols_;
  std::vector<bitoscope_annotation_class> classes_;
  std::vector<bitoscope_annotation_row> rows_;
  std::vector<bitoscope_decoder_output> outputs_;
  std::vector<bitoscope_decoder_input> inputs_;
  std::vector<bitoscope_row_source> rowSources_;
  bitoscope_decoder decoder_{};
};

}

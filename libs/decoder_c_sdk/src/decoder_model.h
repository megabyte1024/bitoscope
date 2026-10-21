/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <string>
#include <vector>

#include "bitoscope_decoder.h"
#include "ipc_decoder.h"

namespace bitoscope::plugin_c_sdk {

struct DecoderRegistration {
  ipc::decoder::DecoderInfo info;
  bitoscope_decode_fn decode = nullptr;
  bitoscope_check_fn check = nullptr;
  void* user = nullptr;
};

struct DecoderFacetRegistration {
  std::vector<DecoderRegistration> decoders;
};

}

struct bitoscope_decoders {
  std::vector<bitoscope::plugin_c_sdk::DecoderRegistration> decoders;
  std::string pluginVersion;
  bool runsScripts = false;
  std::vector<std::string> refusals;
};

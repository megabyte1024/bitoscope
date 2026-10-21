/*
 * SPDX-License-Identifier: MIT
 */

#include "bitoscope_decoder_cpp.h"
#include "lowlevel_decoders.h"

int main(int argc, char** argv) {
  return bitoscope::plugin::runDecoderPlugin(argc, argv, bitoscope::decoders_lowlevel::addLowLevelDecoders);
}

/*
 * SPDX-License-Identifier: MIT
 */

#include "lowlevel_decoders.h"

#include "native_i2c.h"
#include "native_spi.h"
#include "native_uart.h"

namespace bitoscope::decoders_lowlevel {

namespace {

constexpr std::string_view kPluginVersion = "0.2.0";

}

std::unique_ptr<plugin::ProtocolDecoder> makeLowLevelDecoder(std::string_view name) {
  if (name == "uart") return std::make_unique<NativeUart>();
  if (name == "spi") return std::make_unique<NativeSpi>();
  if (name == "i2c") return std::make_unique<NativeI2c>();
  return nullptr;
}

std::expected<void, std::string> addLowLevelDecoders(plugin::Decoders& decoders) {
  decoders.setPluginVersion(kPluginVersion);
  decoders.setRunsScripts(false);
  for (const char* name : {"uart", "spi", "i2c"}) {
    if (!decoders.add(makeLowLevelDecoder(name))) {
      return std::unexpected(std::string("SDK refused decoder '") + name + "'");
    }
  }
  return {};
}

}

/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <cstdint>
#include <expected>
#include <filesystem>
#include <memory>
#include <span>
#include <string>
#include <vector>

#include "decode_placement.h"
#include "decoder_plugins.h"
#include "decoder_registry.h"
#include "decoder_runner.h"

namespace bitoscope::gui_decode {

struct DecoderLogLine {
  enum class Kind { Info, Warning, Error };
  Kind kind = Kind::Info;
  std::string text;
};

class GuiDecoders {
 public:
  static GuiDecoders start(const std::filesystem::path& directory, std::span<const std::string> names,
                           std::expected<DecodePlacement, std::string> placement);
  static GuiDecoders startBesideGui(const std::filesystem::path& gui_directory);

  const bitoscope::decode::DecoderRegistry& registry() const { return plugins_.registry; }
  const std::vector<std::shared_ptr<decoder_host::DecoderPluginPool>>& pools() const { return plugins_.pools; }
  const DecodePlacement& placement() const { return placement_; }
  DecodeThreadSetup threadSetup() const;
  std::vector<DecoderLogLine> logLines() const;

 private:
  GuiDecoders() = default;

  std::filesystem::path directory_;
  DecodePlacement placement_;
  std::string placement_problem_;
  DecoderPlugins plugins_;
};

}

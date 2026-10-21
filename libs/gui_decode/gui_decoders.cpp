/*
 * SPDX-License-Identifier: MIT
 */

#include "gui_decoders.h"

#include <utility>

#include "cli_decoder_plugins.h"

namespace bitoscope::gui_decode {

namespace {

std::string pathText(const std::filesystem::path& path) {
  const std::u8string text = path.u8string();
  return std::string(reinterpret_cast<const char*>(text.data()), text.size());
}

std::string joined(const std::vector<std::string>& names) {
  std::string out;
  for (const std::string& name : names) {
    if (!out.empty()) out += ", ";
    out += name;
  }
  return out;
}

}

GuiDecoders GuiDecoders::start(const std::filesystem::path& directory, std::span<const std::string> names,
                               std::expected<DecodePlacement, std::string> placement) {
  GuiDecoders decoders;
  decoders.directory_ = directory;
  if (placement) {
    decoders.placement_ = std::move(*placement);
  } else {
    decoders.placement_problem_ = std::move(placement.error());
  }
  decoders.plugins_ = startDecoderPlugins(directory, names, decoders.placement_);
  return decoders;
}

GuiDecoders GuiDecoders::startBesideGui(const std::filesystem::path& gui_directory) {
  const std::vector<std::string> names = cli::defaultDecoderPlugins();
  return start(gui_directory / "decoders", names, decodePlacementOfThisMachine());
}

DecodeThreadSetup GuiDecoders::threadSetup() const { return decodeThreadSetup(placement_); }

std::vector<DecoderLogLine> GuiDecoders::logLines() const {
  std::vector<DecoderLogLine> lines;
  if (!placement_problem_.empty()) {
    lines.push_back({DecoderLogLine::Kind::Warning, "decoding is not placed on processors: " + placement_problem_});
  }
  if (!placement_.note.empty()) lines.push_back({DecoderLogLine::Kind::Warning, placement_.note});
  const std::vector<std::string> names = plugins_.registry.names();
  const DecoderLogLine::Kind problem = names.empty() ? DecoderLogLine::Kind::Error : DecoderLogLine::Kind::Warning;
  for (const std::string& text : plugins_.problems) lines.push_back({problem, text});
  if (!names.empty()) {
    lines.push_back({DecoderLogLine::Kind::Info, "Decoders in " + pathText(directory_) + ": " + joined(names)});
  }
  return lines;
}

}

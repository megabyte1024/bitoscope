/*
 * SPDX-License-Identifier: MIT
 */

#include "assets.h"

#include <format>
#include <fstream>
#include <iterator>
#include <new>

namespace bitoscope::gui {

namespace {

template <typename Container>
std::optional<std::string> readWholeFile(const std::filesystem::path& path, Container& out) {
  std::ifstream in(path, std::ios::binary);
  if (!in) {
    return std::format("cannot open {}", path.string());
  }
  try {
    out.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
  } catch (const std::bad_alloc&) {
    return std::format("out of memory reading {}", path.string());
  }
  if (in.bad()) {
    return std::format("cannot read {}", path.string());
  }
  return std::nullopt;
}

bool looksLikeFont(const std::vector<std::uint8_t>& bytes) {
  if (bytes.size() < 12) {
    return false;
  }
  const auto tagIs = [&](std::uint8_t a, std::uint8_t b, std::uint8_t c, std::uint8_t d) {
    return bytes[0] == a && bytes[1] == b && bytes[2] == c && bytes[3] == d;
  };
  return tagIs(0x00, 0x01, 0x00, 0x00) || tagIs('t', 'r', 'u', 'e') || tagIs('O', 'T', 'T', 'O') ||
         tagIs('t', 't', 'c', 'f');
}

}

std::string_view iconFileName(Icon icon) {
  switch (icon) {
  case Icon::About:
    return "about.svg";
  case Icon::Cursors:
    return "cursors.svg";
  case Icon::Decoders:
    return "decoders.svg";
  case Icon::Device:
    return "device.svg";
  case Icon::Log:
    return "log.svg";
  case Icon::Open:
    return "open.svg";
  case Icon::Pin:
    return "pin.svg";
  case Icon::Properties:
    return "properties.svg";
  case Icon::Save:
    return "save.svg";
  case Icon::Signal:
    return "signal.svg";
  case Icon::Start:
    return "start.svg";
  case Icon::Stop:
    return "stop.svg";
  case Icon::Theme:
    return "theme.svg";
  case Icon::TriggerRising:
    return "trigger_rising.svg";
  case Icon::TriggerHigh:
    return "trigger_high.svg";
  case Icon::TriggerFalling:
    return "trigger_falling.svg";
  case Icon::TriggerLow:
    return "trigger_low.svg";
  case Icon::TriggerEdge:
    return "trigger_edge.svg";
  case Icon::Zoom1to1:
    return "zoom_1to1.svg";
  case Icon::ZoomFit:
    return "zoom_fit.svg";
  case Icon::ZoomIn:
    return "zoom_in.svg";
  case Icon::ZoomOut:
    return "zoom_out.svg";
  }
  return {};
}

IconAssets loadIcons(const std::filesystem::path& assetDir, std::uint32_t iconPixelSize,
                    svg_raster::Rgba8 iconColor) {
  IconAssets assets;
  assets.pixelSize = iconPixelSize;
  assets.color = iconColor;

  for (std::size_t i = 0; i < kIconCount; ++i) {
    const auto icon = static_cast<Icon>(i);
    const std::filesystem::path path = assetDir / "icons" / iconFileName(icon);
    std::string& svg = assets.svg[i];
    if (auto problem = readWholeFile(path, svg)) {
      assets.problems.push_back(std::format("Icon not loaded: {}", *problem));
      svg.clear();
      continue;
    }
    auto image = svg_raster::rasterize(svg, iconPixelSize, iconColor);
    if (!image) {
      assets.problems.push_back(std::format("Icon not drawn: {}: {}", path.string(),
                                            svg_raster::describe(image.error())));
      svg.clear();
      continue;
    }
    assets.images[i] = std::move(*image);
  }
  return assets;
}

FontAsset loadFontFile(const std::filesystem::path& fontPath, std::string_view label) {
  FontAsset asset;
  if (auto problem = readWholeFile(fontPath, asset.bytes)) {
    asset.problem = std::format("Font not loaded: {}", *problem);
    asset.bytes.clear();
    return asset;
  }
  if (!looksLikeFont(asset.bytes)) {
    asset.problem =
      std::format("Font not loaded: {} is not TrueType or OpenType font", fontPath.string());
    asset.bytes.clear();
    return asset;
  }
  asset.label = std::string(label);
  return asset;
}

FontAsset loadFont(const std::filesystem::path& assetDir) {
  const std::filesystem::path fontPath = (assetDir / kFontFile).make_preferred();
  return loadFontFile(fontPath, "Google Sans Flex Text");
}

}

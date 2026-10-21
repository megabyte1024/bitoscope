/*
 * SPDX-License-Identifier: MIT
 */

#include "window_icon.h"

#include <GLFW/glfw3.h>

#include <app_icon.h>

#include <cstdint>
#include <format>
#include <fstream>
#include <iterator>
#include <optional>

namespace bitoscope::gui::linux_platform {

namespace {

std::optional<std::string> readFile(const std::filesystem::path& path, std::vector<std::string>& problems) {
  std::ifstream in(path, std::ios::binary);
  if (!in) {
    problems.push_back(std::format("Application icon not loaded: cannot open {}", path.string()));
    return std::nullopt;
  }
  std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  if (in.bad()) {
    problems.push_back(std::format("Application icon not loaded: cannot read {}", path.string()));
    return std::nullopt;
  }
  return text;
}

}

std::vector<std::string> setX11WindowIcon(GLFWwindow* window, const std::filesystem::path& assetDir) {
  std::vector<std::string> problems;
  gui_icons::AppIconSvgs svgs;
  svgs.full = readFile(assetDir / "app_icon.svg", problems);
  svgs.small = readFile(assetDir / "app_icon_small.svg", problems);
  gui_icons::AppIconSet set = gui_icons::rasterizeAppIcons(svgs, gui_icons::kWindowIconSizes);
  problems.insert(problems.end(), set.problems.begin(), set.problems.end());
  if (set.images.empty()) {
    return problems;
  }
  std::vector<GLFWimage> images;
  images.reserve(set.images.size());
  for (svg_raster::RgbaImage& image : set.images) {
    // WHY: GLFW copies the pixels before glfwSetWindowIcon returns.
    images.push_back(GLFWimage{static_cast<int>(image.width), static_cast<int>(image.height),
                               image.pixels.data()});
  }
  glfwSetWindowIcon(window, static_cast<int>(images.size()), images.data());
  return problems;
}

}

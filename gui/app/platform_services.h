/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <imgui.h>

#include <preferences.h>

#include <cstdint>
#include <expected>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "waveform_renderer.h"

namespace bitoscope::gui {

class PlatformServices {
 public:
  virtual ~PlatformServices() = default;

  virtual bool systemUsesDarkTheme() = 0;

  virtual std::vector<std::string> preferredUiLanguages() = 0;

  virtual std::vector<std::string> serialPorts() = 0;

  virtual void setWindowFrameDark(bool dark) = 0;

  virtual std::filesystem::path executableDirectory() = 0;

  virtual std::filesystem::path configDirectory() = 0;

  virtual std::filesystem::path preferencesFile() {
    const std::filesystem::path dir = configDirectory();
    return dir.empty() ? dir : dir / "preferences.json";
  }

  virtual std::optional<gui_prefs::WindowPlacement> windowPlacement() = 0;

  virtual gui_prefs::WindowSystem windowSystem() = 0;

  virtual void requestClose() = 0;

  virtual void setFullScreen(bool fullScreen) = 0;

  virtual bool isFullScreen() = 0;

  virtual std::expected<ImTextureID, std::string> createTexture(
    std::uint32_t width, std::uint32_t height, std::span<const std::uint8_t> rgba) = 0;

  virtual void destroyTexture(ImTextureID texture) = 0;

  virtual WaveformRenderer* waveformRenderer() = 0;

  virtual std::optional<std::string> environmentVariable(std::string_view name) = 0;

  virtual std::optional<std::filesystem::path> systemFontFile() = 0;

  struct FileDialog {
    std::string title;
    std::string fileTypeName;
    std::string allFilesName;
    std::filesystem::path folder;
    std::string fileName;
  };

  // WHY: Modal dialog: call between frames, never inside one; the Linux layer draws drawFrame() frames while it waits.
  virtual std::expected<std::optional<std::filesystem::path>, std::string> askSavePath(const FileDialog& dialog) {
    (void)dialog;
    return std::unexpected(std::string("this platform has no file dialog yet"));
  }

  virtual bool saveNeedsReplaceQuestion(const std::filesystem::path& path) {
    (void)path;
    return false;
  }

  virtual std::expected<std::optional<std::filesystem::path>, std::string> askOpenPath(const FileDialog& dialog) {
    (void)dialog;
    return std::unexpected(std::string("this platform has no file dialog yet"));
  }
};

}

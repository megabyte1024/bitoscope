/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <windows.h>

#include <d3d11.h>

#include <window_placement.h>

#ifdef BITOSCOPE_GUI_TEST_HOOK
#include <dialog_answers.h>
#endif

#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "d3d11_waveform_renderer.h"
#include "platform_services.h"

namespace bitoscope::gui::win32 {

std::filesystem::path roamingConfigDirectory();

std::optional<std::filesystem::path> systemUiFontFile();

std::vector<gui_prefs::ScreenRect> monitorWorkAreas();

class Win32PlatformServices final : public PlatformServices {
 public:
  Win32PlatformServices(HWND hwnd, ID3D11Device* device, ID3D11DeviceContext* context,
                        const RECT* normalRect, bool* fullScreen);

  bool systemUsesDarkTheme() override;

  std::vector<std::string> preferredUiLanguages() override;

  std::vector<std::string> serialPorts() override;

  void setWindowFrameDark(bool dark) override;

  std::filesystem::path executableDirectory() override;

  std::filesystem::path configDirectory() override;

  std::optional<gui_prefs::WindowPlacement> windowPlacement() override;

  gui_prefs::WindowSystem windowSystem() override { return gui_prefs::WindowSystem::Win32; }

  void requestClose() override;

  void setFullScreen(bool fullScreen) override;
  bool isFullScreen() override;

  std::expected<ImTextureID, std::string> createTexture(
    std::uint32_t width, std::uint32_t height, std::span<const std::uint8_t> rgba) override;
  void destroyTexture(ImTextureID texture) override;

  WaveformRenderer* waveformRenderer() override;

  std::optional<std::string> environmentVariable(std::string_view name) override;

  std::optional<std::filesystem::path> systemFontFile() override;

  std::expected<std::optional<std::filesystem::path>, std::string> askSavePath(const FileDialog& dialog) override;
  std::expected<std::optional<std::filesystem::path>, std::string> askOpenPath(const FileDialog& dialog) override;

  void setDialogVeil(std::function<void()> veil);

  std::filesystem::path preferencesFile() override;

#ifdef BITOSCOPE_GUI_TEST_HOOK
  void setPreferencesFile(std::filesystem::path file);
  void setTestDialogAnswers(bitoscope::gui_test_input::DialogAnswers* answers,
                            std::function<void(std::string_view)> log);
#endif

 private:
  HWND hwnd_;
  ID3D11Device* device_;
  const RECT* normalRect_;
  bool* fullScreen_;
  D3d11WaveformRenderer waveform_;
  std::function<void()> dialogVeil_;
  WINDOWPLACEMENT savedPlacement_{};
  LONG_PTR savedStyle_ = 0;
#ifdef BITOSCOPE_GUI_TEST_HOOK
  std::optional<std::filesystem::path> preferencesFileOverride_;
  bitoscope::gui_test_input::DialogAnswers* testDialogAnswers_ = nullptr;
  std::function<void(std::string_view)> testDialogLog_;
#endif
};

}

/*
 * SPDX-License-Identifier: MIT
 */

// WHY: Namespace is linux_platform, not linux: GCC predefines the macro linux with GNU extensions, making it 1.
#pragma once

#include <imgui.h>

#include <portal_file_chooser.h>
#include <window_placement.h>

#ifdef BITOSCOPE_GUI_TEST_HOOK
#include <dialog_answers.h>
#endif

#include <filesystem>
#include <functional>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "color_scheme_watcher.h"
#include "platform_services.h"
#include "vulkan_device.h"

struct GLFWwindow;

namespace bitoscope::gui::linux_platform {

std::filesystem::path configDirectoryFromEnvironment();

std::vector<gui_prefs::ScreenRect> monitorWorkAreas();

bool onWayland();

struct TrackedRect {
  int32_t x = 0;
  int32_t y = 0;
  int32_t width = 0;
  int32_t height = 0;
};

struct TrackedPlacement {
  TrackedRect normal;
  bool maximized = false;
  bool fullScreen = false;
};

class LinuxPlatformServices final : public PlatformServices {
 public:
  LinuxPlatformServices(GLFWwindow* window, VulkanDevice& vulkan, TrackedPlacement* placement,
                        ColorSchemeWatcher& colorScheme, WaveformRenderer* waveform);

  bool systemUsesDarkTheme() override;

  std::vector<std::string> preferredUiLanguages() override;

  std::vector<std::string> serialPorts() override;

  void setWindowFrameDark(bool dark) override;

  std::filesystem::path executableDirectory() override;

  std::filesystem::path configDirectory() override;

  std::filesystem::path preferencesFile() override;

  std::optional<gui_prefs::WindowPlacement> windowPlacement() override;

  gui_prefs::WindowSystem windowSystem() override {
    return onWayland() ? gui_prefs::WindowSystem::Wayland : gui_prefs::WindowSystem::X11;
  }

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

  void setDialogPump(std::function<bool()> pump);

  bool saveNeedsReplaceQuestion(const std::filesystem::path& path) override;

  std::vector<std::string> takeNotes();

#ifdef BITOSCOPE_GUI_TEST_HOOK
  void setPreferencesFile(std::filesystem::path file);
  void setTestDialogAnswers(bitoscope::gui_test_input::DialogAnswers* answers,
                            std::function<void(std::string_view)> log);
#endif

 private:
  struct Texture {
    VkImage image = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkImageView view = VK_NULL_HANDLE;
  };

  void note(std::string line);
  std::expected<std::optional<std::filesystem::path>, std::string> askFile(bool save, const FileDialog& dialog);

  GLFWwindow* window_;
  VulkanDevice& vulkan_;
  TrackedPlacement* placement_;
  ColorSchemeWatcher& colorScheme_;
  WaveformRenderer* waveform_;
  std::function<bool()> dialogPump_;
  gui_linux::SaveAnswer lastSave_;
  std::map<ImTextureID, Texture> textures_;
  std::mutex notesMutex_;
  std::vector<std::string> notes_;
#ifdef BITOSCOPE_GUI_TEST_HOOK
  std::optional<std::filesystem::path> preferencesFileOverride_;
  bitoscope::gui_test_input::DialogAnswers* testDialogAnswers_ = nullptr;
  std::function<void(std::string_view)> testDialogLog_;
#endif
};

}

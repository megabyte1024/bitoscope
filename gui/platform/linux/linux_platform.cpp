/*
 * SPDX-License-Identifier: MIT
 */

#include "linux_platform.h"

// WHY: Include vulkan.h (through vulkan_device.h) BEFORE glfw3.h, so glfw3.h declares its Vulkan functions.
#include <GLFW/glfw3.h>

#include <imgui_impl_vulkan.h>

#include <color_scheme.h>
#include <locale_languages.h>
#include <portal_file_chooser.h>
#include <serial_ports.h>
#include <system_font.h>
#include <xdg_config.h>

#include "file_chooser_dialog.h"
#include "fontconfig_lookup.h"
#include "portal_settings.h"
#include "x11_theme_variant.h"

#include <pwd.h>
#include <unistd.h>

#include <cstdlib>
#include <cstring>
#include <expected>
#include <format>
#include <fstream>
#include <sstream>
#include <system_error>

namespace bitoscope::gui::linux_platform {

namespace {

std::optional<std::string> variable(const char* name) {
  const char* value = std::getenv(name);
  if (value == nullptr || *value == '\0') {
    return std::nullopt;
  }
  return std::string(value);
}

std::optional<std::string> passwordDatabaseHome() {
  long size = sysconf(_SC_GETPW_R_SIZE_MAX);
  std::vector<char> buffer(size > 0 ? static_cast<std::size_t>(size) : 16384);
  passwd entry{};
  passwd* found = nullptr;
  if (getpwuid_r(getuid(), &entry, buffer.data(), buffer.size(), &found) != 0 || found == nullptr ||
      found->pw_dir == nullptr || *found->pw_dir == '\0') {
    return std::nullopt;
  }
  return std::string(found->pw_dir);
}

std::optional<std::string> readSmallTextFile(const std::filesystem::path& path) {
  std::ifstream in(path);
  if (!in) {
    return std::nullopt;
  }
  std::ostringstream text;
  text << in.rdbuf();
  return text.str();
}

}

namespace {

struct XdgValues {
  std::optional<std::string> xdgConfigHome;
  std::optional<std::string> home;
  std::optional<std::string_view> xdg() const {
    return xdgConfigHome ? std::optional<std::string_view>(*xdgConfigHome) : std::nullopt;
  }
  std::optional<std::string_view> homeDir() const {
    return home ? std::optional<std::string_view>(*home) : std::nullopt;
  }
};

XdgValues xdgValues() {
  XdgValues values{variable("XDG_CONFIG_HOME"), variable("HOME")};
  if (!values.home) {
    values.home = passwordDatabaseHome();
  }
  return values;
}

GLFWmonitor* monitorOf(GLFWwindow* window) {
  GLFWmonitor* primary = glfwGetPrimaryMonitor();
  if (glfwGetPlatform() != GLFW_PLATFORM_X11) {
    return primary;
  }
  int x = 0;
  int y = 0;
  int width = 0;
  int height = 0;
  glfwGetWindowPos(window, &x, &y);
  glfwGetWindowSize(window, &width, &height);
  const int centreX = x + width / 2;
  const int centreY = y + height / 2;
  int count = 0;
  GLFWmonitor** monitors = glfwGetMonitors(&count);
  for (int i = 0; i < count; ++i) {
    int mx = 0;
    int my = 0;
    glfwGetMonitorPos(monitors[i], &mx, &my);
    const GLFWvidmode* mode = glfwGetVideoMode(monitors[i]);
    if (mode != nullptr && centreX >= mx && centreX < mx + mode->width && centreY >= my &&
        centreY < my + mode->height) {
      return monitors[i];
    }
  }
  return primary;
}

}

std::filesystem::path configDirectoryFromEnvironment() {
  const XdgValues values = xdgValues();
  return std::filesystem::path(gui_linux::configDirectory(values.xdg(), values.homeDir()));
}

std::vector<gui_prefs::ScreenRect> monitorWorkAreas() {
  std::vector<gui_prefs::ScreenRect> areas;
  int count = 0;
  GLFWmonitor** monitors = glfwGetMonitors(&count);
  for (int i = 0; i < count; ++i) {
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
    glfwGetMonitorWorkarea(monitors[i], &x, &y, &width, &height);
    if (width > 0 && height > 0) {
      areas.push_back({x, y, x + width, y + height});
    }
  }
  return areas;
}

bool onWayland() {
  return glfwGetPlatform() == GLFW_PLATFORM_WAYLAND;
}

LinuxPlatformServices::LinuxPlatformServices(GLFWwindow* window, VulkanDevice& vulkan,
                                             TrackedPlacement* placement,
                                             ColorSchemeWatcher& colorScheme,
                                             WaveformRenderer* waveform)
    : window_(window),
      vulkan_(vulkan),
      placement_(placement),
      colorScheme_(colorScheme),
      waveform_(waveform) {}

bool LinuxPlatformServices::systemUsesDarkTheme() {
  return gui_linux::prefersDark(colorScheme_.current());
}

std::vector<std::string> LinuxPlatformServices::preferredUiLanguages() {
  gui_linux::LocaleEnvironment environment;
  const auto raw = [](const char* name) -> std::optional<std::string> {
    const char* value = std::getenv(name);
    return value != nullptr ? std::optional<std::string>(value) : std::nullopt;
  };
  environment.language = raw("LANGUAGE");
  environment.lcAll = raw("LC_ALL");
  environment.lcMessages = raw("LC_MESSAGES");
  environment.lang = raw("LANG");
  return gui_linux::preferredLanguageTags(environment);
}

std::vector<std::string> LinuxPlatformServices::serialPorts() {
  const std::filesystem::path classDir = "/sys/class/tty";
  std::vector<gui_linux::TtyEntry> entries;
  std::error_code error;
  for (std::filesystem::directory_iterator it(classDir, error), end; !error && it != end;
       it.increment(error)) {
    gui_linux::TtyEntry entry;
    entry.name = it->path().filename().string();
    std::error_code ignored;
    entry.hasDevice = std::filesystem::exists(it->path() / "device", ignored);
    if (std::filesystem::exists(it->path() / "type", ignored)) {
      entry.uartType = readSmallTextFile(it->path() / "type");
    }
    entry.deviceNodeExists = std::filesystem::exists(std::filesystem::path("/dev") / entry.name, ignored);
    entries.push_back(std::move(entry));
  }
  return gui_linux::serialPortsFromSysfs(entries);
}

void LinuxPlatformServices::setWindowFrameDark(bool dark) {
  if (glfwGetPlatform() == GLFW_PLATFORM_X11) {
    setX11ThemeVariant(window_, dark);
  }
}

std::filesystem::path LinuxPlatformServices::executableDirectory() {
  std::error_code error;
  const std::filesystem::path exe = std::filesystem::read_symlink("/proc/self/exe", error);
  if (error || exe.empty()) {
    return std::filesystem::current_path(error);
  }
  return exe.parent_path();
}

std::filesystem::path LinuxPlatformServices::configDirectory() {
  return configDirectoryFromEnvironment();
}

std::optional<gui_prefs::WindowPlacement> LinuxPlatformServices::windowPlacement() {
  const TrackedRect& normal = placement_->normal;
  if (normal.width <= 0 || normal.height <= 0) {
    return std::nullopt;
  }
  gui_prefs::WindowPlacement placement;
  placement.width = normal.width;
  placement.height = normal.height;
  placement.maximized = placement_->maximized;
  // WHY: Wayland never reports a position: omit it instead of saving the compositor's start position or (0, 0).
  if (!onWayland()) {
    placement.position = gui_prefs::WindowPosition{normal.x, normal.y};
  }
  return placement;
}

void LinuxPlatformServices::requestClose() {
  glfwSetWindowShouldClose(window_, GLFW_TRUE);
}

void LinuxPlatformServices::setFullScreen(bool fullScreen) {
  if (fullScreen == placement_->fullScreen) {
    return;
  }
  if (fullScreen) {
    GLFWmonitor* monitor = monitorOf(window_);
    const GLFWvidmode* mode = monitor != nullptr ? glfwGetVideoMode(monitor) : nullptr;
    if (mode == nullptr) {
      return;
    }
    // WHY: Set fullScreen first, so the size and maximize callbacks the switch causes leave the saved placement alone.
    placement_->fullScreen = true;
    glfwSetWindowMonitor(window_, monitor, 0, 0, mode->width, mode->height, mode->refreshRate);
  } else {
    const TrackedRect& normal = placement_->normal;
    glfwSetWindowMonitor(window_, nullptr, normal.x, normal.y, normal.width, normal.height,
                         GLFW_DONT_CARE);
    if (placement_->maximized) {
      glfwMaximizeWindow(window_);
    }
    placement_->fullScreen = false;
  }
}

bool LinuxPlatformServices::isFullScreen() {
  return placement_->fullScreen;
}

std::expected<ImTextureID, std::string> LinuxPlatformServices::createTexture(
  std::uint32_t width, std::uint32_t height, std::span<const std::uint8_t> rgba) {
  if (width == 0 || height == 0 || rgba.size() != static_cast<std::size_t>(width) * height * 4) {
    return std::unexpected(std::format("bad texture size {} x {} for {} bytes", width, height,
                                       rgba.size()));
  }
  VkDevice device = vulkan_.device();
  Texture texture;
  VkBuffer staging = VK_NULL_HANDLE;
  VkDeviceMemory stagingMemory = VK_NULL_HANDLE;
  const auto destroyStaging = [&] {
    if (staging != VK_NULL_HANDLE) {
      vkDestroyBuffer(device, staging, nullptr);
    }
    if (stagingMemory != VK_NULL_HANDLE) {
      vkFreeMemory(device, stagingMemory, nullptr);
    }
  };
  const auto fail = [&](std::string why) -> std::expected<ImTextureID, std::string> {
    destroyStaging();
    if (texture.view != VK_NULL_HANDLE) {
      vkDestroyImageView(device, texture.view, nullptr);
    }
    if (texture.image != VK_NULL_HANDLE) {
      vkDestroyImage(device, texture.image, nullptr);
    }
    if (texture.memory != VK_NULL_HANDLE) {
      vkFreeMemory(device, texture.memory, nullptr);
    }
    return std::unexpected(std::move(why));
  };
  const auto failed = [](const char* what, VkResult r) {
    return std::format("{} failed (VkResult {})", what, static_cast<int>(r));
  };

  VkImageCreateInfo imageInfo{};
  imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
  imageInfo.imageType = VK_IMAGE_TYPE_2D;
  imageInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
  imageInfo.extent = {width, height, 1};
  imageInfo.mipLevels = 1;
  imageInfo.arrayLayers = 1;
  imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
  imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
  imageInfo.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
  imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
  imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  if (const VkResult r = vkCreateImage(device, &imageInfo, nullptr, &texture.image); r != VK_SUCCESS) {
    return fail(failed("vkCreateImage", r));
  }
  VkMemoryRequirements imageNeeds;
  vkGetImageMemoryRequirements(device, texture.image, &imageNeeds);
  const std::optional<std::uint32_t> imageType =
    vulkan_.memoryType(imageNeeds.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
  if (!imageType) {
    return fail("no device-local memory type for texture");
  }
  VkMemoryAllocateInfo imageAlloc{};
  imageAlloc.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
  imageAlloc.allocationSize = imageNeeds.size;
  imageAlloc.memoryTypeIndex = *imageType;
  if (const VkResult r = vkAllocateMemory(device, &imageAlloc, nullptr, &texture.memory); r != VK_SUCCESS) {
    return fail(failed("vkAllocateMemory (texture)", r));
  }
  vkBindImageMemory(device, texture.image, texture.memory, 0);

  VkBufferCreateInfo bufferInfo{};
  bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
  bufferInfo.size = rgba.size();
  bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
  bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
  if (const VkResult r = vkCreateBuffer(device, &bufferInfo, nullptr, &staging); r != VK_SUCCESS) {
    return fail(failed("vkCreateBuffer (staging)", r));
  }
  VkMemoryRequirements bufferNeeds;
  vkGetBufferMemoryRequirements(device, staging, &bufferNeeds);
  const std::optional<std::uint32_t> bufferType = vulkan_.memoryType(
    bufferNeeds.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
  if (!bufferType) {
    return fail("no host-visible coherent memory type for staging buffer");
  }
  VkMemoryAllocateInfo bufferAlloc{};
  bufferAlloc.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
  bufferAlloc.allocationSize = bufferNeeds.size;
  bufferAlloc.memoryTypeIndex = *bufferType;
  if (const VkResult r = vkAllocateMemory(device, &bufferAlloc, nullptr, &stagingMemory); r != VK_SUCCESS) {
    return fail(failed("vkAllocateMemory (staging)", r));
  }
  vkBindBufferMemory(device, staging, stagingMemory, 0);
  void* mapped = nullptr;
  if (const VkResult r = vkMapMemory(device, stagingMemory, 0, rgba.size(), 0, &mapped); r != VK_SUCCESS) {
    return fail(failed("vkMapMemory", r));
  }
  std::memcpy(mapped, rgba.data(), rgba.size());
  vkUnmapMemory(device, stagingMemory);

  const auto uploaded = vulkan_.submitOnce([&](VkCommandBuffer commands) {
    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = texture.image;
    barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.srcAccessMask = 0;
    barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    vkCmdPipelineBarrier(commands, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0,
                         0, nullptr, 0, nullptr, 1, &barrier);
    VkBufferImageCopy region{};
    region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    region.imageExtent = {width, height, 1};
    vkCmdCopyBufferToImage(commands, staging, texture.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1,
                           &region);
    barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier(commands, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                         0, 0, nullptr, 0, nullptr, 1, &barrier);
  });
  if (!uploaded) {
    return fail(uploaded.error());
  }
  destroyStaging();
  staging = VK_NULL_HANDLE;
  stagingMemory = VK_NULL_HANDLE;

  VkImageViewCreateInfo viewInfo{};
  viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
  viewInfo.image = texture.image;
  viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
  viewInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
  viewInfo.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
  if (const VkResult r = vkCreateImageView(device, &viewInfo, nullptr, &texture.view); r != VK_SUCCESS) {
    return fail(failed("vkCreateImageView", r));
  }
  const VkDescriptorSet set =
    ImGui_ImplVulkan_AddTexture(texture.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
  if (set == VK_NULL_HANDLE) {
    return fail("no descriptor set for texture (ImGui_ImplVulkan_AddTexture)");
  }
  const auto id = static_cast<ImTextureID>(reinterpret_cast<std::uintptr_t>(set));
  textures_.emplace(id, texture);
  return id;
}

void LinuxPlatformServices::destroyTexture(ImTextureID texture) {
  const auto found = textures_.find(texture);
  if (found == textures_.end()) {
    return;
  }
  const Texture resources = found->second;
  textures_.erase(found);
  VkDevice device = vulkan_.device();
  const auto set = reinterpret_cast<VkDescriptorSet>(static_cast<std::uintptr_t>(texture));
  vulkan_.deferRelease([device, set, resources] {
    ImGui_ImplVulkan_RemoveTexture(set);
    vkDestroyImageView(device, resources.view, nullptr);
    vkDestroyImage(device, resources.image, nullptr);
    vkFreeMemory(device, resources.memory, nullptr);
  });
}

WaveformRenderer* LinuxPlatformServices::waveformRenderer() {
  return waveform_;
}

std::optional<std::string> LinuxPlatformServices::environmentVariable(std::string_view name) {
  return variable(std::string(name).c_str());
}

std::optional<std::filesystem::path> LinuxPlatformServices::systemFontFile() {
  const auto portal = readPortalSetting("org.gnome.desktop.interface", "font-name");
  const std::optional<std::string> portalFontName =
    portal && portal->text ? portal->text : std::nullopt;
  std::optional<std::string> kdeglobals;
  const XdgValues values = xdgValues();
  if (const std::string home = gui_linux::configHome(values.xdg(), values.homeDir()); !home.empty()) {
    kdeglobals = readSmallTextFile(std::filesystem::path(home) / "kdeglobals");
  }
  const std::string family = gui_linux::systemFontFamily(
    portalFontName ? std::optional<std::string_view>(*portalFontName) : std::nullopt,
    kdeglobals ? std::optional<std::string_view>(*kdeglobals) : std::nullopt);
  const char* source = portalFontName && gui_linux::pangoFontFamily(*portalFontName)
                         ? "portal's font-name"
                       : kdeglobals && gui_linux::kdeglobalsFontFamily(*kdeglobals)
                         ? "kdeglobals"
                         : "neither portal nor kdeglobals names one";
  const std::expected<FontMatch, std::string> match = matchFontFamily(family);
  if (!match) {
    note(std::format("System font: family \"{}\" ({}), but {}", family, source, match.error()));
    return std::nullopt;
  }
  if (match->index != 0) {
    note(std::format("System font: \"{}\" ({}) is face {} of collection {}; only first face "
                     "can be loaded",
                     family, source, match->index, match->file.string()));
    return std::nullopt;
  }
  note(std::format("System font: family \"{}\" ({}), fontconfig's match \"{}\": {}", family, source,
                   match->family, match->file.string()));
  return match->file;
}

namespace {

// WHY: Held keys and buttons are released BEFORE input is blocked: their release would be dropped and they stay down.
class InputBlock {
 public:
  InputBlock() {
    ImGuiIO& io = ImGui::GetIO();
    io.ClearEventsQueue();
    io.ClearInputKeys();
    io.ClearInputMouse();
    io.SetAppAcceptingEvents(false);
  }
  ~InputBlock() { ImGui::GetIO().SetAppAcceptingEvents(true); }
  InputBlock(const InputBlock&) = delete;
  InputBlock& operator=(const InputBlock&) = delete;
};

}

std::expected<std::optional<std::filesystem::path>, std::string> LinuxPlatformServices::askFile(
  bool save, const FileDialog& dialog) {
#ifdef BITOSCOPE_GUI_TEST_HOOK
  if (testDialogAnswers_ != nullptr) {
    const bitoscope::gui_test_input::DialogDecision decision = testDialogAnswers_->take(
      save ? bitoscope::gui_test_input::DialogKind::Save : bitoscope::gui_test_input::DialogKind::Open);
    if (testDialogLog_) {
      testDialogLog_("test hook: " + decision.note);
    }
    if (save) {
      lastSave_ = gui_linux::SaveAnswer();
    }
    if (!decision.path) {
      return std::optional<std::filesystem::path>();
    }
    if (save) {
      lastSave_ = gui_linux::saveAnswer(*decision.path);
      return std::optional<std::filesystem::path>(std::filesystem::path(lastSave_.path));
    }
    return std::optional<std::filesystem::path>(std::filesystem::path(*decision.path));
  }
#endif
  std::error_code error;
  const std::string folder =
    !dialog.folder.empty() && std::filesystem::is_directory(dialog.folder, error) ? dialog.folder.string()
                                                                                 : std::string();
  const gui_linux::FileChooserTexts texts{.title = dialog.title,
                                          .fileTypeName = dialog.fileTypeName,
                                          .allFilesName = dialog.allFilesName,
                                          .folder = folder,
                                          .fileName = dialog.fileName};
  const std::string parent = gui_linux::parentWindowIdentifier(
    glfwGetPlatform() == GLFW_PLATFORM_X11 ? std::optional<std::uint64_t>(x11WindowId(window_)) : std::nullopt);
  std::vector<std::string> notes;
  std::expected<std::optional<std::filesystem::path>, std::string> chosen;
  {
    const InputBlock block;
    chosen = showFileChooser(save ? gui_linux::FileChooserKind::Save : gui_linux::FileChooserKind::Open, texts,
                             parent, dialogPump_, notes);
  }
  for (std::string& line : notes) {
    note(std::move(line));
  }
  if (save) {
    lastSave_ = gui_linux::SaveAnswer();
    if (chosen && *chosen) {
      lastSave_ = gui_linux::saveAnswer((**chosen).string());
      chosen = std::optional<std::filesystem::path>(std::filesystem::path(lastSave_.path));
    }
  }
  return chosen;
}

bool LinuxPlatformServices::saveNeedsReplaceQuestion(const std::filesystem::path& path) {
  std::error_code error;
  return gui_linux::saveNeedsReplaceQuestion(lastSave_, path.string(), std::filesystem::exists(path, error));
}

std::expected<std::optional<std::filesystem::path>, std::string> LinuxPlatformServices::askSavePath(
  const FileDialog& dialog) {
  return askFile(true, dialog);
}

std::expected<std::optional<std::filesystem::path>, std::string> LinuxPlatformServices::askOpenPath(
  const FileDialog& dialog) {
  return askFile(false, dialog);
}

std::filesystem::path LinuxPlatformServices::preferencesFile() {
#ifdef BITOSCOPE_GUI_TEST_HOOK
  if (preferencesFileOverride_) {
    return *preferencesFileOverride_;
  }
#endif
  return PlatformServices::preferencesFile();
}

#ifdef BITOSCOPE_GUI_TEST_HOOK
void LinuxPlatformServices::setPreferencesFile(std::filesystem::path file) {
  preferencesFileOverride_ = std::move(file);
}

void LinuxPlatformServices::setTestDialogAnswers(bitoscope::gui_test_input::DialogAnswers* answers,
                                                 std::function<void(std::string_view)> log) {
  testDialogAnswers_ = answers;
  testDialogLog_ = std::move(log);
}
#endif

void LinuxPlatformServices::setDialogPump(std::function<bool()> pump) {
  dialogPump_ = std::move(pump);
}

void LinuxPlatformServices::note(std::string line) {
  std::lock_guard lock(notesMutex_);
  notes_.push_back(std::move(line));
}

std::vector<std::string> LinuxPlatformServices::takeNotes() {
  std::lock_guard lock(notesMutex_);
  std::vector<std::string> out;
  out.swap(notes_);
  return out;
}

}

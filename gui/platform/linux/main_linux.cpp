/*
 * SPDX-License-Identifier: MIT
 */

// WHY: Vulkan renderer backend is initialized BEFORE GuiApp::configure(), against gui_app.h's contract:
// configure() makes icon textures, and a Vulkan texture is a descriptor set allocated by that backend.

#include "vulkan_device.h"  // WHY: Include vulkan.h before glfw3.h, so GLFW declares its Vulkan functions.

#include <GLFW/glfw3.h>

#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_vulkan.h>

#include <preferences_file.h>
#include <window_placement.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <format>
#include <fstream>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

#include <cursor_theme.h>
#include <frame_loop.h>
#include <frame_pacer.h>
#include <wake_gate.h>
#include <wayland_output.h>
#include <wayland_restore_size.h>
#include <window_system.h>
#include <xdg_config.h>

#include "color_scheme_watcher.h"
#include "gui_app.h"
#include "linux_platform.h"
#include "persistent_mappings.h"
#include "portal_settings.h"
#include "tr.h"
#include "vulkan_waveform_renderer.h"
#include "window_icon.h"

#ifdef BITOSCOPE_GUI_TEST_HOOK
#include <capture_status.h>
#include <frame_times.h>
#include <frame_times_probe.h>
#include <imgui_bridge.h>

#include "test_hook.h"

#define BITOSCOPE_HOOK_STAMP(hook, phase)                                                               \
  ((hook) != nullptr ? (hook)->frameTimes().endPhase(bitoscope::gui_test_input::FramePhase::phase, steadyNs()) \
                     : void())
#else
#define BITOSCOPE_HOOK_STAMP(hook, phase) ((void)0)
#endif

namespace {

using bitoscope::gui::GuiApp;
using bitoscope::gui::linux_platform::ColorSchemeWatcher;
using bitoscope::gui::linux_platform::LinuxPlatformServices;
using bitoscope::gui::linux_platform::onWayland;
using bitoscope::gui::linux_platform::TrackedPlacement;
using bitoscope::gui::linux_platform::VulkanDevice;
using bitoscope::gui::linux_platform::VulkanWaveformRenderer;

constexpr char kWindowTitle[] = "bitoscope";
constexpr int kInitialWidth = 1280;
constexpr int kInitialHeight = 800;

struct WindowState {
  TrackedPlacement placement;
  bitoscope::gui_linux::WaylandRestoreSize restoreSize;
  float newDpiScale = 0;
  std::atomic<bool> systemThemeChanged{false};
  std::string fatalError;
  std::uint64_t events = 0;
#ifdef BITOSCOPE_GUI_TEST_HOOK
  bitoscope::gui::linux_platform::TestHook* testHook = nullptr;
  bool hookQuit = false;
#endif
};

#ifdef BITOSCOPE_GUI_TEST_HOOK
std::uint64_t steadyNs() {
  return static_cast<std::uint64_t>(
    std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());
}
#endif

std::vector<std::string> gGlfwErrors;

void onGlfwError(int code, const char* description) {
  std::string line = std::format("GLFW error 0x{:05X}: {}", code, description != nullptr ? description : "");
  std::fprintf(stderr, "bitoscope: %s\n", line.c_str());
  gGlfwErrors.push_back(std::move(line));
}

// WHY: Call only when GLFW will choose Wayland: on X11 Xcursor reads these variables ahead of its resources.
// Runs first in main(): setenv() must not race the colour scheme watcher's thread.
void applyWaylandCursorThemeWorkaround() {
  const bool themeSet = std::getenv("XCURSOR_THEME") != nullptr;
  const bool sizeSet = std::getenv("XCURSOR_SIZE") != nullptr;
  if (themeSet && sizeSet) {
    return;
  }

  const char* xdgCurrentDesktopEnv = std::getenv("XDG_CURRENT_DESKTOP");
  const std::optional<std::string_view> xdgCurrentDesktop =
    xdgCurrentDesktopEnv != nullptr ? std::optional<std::string_view>(xdgCurrentDesktopEnv) : std::nullopt;

  std::optional<std::string> kcminputrcText;
  const char* xdgConfigHome = std::getenv("XDG_CONFIG_HOME");
  const char* home = std::getenv("HOME");
  const std::string configHome = bitoscope::gui_linux::configHome(
    xdgConfigHome != nullptr ? std::optional<std::string_view>(xdgConfigHome) : std::nullopt,
    home != nullptr ? std::optional<std::string_view>(home) : std::nullopt);
  if (!configHome.empty()) {
    if (std::ifstream file(std::filesystem::path(configHome) / "kcminputrc", std::ios::binary);
        file) {
      std::ostringstream contents;
      contents << file.rdbuf();
      kcminputrcText = contents.str();
    }
  }
  const std::optional<std::string_view> kcminputrcView =
    kcminputrcText ? std::optional<std::string_view>(*kcminputrcText) : std::nullopt;

  const bool kde = bitoscope::gui_linux::isKdeDesktop(xdgCurrentDesktop);

  bitoscope::gui_linux::DesktopCursor cursor =
    kde ? bitoscope::gui_linux::desktopCursor(xdgCurrentDesktop, kcminputrcView, std::nullopt, std::nullopt)
        : bitoscope::gui_linux::DesktopCursor{};
  if (!cursor.theme) {
    std::optional<std::string> portalTheme;
    std::optional<std::uint32_t> portalSize;
    if (const auto reply = bitoscope::gui::linux_platform::readPortalSetting(
          "org.gnome.desktop.interface", "cursor-theme");
        reply && reply->text) {
      portalTheme = reply->text;
    }
    if (const auto reply = bitoscope::gui::linux_platform::readPortalSetting(
          "org.gnome.desktop.interface", "cursor-size");
        reply && reply->number) {
      portalSize = reply->number;
    }
    cursor = bitoscope::gui_linux::desktopCursor(
      xdgCurrentDesktop, kcminputrcView,
      portalTheme ? std::optional<std::string_view>(*portalTheme) : std::nullopt, portalSize);
  }

  if (!themeSet && cursor.theme) {
    setenv("XCURSOR_THEME", cursor.theme->c_str(), 0);
  }
  if (!sizeSet && cursor.size) {
    setenv("XCURSOR_SIZE", std::to_string(*cursor.size).c_str(), 0);
  }
}

bitoscope::gui_linux::GlfwWindowSystem expectedGlfwWindowSystemFromEnvironment() {
  const auto variable = [](const char* name) -> std::optional<std::string_view> {
    const char* value = std::getenv(name);
    return value != nullptr ? std::optional<std::string_view>(value) : std::nullopt;
  };
  return bitoscope::gui_linux::expectedGlfwWindowSystem({.xdgSessionType = variable("XDG_SESSION_TYPE"),
                                                         .waylandDisplay = variable("WAYLAND_DISPLAY"),
                                                         .display = variable("DISPLAY")});
}

float dpiScaleOf(GLFWwindow* window) {
  if (onWayland()) {
    return 1.0f;
  }
  float x = 1.0f;
  float y = 1.0f;
  glfwGetWindowContentScale(window, &x, &y);
  return x > 0.0f ? x : 1.0f;
}

WindowState& stateOf(GLFWwindow* window) {
  return *static_cast<WindowState*>(glfwGetWindowUserPointer(window));
}

bool isNormal(GLFWwindow* window) {
  return !stateOf(window).placement.fullScreen &&
         glfwGetWindowAttrib(window, GLFW_MAXIMIZED) == GLFW_FALSE &&
         glfwGetWindowAttrib(window, GLFW_ICONIFIED) == GLFW_FALSE;
}

void countEvent(GLFWwindow* window) {
  ++stateOf(window).events;
}

void onWindowPos(GLFWwindow* window, int x, int y) {
  countEvent(window);
  if (isNormal(window)) {
    stateOf(window).placement.normal.x = x;
    stateOf(window).placement.normal.y = y;
  }
}

void onWindowSize(GLFWwindow* window, int width, int height) {
  countEvent(window);
  // WHY: Until the restore resize is applied, the size reported is still the maximized one.
  if (width > 0 && height > 0 && isNormal(window) && !stateOf(window).restoreSize.resizePending()) {
    stateOf(window).placement.normal.width = width;
    stateOf(window).placement.normal.height = height;
  }
}

// WHY: On Wayland, ask for the normal size on restore: a window created maximized has none,
// and KWin kept the maximized size.
void onWindowMaximize(GLFWwindow* window, int maximized) {
  countEvent(window);
  WindowState& state = stateOf(window);
  if (state.placement.fullScreen) {
    return;
  }
  state.placement.maximized = maximized == GLFW_TRUE;
  if (maximized == GLFW_TRUE) {
    state.restoreSize.remember({state.placement.normal.width, state.placement.normal.height});
  } else if (onWayland()) {
    // WHY: Resize in applyRestoreSize(), not here: GLFW applies the configure's own size after this callback.
    state.restoreSize.windowRestored();
  }
}

void applyRestoreSize(GLFWwindow* window, WindowState& state) {
  if (const std::optional<bitoscope::gui_linux::WindowSize> size = state.restoreSize.takeResize()) {
    glfwSetWindowSize(window, size->width, size->height);
  }
}

void onContentScale(GLFWwindow* window, float, float) {
  countEvent(window);
  stateOf(window).newDpiScale = dpiScaleOf(window);
}

// WHY: Install these before Dear ImGui's GLFW backend, which chains the callbacks installed before it.
void installEventCounters(GLFWwindow* window) {
  glfwSetFramebufferSizeCallback(window, [](GLFWwindow* w, int, int) { countEvent(w); });
  glfwSetWindowIconifyCallback(window, [](GLFWwindow* w, int) { countEvent(w); });
  glfwSetWindowRefreshCallback(window, [](GLFWwindow* w) { countEvent(w); });
  glfwSetWindowCloseCallback(window, [](GLFWwindow* w) { countEvent(w); });
  glfwSetWindowFocusCallback(window, [](GLFWwindow* w, int) { countEvent(w); });
  glfwSetCursorEnterCallback(window, [](GLFWwindow* w, int) { countEvent(w); });
  glfwSetCursorPosCallback(window, [](GLFWwindow* w, double, double) { countEvent(w); });
  glfwSetMouseButtonCallback(window, [](GLFWwindow* w, int, int, int) { countEvent(w); });
  glfwSetScrollCallback(window, [](GLFWwindow* w, double, double) { countEvent(w); });
  glfwSetKeyCallback(window, [](GLFWwindow* w, int, int, int, int) { countEvent(w); });
  glfwSetCharCallback(window, [](GLFWwindow* w, unsigned int) { countEvent(w); });
}

std::vector<std::string> gVulkanBackendErrors;

void onVulkanBackendResult(VkResult result) {
  if (result != VK_SUCCESS) {
    gVulkanBackendErrors.push_back(
      std::format("Dear ImGui's Vulkan backend: call failed (VkResult {})", static_cast<int>(result)));
  }
}

void showError(const std::string& text) {
  std::fprintf(stderr, "bitoscope: %s\n", text.c_str());
}

// WHY: On Wayland GLFW reports an output's mode size as work area and an INTEGER scale,
// while windows are sized in logical pixels.
std::optional<bitoscope::gui_prefs::ScreenRect> primaryWorkArea(std::optional<float> windowScale) {
  GLFWmonitor* monitor = glfwGetPrimaryMonitor();
  if (monitor == nullptr) {
    return std::nullopt;
  }
  int x = 0;
  int y = 0;
  int width = 0;
  int height = 0;
  glfwGetMonitorWorkarea(monitor, &x, &y, &width, &height);
  if (onWayland()) {
    float scale = 1.0f;
    float unused = 1.0f;
    glfwGetMonitorContentScale(monitor, &scale, &unused);
    const std::optional<bitoscope::gui_linux::LogicalSize> logical =
      bitoscope::gui_linux::waylandLogicalOutputSize(width, height, scale, windowScale);
    if (!logical) {
      return std::nullopt;
    }
    width = logical->width;
    height = logical->height;
  }
  if (width <= 0 || height <= 0) {
    return std::nullopt;
  }
  return bitoscope::gui_prefs::ScreenRect{x, y, x + width, y + height};
}

void drawAndPresent(GLFWwindow* window, WindowState& state, VulkanDevice& vulkan, GuiApp& app, bool dialogOpen);

void takePlatformNotes(GuiApp& app, ColorSchemeWatcher& colorScheme, LinuxPlatformServices& platform) {
  for (const std::string& line : colorScheme.takeNotes()) {
    app.logger().log(bitoscope::log::LogLevel::info, "gui", line);
  }
  for (const std::string& line : platform.takeNotes()) {
    app.logger().log(bitoscope::log::LogLevel::info, "gui", line);
  }
  for (const std::string& line : gGlfwErrors) {
    app.logger().log(bitoscope::log::LogLevel::warning, "gui", line);
  }
  gGlfwErrors.clear();
  for (const std::string& line : gVulkanBackendErrors) {
    app.logger().log(bitoscope::log::LogLevel::error, "gui", line);
  }
  gVulkanBackendErrors.clear();
}

void renderFrame(GLFWwindow* window, WindowState& state, VulkanDevice& vulkan, GuiApp& app) {
#ifdef BITOSCOPE_GUI_TEST_HOOK
  bitoscope::gui::linux_platform::TestHook* const hook = state.testHook;
  if (hook != nullptr) {
    hook->frameTimes().beginFrame(steadyNs());
  }
#endif
  if (state.newDpiScale != 0) {
    app.setDpiScale(state.newDpiScale);
    state.newDpiScale = 0;
  }
  if (state.systemThemeChanged.exchange(false)) {
    app.systemThemeChanged();
  }
  BITOSCOPE_HOOK_STAMP(hook, AppBegin);
  if (auto waited = vulkan.waitForFrameSlot(); !waited) {
    state.fatalError = "Graphics device was lost (" + waited.error() + "); bitoscope must close.";
    return;
  }
  BITOSCOPE_HOOK_STAMP(hook, SlotWait);
  app.beginFrame();
  BITOSCOPE_HOOK_STAMP(hook, AppBegin);
  if (!state.fatalError.empty()) {
    return;
  }
  drawAndPresent(window, state, vulkan, app, false);
}

// WHY: A whole frame is drawn: a docking layout not submitted for a frame is lost, and old draw data fits old size.
// Never call GuiApp::beginFrame() or pumpBackground() here: this runs inside pumpBackground().
bool pumpDialogFrame(GLFWwindow* window, WindowState& state, VulkanDevice& vulkan, GuiApp& app) {
  glfwPollEvents();
  applyRestoreSize(window, state);
  if (glfwWindowShouldClose(window) || !state.fatalError.empty()) {
    return false;
  }
  int width = 0;
  int height = 0;
  glfwGetFramebufferSize(window, &width, &height);
  if (glfwGetWindowAttrib(window, GLFW_ICONIFIED) == GLFW_TRUE || width == 0 || height == 0) {
    return true;
  }
  if (auto waited = vulkan.waitForFrameSlot(); !waited) {
    state.fatalError = "Graphics device was lost (" + waited.error() + "); bitoscope must close.";
    return false;
  }
  drawAndPresent(window, state, vulkan, app, true);
  if (!state.fatalError.empty()) {
    return false;
  }
  if (auto waited = vulkan.waitForFrameSlot(); !waited) {
    state.fatalError = "Graphics device was lost (" + waited.error() + "); bitoscope must close.";
    return false;
  }
  return true;
}

// WHY: The veil shows no text: the dialog is the system's and says what is going on.
void drawDialogVeil() {
  const ImGuiViewport* viewport = ImGui::GetMainViewport();
  const ImVec2 max(viewport->Pos.x + viewport->Size.x, viewport->Pos.y + viewport->Size.y);
  ImGui::GetForegroundDrawList()->AddRectFilled(viewport->Pos, max, ImGui::GetColorU32(ImGuiCol_ModalWindowDimBg));
}

void drawAndPresent(GLFWwindow* window, WindowState& state, VulkanDevice& vulkan, GuiApp& app, bool dialogOpen) {
  ImGui_ImplVulkan_NewFrame();
  ImGui_ImplGlfw_NewFrame();
#ifdef BITOSCOPE_GUI_TEST_HOOK
  bitoscope::gui::linux_platform::TestHook* const hook = dialogOpen ? nullptr : state.testHook;
  const auto hookNowMs = static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
                                                      std::chrono::steady_clock::now().time_since_epoch())
                                                      .count());
  if (hook != nullptr) {
    hook->injectInput(ImGui::GetIO(), hookNowMs);
  }
#endif
  ImGui::NewFrame();
  BITOSCOPE_HOOK_STAMP(hook, NewFrame);
  app.drawFrame();
  if (dialogOpen) {
    drawDialogVeil();
  }
  BITOSCOPE_HOOK_STAMP(hook, Draw);
  ImGui::Render();
  BITOSCOPE_HOOK_STAMP(hook, Render);

  int width = 0;
  int height = 0;
  glfwGetFramebufferSize(window, &width, &height);
  const auto started = vulkan.beginFrame(static_cast<std::uint32_t>(std::max(width, 0)),
                                         static_cast<std::uint32_t>(std::max(height, 0)),
                                         app.clearColor());
  BITOSCOPE_HOOK_STAMP(hook, Acquire);
  if (!started) {
    state.fatalError = "Graphics device was lost or window's surface is gone (" +
                       started.error() + "); bitoscope must close.";
    return;
  }
  if (*started == VulkanDevice::FrameStart::Recording) {
    ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), vulkan.commandBuffer());
#ifdef BITOSCOPE_GUI_TEST_HOOK
    if (hook != nullptr && hook->wantsScreenshot()) {
      vulkan.requestReadback();
    }
#endif
    BITOSCOPE_HOOK_STAMP(hook, Record);
    if (auto ended = vulkan.endFrame(); !ended) {
      state.fatalError = "Graphics device was lost or window's surface is gone (" +
                         ended.error() + "); bitoscope must close.";
      return;
    }
    BITOSCOPE_HOOK_STAMP(hook, Present);
  }
#ifdef BITOSCOPE_GUI_TEST_HOOK
  if (hook != nullptr) {
    const bool distorted = hook->frameDistorted();
    hook->answerQuery(*ImGui::GetCurrentContext(), [&vulkan] { return vulkan.takeReadback(); });
    hook->finishFrame(hookNowMs);
    hook->frameTimes().endPhase(bitoscope::gui_test_input::FramePhase::Hook, steadyNs());
    hook->frameTimes().commitFrame(static_cast<std::uint64_t>(ImGui::GetFrameCount()), distorted);
    if (hook->quitRequested()) {
      state.hookQuit = true;
    }
    if (hook->takeCloseRequest()) {
      glfwSetWindowShouldClose(window, GLFW_TRUE);
    }
  }
#endif
}

int runFrames(GLFWwindow* window, WindowState& state, VulkanDevice& vulkan, GuiApp& app,
              ColorSchemeWatcher& colorScheme, LinuxPlatformServices& platform) {
  std::uint64_t eventsSeen = state.events;
  bitoscope::gui_frame_pacing::FrameLoopHooks hooks;
  hooks.handleEvents = [window, &state] {
    glfwPollEvents();
    applyRestoreSize(window, state);
  };
  hooks.closeRequested = [window, &state, &app] {
#ifdef BITOSCOPE_GUI_TEST_HOOK
    if (state.hookQuit) {
      return true;
    }
#else
    (void)state;
#endif
    if (glfwWindowShouldClose(window) == GLFW_TRUE) {
      glfwSetWindowShouldClose(window, GLFW_FALSE);  // WHY: Cancel the close request: GuiApp asks about unsaved panels, and the loop ends only once it confirms.
      app.requestAppClose();
    }
    return app.closeConfirmed();
  };
  hooks.observeWindow = [&] {
    bitoscope::gui_frame_pacing::WindowObservation observed;
    // WHY: GLFW's Wayland backend always reports not iconified, so a minimized Wayland window counts as Visible;
    // GLFW has no occlusion query, so occluded stays false.
    observed.iconified = glfwGetWindowAttrib(window, GLFW_ICONIFIED) == GLFW_TRUE;
    glfwGetFramebufferSize(window, &observed.framebufferWidth, &observed.framebufferHeight);
    observed.eventsHandled = state.events != eventsSeen;
    eventsSeen = state.events;
    observed.changePending = state.newDpiScale != 0 || state.systemThemeChanged.load();
#ifdef BITOSCOPE_GUI_TEST_HOOK
    observed.testHookBusy = state.testHook != nullptr && state.testHook->busy();
#endif
    return observed;
  };
  hooks.pumpBackground = [&] {
    takePlatformNotes(app, colorScheme, platform);
    bitoscope::gui_frame_pacing::AppObservation observed;
    observed.backgroundChanged = app.pumpBackground();
    observed.captureRunning = app.captureRunning();
    observed.captureAdvanced = app.captureAdvanced();
    observed.asyncWorkInFlight = app.asyncWorkInFlight();
    observed.uiNeedsMore = app.wantsMoreFrames();
    observed.reduceIdleFrameRate = app.reduceIdleFrameRate();
    return observed;
  };
  std::string shownTitle = kWindowTitle;
  hooks.drawFrame = [&] {
    renderFrame(window, state, vulkan, app);
    if (std::string title = app.windowTitle(kWindowTitle); title != shownTitle) {
      glfwSetWindowTitle(window, title.c_str());
      shownTitle = std::move(title);
    }
  };
  hooks.failed = [&state] { return !state.fatalError.empty(); };
  hooks.wait = [](std::uint32_t timeoutMs) { glfwWaitEventsTimeout(static_cast<double>(timeoutMs) / 1000.0); };
  hooks.nowMs = [] {
    return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
                                        std::chrono::steady_clock::now().time_since_epoch())
                                        .count());
  };
  bitoscope::gui_frame_pacing::FramePacer pacer;
  const int exitCode = bitoscope::gui_frame_pacing::runFrameLoop(hooks, pacer);
  if (exitCode != 0) {
    showError(state.fatalError);
  }
  return exitCode;
}

}

int main() {
  // WHY: Before glfwInit() and before any thread starts: GLFW reads XCURSOR_THEME at init,
  // and setenv() is unsafe beside threads.
  if (expectedGlfwWindowSystemFromEnvironment() == bitoscope::gui_linux::GlfwWindowSystem::Wayland) {
    applyWaylandCursorThemeWorkaround();
  }

  WindowState state;
  // WHY: Declared before the colour scheme watcher and app, whose threads call it, so it outlives them.
  bitoscope::gui_linux::WakeGate wake([] { glfwPostEmptyEvent(); });
  ColorSchemeWatcher colorScheme([&state, &wake] {
    state.systemThemeChanged = true;
    wake.wake();
  });

  const std::filesystem::path configDir =
    bitoscope::gui::linux_platform::configDirectoryFromEnvironment();
  std::filesystem::path preferencesFile = configDir.empty() ? configDir : configDir / "preferences.json";
#ifdef BITOSCOPE_GUI_TEST_HOOK
  const bitoscope::gui_test_input::PreferencesChoice preferencesChoice =
    bitoscope::gui::linux_platform::TestHook::choosePreferencesFile(preferencesFile);
  const bool preferencesOverridden = preferencesChoice.file != preferencesFile;
  preferencesFile = preferencesChoice.file;
  const std::string preferencesNote = preferencesChoice.note;
#endif
  bitoscope::gui_prefs::StartupPreferences startup;
  if (!preferencesFile.empty()) {
    startup = bitoscope::gui_prefs::loadPreferencesOrDefaults(
      preferencesFile, bitoscope::gui_prefs::LegacyPlacementTarget::Drop);
  }

  glfwSetErrorCallback(onGlfwError);
  if (glfwInit() == GLFW_FALSE) {
    showError("GLFW could not be started (no Wayland or X11 display?); see GLFW error above.");
    return 1;
  }
  if (glfwVulkanSupported() == GLFW_FALSE) {
    showError("No Vulkan loader or no Vulkan driver was found (GLFW: glfwVulkanSupported).");
    glfwTerminate();
    return 1;
  }
  const bool wayland = onWayland();
  const bitoscope::gui_prefs::WindowSystem windowSystem =
    wayland ? bitoscope::gui_prefs::WindowSystem::Wayland : bitoscope::gui_prefs::WindowSystem::X11;

  std::optional<bitoscope::gui_prefs::WindowPlacement> placement;
  if (auto saved = startup.preferences.windowPlacements.find(windowSystem);
      saved != startup.preferences.windowPlacements.end()) {
    placement = saved->second;
  }
  std::string placementNote;
  if (placement) {
    if (auto note = bitoscope::gui_prefs::placementRejectionNote(
          *placement, bitoscope::gui::linux_platform::monitorWorkAreas())) {
      placementNote = *note;
      placement.reset();
    }
  }

  glfwDefaultWindowHints();
  glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
  glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
  glfwWindowHint(GLFW_SCALE_TO_MONITOR, GLFW_FALSE);  // WHY: Sizes below are already scaled.
  // WHY: GLFW_AUTO_ICONIFY is off: borderless full screen must not minimize when it loses focus.
  glfwWindowHint(GLFW_AUTO_ICONIFY, GLFW_FALSE);
  glfwWindowHintString(GLFW_WAYLAND_APP_ID, "bitoscope");
  glfwWindowHintString(GLFW_X11_CLASS_NAME, "bitoscope");
  glfwWindowHintString(GLFW_X11_INSTANCE_NAME, "bitoscope");
  int defaultWidth = kInitialWidth;
  int defaultHeight = kInitialHeight;
  if (!wayland) {
    float scale = 1.0f;
    float unused = 1.0f;
    if (GLFWmonitor* primary = glfwGetPrimaryMonitor()) {
      glfwGetMonitorContentScale(primary, &scale, &unused);
    }
    defaultWidth = static_cast<int>(std::lround(kInitialWidth * scale));
    defaultHeight = static_cast<int>(std::lround(kInitialHeight * scale));
  }
  const std::optional<bitoscope::gui_prefs::ScreenRect> work = primaryWorkArea(std::nullopt);
  const bitoscope::gui_prefs::WindowRectPlan rectPlan =
    bitoscope::gui_prefs::windowRectPlan(placement, work, defaultWidth, defaultHeight);
  if (!wayland && (rectPlan.hasPosition || work)) {
    glfwWindowHint(GLFW_POSITION_X, rectPlan.left);
    glfwWindowHint(GLFW_POSITION_Y, rectPlan.top);
  }
  if (placement) {
    glfwWindowHint(GLFW_MAXIMIZED, placement->maximized ? GLFW_TRUE : GLFW_FALSE);
  }
  GLFWwindow* window =
    glfwCreateWindow(rectPlan.width, rectPlan.height, kWindowTitle, nullptr, nullptr);
  if (window == nullptr) {
    showError("Window could not be created (glfwCreateWindow); see GLFW error above.");
    glfwTerminate();
    return 1;
  }
  glfwSetWindowUserPointer(window, &state);
  glfwGetWindowSize(window, &state.placement.normal.width, &state.placement.normal.height);
  if (!wayland) {
    glfwGetWindowPos(window, &state.placement.normal.x, &state.placement.normal.y);
  }
  state.placement.maximized = placement && placement->maximized;
  state.restoreSize.remember({state.placement.normal.width, state.placement.normal.height});
  glfwSetWindowPosCallback(window, onWindowPos);
  glfwSetWindowSizeCallback(window, onWindowSize);
  glfwSetWindowMaximizeCallback(window, onWindowMaximize);
  glfwSetWindowContentScaleCallback(window, onContentScale);
  installEventCounters(window);  // WHY: Before Dear ImGui's backend, which chains the input callbacks.
  wake.open();

  bool readableSwapchain = false;
#ifdef BITOSCOPE_GUI_TEST_HOOK
  readableSwapchain = bitoscope::gui::linux_platform::TestHook::requested();
#endif
  auto vulkan = VulkanDevice::create(window, readableSwapchain);
  if (!vulkan) {
    showError("Vulkan could not be started: " + vulkan.error());
    glfwDestroyWindow(window);
    wake.close();
    glfwTerminate();
    return 1;
  }
  VulkanDevice& device = **vulkan;

  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  bitoscope::gui::linux_platform::PersistentMappings backendMappings({vkMapMemory, vkUnmapMemory, vkFreeMemory});
  bitoscope::gui::linux_platform::BackendFunctionLoader backendLoader{
    &backendMappings,
    [](const char* name, void* instance) { return vkGetInstanceProcAddr(static_cast<VkInstance>(instance), name); },
    device.instance()};
  if (!ImGui_ImplVulkan_LoadFunctions(VK_API_VERSION_1_0, bitoscope::gui::linux_platform::loadBackendFunction,
                                      &backendLoader)) {
    showError("Vulkan could not be started: function needed by Dear ImGui's Vulkan backend is missing.");
    ImGui::DestroyContext();
    vulkan->reset();
    glfwDestroyWindow(window);
    wake.close();
    glfwTerminate();
    return 1;
  }
  ImGui_ImplVulkan_InitInfo init{};
  init.ApiVersion = VK_API_VERSION_1_0;
  init.Instance = device.instance();
  init.PhysicalDevice = device.physicalDevice();
  init.Device = device.device();
  init.QueueFamily = device.queueFamily();
  init.Queue = device.queue();
  init.DescriptorPool = device.descriptorPool();
  init.MinImageCount = device.minImageCount();
  init.ImageCount = std::max(device.imageCount(), device.minImageCount());
  init.PipelineInfoMain.RenderPass = device.renderPass();
  init.PipelineInfoMain.Subpass = 0;
  init.PipelineInfoMain.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
  init.CheckVkResultFn = onVulkanBackendResult;
  ImGui_ImplVulkan_Init(&init);

  const bool startFullScreen = startup.preferences.fullScreen;
  std::optional<VulkanWaveformRenderer> waveform(std::in_place, device);
  LinuxPlatformServices platform(window, device, &state.placement, colorScheme, &*waveform);
  GuiApp app(platform, std::move(startup));
  app.setWakeCallback([&wake] { wake.wake(); });
  platform.setDialogPump([&] { return pumpDialogFrame(window, state, device, app); });
  app.logger().logf(bitoscope::log::LogLevel::info, "gui", "Window system: {} (GLFW {}); Vulkan device: {}",
                    wayland ? "Wayland" : "X11", glfwGetVersionString(), device.description());
  if (!wayland) {
    for (const std::string& problem :
         bitoscope::gui::linux_platform::setX11WindowIcon(
           window, platform.executableDirectory() / "assets" / "app_icon")) {
      app.logger().log(bitoscope::log::LogLevel::warning, "gui", problem);
    }
  }
  if (!placementNote.empty()) {
    app.logger().log(bitoscope::log::LogLevel::warning, "gui", placementNote);
  }
#ifdef BITOSCOPE_GUI_TEST_HOOK
  platform.setPreferencesFile(preferencesFile);
  if (!preferencesNote.empty()) {
    app.logger().log(preferencesOverridden ? bitoscope::log::LogLevel::info : bitoscope::log::LogLevel::warning, "gui",
                     preferencesNote);
  }
#endif
  app.configure(dpiScaleOf(window));
  ImGui_ImplGlfw_InitForVulkan(window, true);

#ifdef BITOSCOPE_GUI_TEST_HOOK
  // WHY: Destroyed before wake and app: its socket thread must never wake something that is gone.
  std::string hookNote;
  std::unique_ptr<bitoscope::gui::linux_platform::TestHook> testHook =
    bitoscope::gui::linux_platform::TestHook::createFromEnvironment(hookNote);
  std::optional<bitoscope::gui_test_input::FrameTimesProbe> frameProbe;
  if (testHook) {
    testHook->setWakeCallback([&wake] { wake.wake(); });
    state.testHook = testHook.get();
    frameProbe.emplace(testHook->frameTimes(), steadyNs);
    app.setFrameProbe(&*frameProbe);
    platform.setTestDialogAnswers(&testHook->dialogAnswers(), [&app](std::string_view message) {
      app.logger().log(bitoscope::log::LogLevel::info, "gui", std::string(message));
    });
    testHook->setCaptureSource([&app] {
      std::vector<bitoscope::gui_test_input::SessionCapture> captures;
      for (const auto& panel : app.panelCaptureStatuses()) {
        bitoscope::gui_test_input::SessionCapture capture;
        capture.name = panel.name;
        capture.state = std::string(bitoscope::gui_capture::captureStateName(panel.status.state));
        capture.stored = panel.status.storedSamples;
        capture.requested = panel.status.requestedSamples;
        capture.reason = panel.status.reason;
        captures.push_back(std::move(capture));
      }
      return captures;
    });
    testHook->setPresentModeControl(
      {.apply = [window, &device](bitoscope::gui_test_input::PresentModeRequest request)
         -> std::expected<std::string, std::string> {
         int width = 0;
         int height = 0;
         glfwGetFramebufferSize(window, &width, &height);
         if (auto set = device.setVsync(request == bitoscope::gui_test_input::PresentModeRequest::Normal,
                                        static_cast<std::uint32_t>(std::max(width, 0)),
                                        static_cast<std::uint32_t>(std::max(height, 0)));
             !set) {
           return std::unexpected(set.error());
         }
         return device.presentModeName();
       },
       .current = [&device] { return device.presentModeName(); }});
    device.setSubmitStamp([&state] {
      if (state.testHook != nullptr) {
        state.testHook->frameTimes().endPhase(bitoscope::gui_test_input::FramePhase::Submit, steadyNs());
      }
    });
    bitoscope::gui_test_input::blockRealInput(ImGui::GetIO());
  }
  if (!hookNote.empty()) {
    app.logger().log(testHook ? bitoscope::log::LogLevel::info : bitoscope::log::LogLevel::warning, "gui", hookNote);
  }
#endif

  glfwShowWindow(window);
  if (wayland) {
    float windowScale = 1.0f;
    float unused = 1.0f;
    glfwGetWindowContentScale(window, &windowScale, &unused);
    if (const auto fine = primaryWorkArea(windowScale)) {
      const bitoscope::gui_prefs::WindowRectPlan finePlan =
        bitoscope::gui_prefs::windowRectPlan(placement, fine, defaultWidth, defaultHeight);
      if (finePlan.width != rectPlan.width || finePlan.height != rectPlan.height) {
        // WHY: GLFW's Wayland backend does not report a size the client set itself, so it is tracked here.
        state.restoreSize.remember({finePlan.width, finePlan.height});
        state.placement.normal.width = finePlan.width;
        state.placement.normal.height = finePlan.height;
        if (!(placement && placement->maximized)) {
          glfwSetWindowSize(window, finePlan.width, finePlan.height);
        }
      }
    }
  }
  if (startFullScreen) {
    platform.setFullScreen(true);
  }

  const int exitCode = runFrames(window, state, device, app, colorScheme, platform);

#ifdef BITOSCOPE_GUI_TEST_HOOK
  state.testHook = nullptr;
  app.setFrameProbe(nullptr);
  frameProbe.reset();
  platform.setTestDialogAnswers(nullptr, {});  // WHY: Clear the answers before the hook, which owns them, is destroyed.
  testHook.reset();
#endif

  app.shutdown();  // WHY: app.shutdown() needs the window and the renderer backend alive: it saves preferences and releases textures.
  app.setWakeCallback({});
  waveform.reset();
  device.waitIdleAndRelease();
  ImGui_ImplVulkan_Shutdown();
  ImGui_ImplGlfw_Shutdown();
  ImGui::DestroyContext();
  vulkan->reset();
  glfwDestroyWindow(window);
  wake.close();  // WHY: No glfwPostEmptyEvent() after this: the colour scheme watcher's thread still runs.
  glfwTerminate();
  return exitCode;
}

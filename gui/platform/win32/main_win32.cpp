/*
 * SPDX-License-Identifier: MIT
 */

#include <windows.h>

#include <dwmapi.h>

#include <imgui.h>
#include <imgui_impl_dx11.h>
#include <imgui_impl_win32.h>

#include <preferences_file.h>
#include <window_placement.h>

#include <frame_loop.h>
#include <frame_pacer.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cwchar>
#include <format>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <utility>

#include "d3d11_device.h"
#include "gui_app.h"
#include "win32_platform.h"

#ifdef BITOSCOPE_GUI_TEST_HOOK
#include <frame_times_probe.h>
#include <imgui_bridge.h>

#include "test_hook.h"

#define BITOSCOPE_HOOK_STAMP(hook, phase)                                                               \
  ((hook) != nullptr ? (hook)->frameTimes().endPhase(bitoscope::gui_test_input::FramePhase::phase, steadyNs()) \
                     : void())
#else
#define BITOSCOPE_HOOK_STAMP(hook, phase) ((void)0)
#endif

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam,
                                                             LPARAM lParam);

namespace {

using bitoscope::gui::GuiApp;
using bitoscope::gui::win32::D3d11Device;
using bitoscope::gui::win32::Win32PlatformServices;
using bitoscope::gui_frame_pacing::AppObservation;
using bitoscope::gui_frame_pacing::FrameLoopHooks;
using bitoscope::gui_frame_pacing::WindowObservation;

constexpr wchar_t kWindowClass[] = L"bitoscope_gui";
constexpr wchar_t kWindowTitle[] = L"bitoscope";
constexpr int kInitialWidth = 1280;
constexpr int kInitialHeight = 800;

struct WindowState {
  UINT resizeWidth = 0;
  UINT resizeHeight = 0;
  float newDpiScale = 0;
  bool systemThemeChanged = false;
  bool closeRequested = false;
  bool forceClose = false;
  bool activity = false;
  RECT normalRect{};
  bool inSizeMove = false;
  bool renderingFrame = false;
  bool dialogVeil = false;
  std::function<void()> renderDuringSizing;
  bool cloaked = true;
  bool occluded = false;
  std::string fatalError;
  // WHY: WindowState::normalRect must not follow WM_WINDOWPOSCHANGED in
  // full screen: it would keep the screen rectangle.
  bool fullScreen = false;
#ifdef BITOSCOPE_GUI_TEST_HOOK
  bitoscope::gui::win32::TestHook* testHook = nullptr;
#endif
};

class WakeEvent {
 public:
  WakeEvent() : handle_(CreateEventW(nullptr, FALSE, FALSE, nullptr)) {}
  ~WakeEvent() {
    if (handle_ != nullptr) {
      CloseHandle(handle_);
    }
  }
  WakeEvent(const WakeEvent&) = delete;
  WakeEvent& operator=(const WakeEvent&) = delete;
  HANDLE handle() const { return handle_; }

 private:
  HANDLE handle_;
};

float dpiScaleOf(HWND hwnd) {
  return static_cast<float>(GetDpiForWindow(hwnd)) / static_cast<float>(USER_DEFAULT_SCREEN_DPI);
}

bool isActivityMessage(UINT msg) {
  switch (msg) {
  case WM_TIMER:
  case WM_PAINT:
  case WM_NCHITTEST:
  case WM_SETCURSOR:
  case WM_GETICON:
  case WM_GETTEXT:
  case WM_GETTEXTLENGTH:
    return false;
  default:
    return true;
  }
}

#ifdef BITOSCOPE_GUI_TEST_HOOK
bool isRealInputMessage(UINT msg) {
  switch (msg) {
  case WM_NCMOUSEMOVE:
  case WM_NCMOUSELEAVE:
  case WM_MOUSELEAVE:
  case WM_SETFOCUS:
  case WM_KILLFOCUS:
  case WM_IME_COMPOSITION:
  case WM_IME_CHAR:
    return true;
  default:
    return (msg >= WM_MOUSEFIRST && msg <= WM_MOUSELAST) || (msg >= WM_KEYFIRST && msg <= WM_KEYLAST) ||
           msg == WM_MOUSEHWHEEL;
  }
}
#endif

LRESULT CALLBACK windowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
  auto* state = reinterpret_cast<WindowState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
  if (state != nullptr && isActivityMessage(msg)) {
    state->activity = true;
  }
  bool toImGui = true;
#ifdef BITOSCOPE_GUI_TEST_HOOK
  toImGui = !(state != nullptr && state->testHook != nullptr && isRealInputMessage(msg));
#endif
  if (toImGui && ImGui_ImplWin32_WndProcHandler(hwnd, msg, wParam, lParam)) {
    return 1;
  }
  switch (msg) {
  case WM_SIZE:
    if (state != nullptr && wParam != SIZE_MINIMIZED) {
      state->resizeWidth = LOWORD(lParam);
      state->resizeHeight = HIWORD(lParam);
      if (state->inSizeMove && state->renderDuringSizing && !state->renderingFrame) {
        state->renderDuringSizing();
      }
    }
    return 0;
  case WM_ENTERSIZEMOVE:
    if (state != nullptr) {
      state->inSizeMove = true;
    }
    break;
  case WM_EXITSIZEMOVE:
    if (state != nullptr) {
      state->inSizeMove = false;
    }
    break;
  case WM_DPICHANGED: {
    const RECT* suggested = reinterpret_cast<const RECT*>(lParam);
    SetWindowPos(hwnd, nullptr, suggested->left, suggested->top,
                 suggested->right - suggested->left, suggested->bottom - suggested->top,
                 SWP_NOZORDER | SWP_NOACTIVATE);
    if (state != nullptr) {
      state->newDpiScale =
        static_cast<float>(LOWORD(wParam)) / static_cast<float>(USER_DEFAULT_SCREEN_DPI);
    }
    return 0;
  }
  case WM_SETTINGCHANGE:
    if (state != nullptr && lParam != 0 &&
        std::wcscmp(reinterpret_cast<const wchar_t*>(lParam), L"ImmersiveColorSet") == 0) {
      state->systemThemeChanged = true;
    }
    break;
  case WM_WINDOWPOSCHANGED:
    if (state != nullptr && !state->fullScreen && !IsIconic(hwnd) && !IsZoomed(hwnd)) {
      GetWindowRect(hwnd, &state->normalRect);
    }
    break;
  case WM_CLOSE:
    if (state != nullptr) {
      state->closeRequested = true;
      return 0;
    }
    break;
  case WM_SYSCOMMAND:
    // WHY: Swallow SC_KEYMENU: Alt alone would open the absent window menu and swallow the next key press.
    if ((wParam & 0xfff0) == SC_KEYMENU) {
      return 0;
    }
    break;
  case WM_DESTROY:
    PostQuitMessage(0);
    return 0;
  default:
    break;
  }
  return DefWindowProcW(hwnd, msg, wParam, lParam);
}

#ifdef BITOSCOPE_GUI_TEST_HOOK
std::uint64_t steadyNs() {
  return static_cast<std::uint64_t>(
    std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());
}
#endif

void showError(HWND owner, const std::string& text) {
  MessageBoxA(owner, text.c_str(), "bitoscope", MB_OK | MB_ICONERROR);
}

// WHY: Window is shown cloaked until its first Present: a flip-model swap
// chain is blank before it (white flash in dark theme).
void setCloaked(HWND hwnd, bool cloaked) {
  const BOOL value = cloaked ? TRUE : FALSE;
  DwmSetWindowAttribute(hwnd, DWMWA_CLOAK, &value, sizeof(value));
}

void renderFrame(HWND hwnd, WindowState& state, D3d11Device& d3d, GuiApp& app) {
  if (!state.fatalError.empty()) {
    return;
  }
#ifdef BITOSCOPE_GUI_TEST_HOOK
  bitoscope::gui::win32::TestHook* const hook = state.testHook;
  if (hook != nullptr) {
    hook->frameTimes().beginFrame(steadyNs());
  }
#endif
  state.renderingFrame = true;
  if (state.resizeWidth != 0 && state.resizeHeight != 0) {
    if (auto resized = d3d.resize(state.resizeWidth, state.resizeHeight); !resized) {
      state.fatalError = resized.error();
      state.renderingFrame = false;
      return;
    }
    state.resizeWidth = state.resizeHeight = 0;
  }
  if (state.newDpiScale != 0) {
    app.setDpiScale(state.newDpiScale);
    state.newDpiScale = 0;
  }
  if (state.systemThemeChanged) {
    app.systemThemeChanged();
    state.systemThemeChanged = false;
  }
  app.beginFrame();
  BITOSCOPE_HOOK_STAMP(hook, AppBegin);

  ImGui_ImplDX11_NewFrame();
  ImGui_ImplWin32_NewFrame();
#ifdef BITOSCOPE_GUI_TEST_HOOK
  const auto hookNowMs = static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
                                                      std::chrono::steady_clock::now().time_since_epoch()).count());
  if (state.testHook != nullptr) {
    state.testHook->injectInput(ImGui::GetIO(), hookNowMs);
  }
#endif
  ImGui::NewFrame();
  BITOSCOPE_HOOK_STAMP(hook, NewFrame);
  app.drawFrame();
  if (state.dialogVeil) {
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    const ImVec2 max(viewport->Pos.x + viewport->Size.x, viewport->Pos.y + viewport->Size.y);
    ImGui::GetForegroundDrawList()->AddRectFilled(viewport->Pos, max, ImGui::GetColorU32(ImGuiCol_ModalWindowDimBg));
  }
  BITOSCOPE_HOOK_STAMP(hook, Draw);
  ImGui::Render();
  BITOSCOPE_HOOK_STAMP(hook, Render);
  d3d.beginFrame(app.clearColor());
  BITOSCOPE_HOOK_STAMP(hook, Acquire);
  ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
  BITOSCOPE_HOOK_STAMP(hook, Record);
#ifdef BITOSCOPE_GUI_TEST_HOOK
  const bool distorted = hook != nullptr && hook->frameDistorted();
  // WHY: answerQuery() must run BEFORE Present: a flip-model swap chain discards the back buffer when it is presented.
  if (state.testHook != nullptr) {
    state.testHook->answerQuery(*ImGui::GetCurrentContext(), d3d);
  }
#endif
  BITOSCOPE_HOOK_STAMP(hook, Hook);

  const D3d11Device::PresentResult presented = d3d.present();
  BITOSCOPE_HOOK_STAMP(hook, Present);
#ifdef BITOSCOPE_GUI_TEST_HOOK
  if (state.testHook != nullptr) {
    state.testHook->finishFrame(hookNowMs);
    state.testHook->frameTimes().endPhase(bitoscope::gui_test_input::FramePhase::Hook, steadyNs());
    state.testHook->frameTimes().commitFrame(static_cast<std::uint64_t>(ImGui::GetFrameCount()), distorted);
    if (state.testHook->quitRequested()) {
      state.forceClose = true;
    }
    if (state.testHook->takeCloseRequest()) {
      state.closeRequested = true;
    }
  }
#endif
  if (state.cloaked) {
    setCloaked(hwnd, false);
    state.cloaked = false;
  }
  switch (presented) {
  case D3d11Device::PresentResult::Shown:
    break;
  case D3d11Device::PresentResult::Occluded:
    state.occluded = true;
    break;
  case D3d11Device::PresentResult::DeviceLost:
    state.fatalError = "Graphics device was lost (removed, reset or hung); bitoscope must close.";
    break;
  }
  state.renderingFrame = false;
}

void renderVeiledFrame(HWND hwnd, WindowState& state, D3d11Device& d3d, GuiApp& app) {
  if (state.renderingFrame) {
    return;
  }
  state.dialogVeil = true;
  renderFrame(hwnd, state, d3d, app);
  state.dialogVeil = false;
}

// WHY: Loop wakes on an event, not a WM_APP posted to the window: a
// thread posting during shutdown could hit a dead HWND.
int runFrames(HWND hwnd, WindowState& state, D3d11Device& d3d, GuiApp& app, HANDLE wake) {
  state.renderDuringSizing = [&] { renderFrame(hwnd, state, d3d, app); };
  bool quit = false;
  int quitCode = 0;
  const auto testHookOn = [&state] {
#ifdef BITOSCOPE_GUI_TEST_HOOK
    return state.testHook != nullptr;
#else
    (void)state;
    return false;
#endif
  };

  FrameLoopHooks hooks;
  hooks.handleEvents = [&] {
    MSG msg;
    while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
      if (msg.message == WM_QUIT) {
        quit = true;
        quitCode = static_cast<int>(msg.wParam);
        return;
      }
      TranslateMessage(&msg);
      DispatchMessageW(&msg);
    }
  };
  hooks.closeRequested = [&] {
    if (quit) {
      return true;
    }
    if (state.closeRequested) {
      state.closeRequested = false;
      app.requestAppClose();
    }
    return state.forceClose || app.closeConfirmed();
  };
  hooks.observeWindow = [&] {
    WindowObservation observed;
    RECT client{};
    GetClientRect(hwnd, &client);
    observed.framebufferWidth = static_cast<int>(client.right - client.left);
    observed.framebufferHeight = static_cast<int>(client.bottom - client.top);
    // WHY: Minimized is tested directly: a flip-model Present does not reliably report a minimized window as occluded.
    if (testHookOn() && !IsIconic(hwnd)) {
      state.occluded = false;
    } else if (IsIconic(hwnd)) {
      if (state.cloaked) {
        setCloaked(hwnd, false);
        state.cloaked = false;
      }
      observed.iconified = true;
    } else if (state.occluded && d3d.stillOccluded()) {
      observed.occluded = true;
    } else {
      state.occluded = false;
    }
    observed.eventsHandled = std::exchange(state.activity, false);
    observed.testHookBusy = testHookOn();
    observed.changePending = state.resizeWidth != 0 || state.resizeHeight != 0 || state.newDpiScale != 0 ||
                             state.systemThemeChanged || state.cloaked;
    return observed;
  };
  hooks.pumpBackground = [&] {
    AppObservation observed;
    observed.backgroundChanged = app.pumpBackground();
    observed.captureRunning = app.captureRunning();
    observed.captureAdvanced = app.captureAdvanced();
    observed.asyncWorkInFlight = app.asyncWorkInFlight();
    observed.uiNeedsMore = app.wantsMoreFrames();
    observed.reduceIdleFrameRate = app.reduceIdleFrameRate();
    return observed;
  };
  std::string shownTitle = "bitoscope";
  hooks.drawFrame = [&] {
    const auto frameStart = std::chrono::steady_clock::now();
    renderFrame(hwnd, state, d3d, app);
    if (std::string title = app.windowTitle("bitoscope"); title != shownTitle) {
      SetWindowTextA(hwnd, title.c_str());
      shownTitle = std::move(title);
    }
    if (testHookOn() && d3d.syncInterval() == 0) {
      const auto spent = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - frameStart);
      if (spent < std::chrono::milliseconds(16)) {
        Sleep(static_cast<DWORD>((std::chrono::milliseconds(16) - spent).count()));
      }
    }
  };
  hooks.failed = [&state] { return !state.fatalError.empty(); };
  hooks.wait = [wake](std::uint32_t timeoutMs) {
    MsgWaitForMultipleObjectsEx(1, &wake, timeoutMs, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
  };
  hooks.nowMs = [] {
    return static_cast<std::uint64_t>(
      std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count());
  };

  bitoscope::gui_frame_pacing::FramePacer pacer;
  const int exitCode = bitoscope::gui_frame_pacing::runFrameLoop(hooks, pacer);
  state.renderDuringSizing = nullptr;
  if (exitCode != 0) {
    showError(hwnd, state.fatalError);
    return exitCode;
  }
  return quit ? quitCode : 0;
}

}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int showCommand) {
  WNDCLASSEXW wc{};
  wc.cbSize = sizeof(wc);
  wc.style = CS_HREDRAW | CS_VREDRAW;
  wc.lpfnWndProc = windowProc;
  wc.hInstance = instance;
  wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
  wc.lpszClassName = kWindowClass;
  wc.hIcon = static_cast<HICON>(LoadImageW(instance, MAKEINTRESOURCEW(1), IMAGE_ICON,
                                           GetSystemMetrics(SM_CXICON), GetSystemMetrics(SM_CYICON),
                                           LR_DEFAULTCOLOR));
  wc.hIconSm = static_cast<HICON>(LoadImageW(instance, MAKEINTRESOURCEW(1), IMAGE_ICON,
                                             GetSystemMetrics(SM_CXSMICON),
                                             GetSystemMetrics(SM_CYSMICON), LR_DEFAULTCOLOR));
  if (RegisterClassExW(&wc) == 0) {
    showError(nullptr, "RegisterClassExW failed.");
    return 1;
  }

  const std::filesystem::path configDir = bitoscope::gui::win32::roamingConfigDirectory();
  std::filesystem::path preferencesFile = configDir.empty() ? configDir : configDir / "preferences.json";
  std::string preferencesNote;
#ifdef BITOSCOPE_GUI_TEST_HOOK
  const bitoscope::gui_test_input::PreferencesChoice preferencesChoice =
    bitoscope::gui::win32::TestHook::choosePreferencesFile(preferencesFile);
  const bool preferencesOverridden = preferencesChoice.file != preferencesFile;
  preferencesFile = preferencesChoice.file;
  preferencesNote = preferencesChoice.note;
#endif
  bitoscope::gui_prefs::StartupPreferences startup;
  if (!preferencesFile.empty()) {
    startup = bitoscope::gui_prefs::loadPreferencesOrDefaults(
      preferencesFile, bitoscope::gui_prefs::LegacyPlacementTarget::MapToWin32);
  }
  std::optional<bitoscope::gui_prefs::WindowPlacement> placement;
  if (auto saved = startup.preferences.windowPlacements.find(bitoscope::gui_prefs::WindowSystem::Win32);
      saved != startup.preferences.windowPlacements.end()) {
    placement = saved->second;
  }
  std::string placementNote;
  if (placement) {
    if (auto note = bitoscope::gui_prefs::placementRejectionNote(
          *placement, bitoscope::gui::win32::monitorWorkAreas())) {
      placementNote = *note;
      placement.reset();
    }
  }

  const bool hasPosition = placement && placement->position.has_value();
  HWND hwnd = hasPosition
                ? CreateWindowExW(0, kWindowClass, kWindowTitle, WS_OVERLAPPEDWINDOW,
                                  placement->position->x, placement->position->y, placement->width,
                                  placement->height, nullptr, nullptr, instance, nullptr)
                : CreateWindowExW(0, kWindowClass, kWindowTitle, WS_OVERLAPPEDWINDOW, CW_USEDEFAULT,
                                  CW_USEDEFAULT,
                                  placement ? placement->width : kInitialWidth,
                                  placement ? placement->height : kInitialHeight, nullptr, nullptr,
                                  instance, nullptr);
  if (hwnd == nullptr) {
    showError(nullptr, "CreateWindowExW failed.");
    UnregisterClassW(kWindowClass, instance);
    return 1;
  }
  WindowState state;
  SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(&state));
  GetWindowRect(hwnd, &state.normalRect);
  const float dpiScale = dpiScaleOf(hwnd);
  MONITORINFO monitor{};
  monitor.cbSize = sizeof(monitor);
  const bool haveMonitorInfo =
    GetMonitorInfoW(MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST), &monitor) != 0;
  std::optional<bitoscope::gui_prefs::ScreenRect> workArea;
  if (haveMonitorInfo) {
    workArea = bitoscope::gui_prefs::ScreenRect{monitor.rcWork.left, monitor.rcWork.top,
                                                monitor.rcWork.right, monitor.rcWork.bottom};
  }
  const bitoscope::gui_prefs::WindowRectPlan rectPlan = bitoscope::gui_prefs::windowRectPlan(
    placement, workArea, static_cast<int32_t>(kInitialWidth * dpiScale),
    static_cast<int32_t>(kInitialHeight * dpiScale));
  if (!rectPlan.hasPosition) {
    const UINT flags = (haveMonitorInfo ? SWP_NOZORDER : (SWP_NOMOVE | SWP_NOZORDER)) | SWP_NOACTIVATE;
    SetWindowPos(hwnd, nullptr, rectPlan.left, rectPlan.top, rectPlan.width, rectPlan.height, flags);
  }

  auto d3d = D3d11Device::create(hwnd);
  if (!d3d) {
    showError(hwnd, "Direct3D 11 could not be started: " + d3d.error());
    DestroyWindow(hwnd);
    UnregisterClassW(kWindowClass, instance);
    return 1;
  }
  state.resizeWidth = state.resizeHeight = 0;

  const bool startFullScreen = startup.preferences.fullScreen;

  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  // WHY: `wake` is declared before `app` so it outlives every thread that
  // can still post; app joins them in its destructor.
  WakeEvent wake;
  Win32PlatformServices platform(hwnd, d3d->device(), d3d->context(), &state.normalRect,
                                 &state.fullScreen);
  GuiApp app(platform, std::move(startup));
  app.setWakeCallback([handle = wake.handle()] { SetEvent(handle); });
  platform.setDialogVeil([&] { renderVeiledFrame(hwnd, state, *d3d, app); });
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
  app.configure(dpiScale);
  ImGui_ImplWin32_Init(hwnd);
  ImGui_ImplDX11_Init(d3d->device(), d3d->context());

#ifdef BITOSCOPE_GUI_TEST_HOOK
  // WHY: Test hook is declared after `wake` and `app` and ends before both:
  // its pipe thread must not post to a dead object.
  std::string hookNote;
  std::unique_ptr<bitoscope::gui::win32::TestHook> testHook =
    bitoscope::gui::win32::TestHook::createFromEnvironment(hookNote);
  std::optional<bitoscope::gui_test_input::FrameTimesProbe> frameProbe;
  if (testHook) {
    testHook->setWakeCallback([handle = wake.handle()] { SetEvent(handle); });
    state.testHook = testHook.get();
    frameProbe.emplace(testHook->frameTimes(), steadyNs);
    app.setFrameProbe(&*frameProbe);
    d3d->setSyncInterval(0);
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
    bitoscope::gui_test_input::blockRealInput(ImGui::GetIO());
  }
  if (!hookNote.empty()) {
    app.logger().log(testHook ? bitoscope::log::LogLevel::info : bitoscope::log::LogLevel::warning, "gui", hookNote);
  }
#endif

  setCloaked(hwnd, true);
  ShowWindow(hwnd, placement && placement->maximized ? SW_SHOWMAXIMIZED : showCommand);
  UpdateWindow(hwnd);
  if (startFullScreen) {
    platform.setFullScreen(true);
  }

  const int exitCode = runFrames(hwnd, state, *d3d, app, wake.handle());

#ifdef BITOSCOPE_GUI_TEST_HOOK
  state.testHook = nullptr;
  app.setFrameProbe(nullptr);
  frameProbe.reset();
  platform.setTestDialogAnswers(nullptr, {});
  testHook.reset();
#endif

  app.shutdown();
  app.setWakeCallback({});
  ImGui_ImplDX11_Shutdown();
  ImGui_ImplWin32_Shutdown();
  ImGui::DestroyContext();
  // WHY: Clear GWLP_USERDATA before DestroyWindow: the window procedure may still run and must not see freed state.
  SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
  if (IsWindow(hwnd)) {
    DestroyWindow(hwnd);
  }
  UnregisterClassW(kWindowClass, instance);
  return exitCode;
}

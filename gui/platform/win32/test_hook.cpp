/*
 * SPDX-License-Identifier: MIT
 */

#include "test_hook.h"

#ifdef BITOSCOPE_GUI_TEST_HOOK

#include <windows.h>

#include <algorithm>
#include <filesystem>
#include <format>
#include <fstream>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <imgui.h>

#include <image_scale.h>
#include <imgui_bridge.h>
#include <pipe_name.h>
#include <png_write.h>
#include <reply_format.h>

#include "d3d11_device.h"

namespace bitoscope::gui::win32 {

namespace gti = bitoscope::gui_test_input;

namespace {

std::string syncIntervalName(UINT interval) { return std::format("sync-interval-{}", interval); }

std::string environmentToken(bool& isSet) {
  wchar_t buffer[256];
  SetLastError(0);
  const DWORD length = GetEnvironmentVariableW(L"BITOSCOPE_TEST_INPUT", buffer, static_cast<DWORD>(std::size(buffer)));
  isSet = length != 0 || GetLastError() != ERROR_ENVVAR_NOT_FOUND;
  if (length == 0 || length >= std::size(buffer)) {
    return {};
  }
  std::string token;
  for (DWORD i = 0; i < length; ++i) {
    if (buffer[i] > 127) {
      return std::string("\x01");
    }
    token.push_back(static_cast<char>(buffer[i]));
  }
  return token;
}

std::optional<std::string> environmentUtf8(const wchar_t* name) {
  SetLastError(0);
  const DWORD needed = GetEnvironmentVariableW(name, nullptr, 0);
  if (needed == 0) {
    return GetLastError() == ERROR_ENVVAR_NOT_FOUND ? std::nullopt : std::optional<std::string>(std::string());
  }
  std::wstring value(needed, L'\0');
  const DWORD length = GetEnvironmentVariableW(name, value.data(), needed);
  value.resize(length);
  if (value.empty()) {
    return std::string();
  }
  const int bytes = WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), nullptr, 0, nullptr,
                                        nullptr);
  std::string out(static_cast<std::size_t>((std::max)(bytes, 0)), '\0');
  WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), out.data(), bytes, nullptr, nullptr);
  return out;
}

std::string saveScreenshot(D3d11Device& device, const std::string& path, std::uint32_t scale) {
  auto image = device.captureBackBuffer();
  if (!image) {
    return gti::errorReply("screenshot: " + image.error());
  }
  if (scale > 1) {
    gti::RgbaPicture scaled = gti::boxDownscale(image->width, image->height, image->rgba, scale);
    image->width = scaled.width;
    image->height = scaled.height;
    image->rgba = std::move(scaled.rgba);
  }
  const std::vector<std::uint8_t> png = bitoscope::png_write::encodePng(image->width, image->height, image->rgba);
  const std::filesystem::path file{std::u8string(path.begin(), path.end())};
  std::error_code ignored;
  if (file.has_parent_path()) {
    std::filesystem::create_directories(file.parent_path(), ignored);
  }
  std::ofstream out(file, std::ios::binary | std::ios::trunc);
  if (!out) {
    return gti::errorReply(std::format("screenshot: cannot open {} for writing", path));
  }
  out.write(reinterpret_cast<const char*>(png.data()), static_cast<std::streamsize>(png.size()));
  out.close();
  if (!out) {
    return gti::errorReply(std::format("screenshot: writing {} failed", path));
  }
  return gti::screenshotReply(path, static_cast<std::int32_t>(image->width), static_cast<std::int32_t>(image->height));
}

}

TestHook::TestHook() = default;

std::unique_ptr<TestHook> TestHook::createFromEnvironment(std::string& note) {
  note.clear();
  bool isSet = false;
  const std::string token = environmentToken(isSet);
  if (!isSet) {
    return nullptr;
  }
  const auto name = gti::pipeNameForToken(token);
  if (!name) {
    note = "BITOSCOPE_TEST_INPUT is set but unusable, test hook is off: " + name.error();
    return nullptr;
  }
  std::unique_ptr<TestHook> hook(new TestHook());
  hook->server_ = std::make_unique<TestPipeServer>(*name, hook->channel_);
  std::string error;
  if (!hook->server_->start(error)) {
    note = "test hook's pipe could not be created, test hook is off: " + error;
    return nullptr;
  }
  note = "test hook on: \\\\.\\pipe\\" + *name;
  return hook;
}

TestHook::~TestHook() {
  channel_.close();
  if (server_) {
    server_->stop();
  }
}

void TestHook::setCaptureSource(std::function<std::vector<gti::SessionCapture>()> source) {
  captureSource_ = std::move(source);
}

gti::PreferencesChoice TestHook::choosePreferencesFile(const std::filesystem::path& defaultFile) {
  bool tokenSet = false;
  const std::string token = environmentToken(tokenSet);
  const bool hookRequested = tokenSet && gti::pipeNameForToken(token).has_value();
  const std::optional<std::string> value = environmentUtf8(L"BITOSCOPE_PREFERENCES");
  return gti::choosePreferencesFile(hookRequested, value ? std::optional<std::string_view>(*value) : std::nullopt,
                                    defaultFile);
}

void TestHook::setWakeCallback(std::function<void()> callback) { channel_.setWakeCallback(std::move(callback)); }

void TestHook::injectInput(ImGuiIO& io, std::uint64_t nowMs) {
  for (gti::CommandChannel::Pending& pending : channel_.take()) {
    sequencer_.push(pending.ticket, std::move(pending.command));
  }
  queryReply_.reset();
  plan_ = sequencer_.beginFrame(nowMs, gti::scaleOf(io));
  gti::applyEvents(io, plan_.events);
}

void TestHook::answerQuery(ImGuiContext& context, D3d11Device& device) {
  observation_.windowNames.clear();
  for (const gti::WindowInfo& window : gti::collectWindows(context)) {
    observation_.windowNames.push_back(window.name);
  }
  observation_.captures = captureSource_ ? captureSource_() : std::vector<gti::SessionCapture>();
  frameTimes_.noteCaptures(observation_.captures);
  if (!plan_.query) {
    return;
  }
  switch (plan_.query->kind) {
  case gti::FrameQuery::Kind::Screenshot:
    queryReply_ = saveScreenshot(device, plan_.query->path, plan_.query->scale);
    break;
  case gti::FrameQuery::Kind::Capture:
    queryReply_ = gti::captureReply(observation_.captures);
    break;
  case gti::FrameQuery::Kind::DialogSave:
    dialogAnswers_.queueSave(plan_.query->path);
    queryReply_ = gti::okReply("queued dialog-save " + plan_.query->path);
    break;
  case gti::FrameQuery::Kind::DialogOpen:
    dialogAnswers_.queueOpen(plan_.query->path);
    queryReply_ = gti::okReply("queued dialog-open " + plan_.query->path);
    break;
  case gti::FrameQuery::Kind::DialogCancel:
    dialogAnswers_.queueCancel();
    queryReply_ = gti::okReply("queued dialog-cancel");
    break;
  case gti::FrameQuery::Kind::Windows:
    queryReply_ = gti::windowsReply(gti::collectWindows(context));
    break;
  case gti::FrameQuery::Kind::Hover:
    queryReply_ = gti::hoverReply(gti::collectHover(context));
    break;
  case gti::FrameQuery::Kind::Info:
    queryReply_ = gti::infoReply(gti::collectInfo(context));
    break;
  case gti::FrameQuery::Kind::FrameMark:
    frameTimes_.mark();
    queryReply_ = gti::frameMarkReply(gti::collectInfo(context).frame + 1);
    break;
  case gti::FrameQuery::Kind::FrameTimes:
    queryReply_ =
      gti::frameTimesReply(frameTimes_.summarize(plan_.query->selection), syncIntervalName(device.syncInterval()));
    break;
  case gti::FrameQuery::Kind::PresentMode:
    device.setSyncInterval(plan_.query->presentMode == gti::PresentModeRequest::Normal ? 1 : 0);
    queryReply_ = gti::presentModeReply(plan_.query->presentMode, syncIntervalName(device.syncInterval()));
    break;
  }
}

void TestHook::finishFrame(std::uint64_t nowMs) {
  for (const gti::Reply& reply : sequencer_.endFrame(nowMs, std::exchange(queryReply_, std::nullopt), observation_)) {
    channel_.reply(reply.ticket, reply.text);
  }
  if (plan_.quit) {
    quitRequested_ = true;  // WHY: Set after the reply is queued so the client hears ok before the window goes.
    plan_.quit = false;
  }
  if (plan_.close) {
    closeRequested_ = true;
    plan_.close = false;
  }
}

}

#endif

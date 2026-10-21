/*
 * SPDX-License-Identifier: MIT
 */

#include "test_hook.h"

#ifdef BITOSCOPE_GUI_TEST_HOOK

#include <signal.h>
#include <unistd.h>

#include <cstdlib>
#include <filesystem>
#include <format>
#include <fstream>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <imgui.h>

#include <imgui_bridge.h>
#include <pipe_name.h>
#include <png_write.h>
#include <reply_format.h>
#include <socket_path.h>

namespace bitoscope::gui::linux_platform {

namespace gti = bitoscope::gui_test_input;

namespace {

std::optional<std::string> environment(const char* name) {
  const char* value = std::getenv(name);
  return value != nullptr ? std::optional<std::string>(value) : std::nullopt;
}

std::string saveScreenshot(std::expected<gti::RgbaPicture, std::string> image, const std::string& path,
                           std::uint32_t scale) {
  if (!image) {
    return gti::errorReply("screenshot: " + image.error());
  }
  if (scale > 1) {
    *image = gti::boxDownscale(image->width, image->height, image->rgba, scale);
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

bool TestHook::requested() {
  const std::optional<std::string> token = environment("BITOSCOPE_TEST_INPUT");
  return token && gti::pipeNameForToken(*token).has_value();
}

std::unique_ptr<TestHook> TestHook::createFromEnvironment(std::string& note) {
  note.clear();
  const std::optional<std::string> token = environment("BITOSCOPE_TEST_INPUT");
  if (!token) {
    return nullptr;
  }
  const std::optional<std::string> runtimeDir = environment("XDG_RUNTIME_DIR");
  const gti::SocketDirectory directory = gti::socketDirectory(
    runtimeDir ? std::optional<std::string_view>(*runtimeDir) : std::nullopt, static_cast<std::uint32_t>(geteuid()));
  const auto path = gti::socketPathForToken(*token, directory.path);
  if (!path) {
    note = "BITOSCOPE_TEST_INPUT is set but unusable, the test hook is off: " + path.error();
    return nullptr;
  }
  if (directory.privateFallback) {
    if (auto made = bitoscope::gui_test_socket::ensurePrivateDirectory(directory.path, geteuid()); !made) {
      note = "the test hook's socket directory is not usable, the test hook is off: " + made.error();
      return nullptr;
    }
  }
  std::unique_ptr<TestHook> hook(new TestHook());
  hook->server_ = std::make_unique<bitoscope::gui_test_socket::TestSocketServer>(*path, hook->channel_, geteuid());
  if (auto started = hook->server_->start(); !started) {
    note = "the test hook's socket could not be made, the test hook is off: " + started.error();
    return nullptr;
  }
  note = "test hook on: " + *path;
  if (directory.privateFallback) {
    note += " (XDG_RUNTIME_DIR is not set to an absolute path: a private directory under /tmp is used)";
  }
  if (hook->server_->replacedStaleSocket()) {
    note += " (a stale socket file of an earlier run was removed)";
  }
  if (auto armed = hook->server_->removeFileOnTerminationSignal(); !armed) {
    note += "; socket file stays if signal ends program: " + armed.error();
  } else {
    std::string names;
    for (const int signal : *armed) {
      names += std::string(names.empty() ? "" : ", ") +
               (signal == SIGTERM ? "SIGTERM" : signal == SIGINT ? "SIGINT" : signal == SIGHUP ? "SIGHUP" : "?");
    }
    note += names.empty() ? std::string("; no termination signal removes socket file (each is handled elsewhere)")
                          : "; socket file removed on " + names;
  }
  return hook;
}

TestHook::~TestHook() {
  channel_.close();
  if (server_) {
    server_->stop();
  }
}

gti::PreferencesChoice TestHook::choosePreferencesFile(const std::filesystem::path& defaultFile) {
  const std::optional<std::string> value = environment("BITOSCOPE_PREFERENCES");
  return gti::choosePreferencesFile(requested(), value ? std::optional<std::string_view>(*value) : std::nullopt,
                                    defaultFile);
}

void TestHook::setCaptureSource(std::function<std::vector<gti::SessionCapture>()> source) {
  captureSource_ = std::move(source);
}

void TestHook::setWakeCallback(std::function<void()> callback) { channel_.setWakeCallback(std::move(callback)); }

void TestHook::setPresentModeControl(PresentModeControl control) {
  presentModeControl_ = std::move(control);
}

void TestHook::takeArrived() {
  for (gti::CommandChannel::Pending& pending : channel_.take()) {
    sequencer_.push(pending.ticket, std::move(pending.command));
  }
}

bool TestHook::busy() {
  takeArrived();
  return !sequencer_.idle();
}

void TestHook::injectInput(ImGuiIO& io, std::uint64_t nowMs) {
  takeArrived();
  queryReply_.reset();
  plan_ = sequencer_.beginFrame(nowMs, gti::scaleOf(io));
  gti::applyEvents(io, plan_.events);
}

bool TestHook::wantsScreenshot() const {
  return plan_.query && plan_.query->kind == gti::FrameQuery::Kind::Screenshot;
}

void TestHook::answerQuery(ImGuiContext& context,
                           const std::function<std::expected<gti::RgbaPicture, std::string>()>& screenshot) {
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
    queryReply_ = saveScreenshot(screenshot(), plan_.query->path, plan_.query->scale);
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
    queryReply_ = gti::frameTimesReply(frameTimes_.summarize(plan_.query->selection),
                                       presentModeControl_.current ? presentModeControl_.current() : "unknown");
    break;
  case gti::FrameQuery::Kind::PresentMode:
    if (!presentModeControl_.apply) {
      queryReply_ = gti::errorReply("present-mode: not available on this platform");
      break;
    }
    if (const auto mode = presentModeControl_.apply(plan_.query->presentMode); mode) {
      queryReply_ = gti::presentModeReply(plan_.query->presentMode, *mode);
    } else {
      queryReply_ = gti::errorReply("present-mode: " + mode.error());
    }
    break;
  }
}

void TestHook::finishFrame(std::uint64_t nowMs) {
  for (const gti::Reply& reply : sequencer_.endFrame(nowMs, std::exchange(queryReply_, std::nullopt), observation_)) {
    channel_.reply(reply.ticket, reply.text);
  }
  if (plan_.quit) {
    quitRequested_ = true;  // WHY: Set after the reply is queued, so the client hears ok before the window goes.
    plan_.quit = false;
  }
  if (plan_.close) {
    closeRequested_ = true;  // WHY: Set after the reply is queued, so the client hears ok before the window closes.
    plan_.close = false;
  }
}

}

#endif

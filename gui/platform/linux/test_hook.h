/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#ifdef BITOSCOPE_GUI_TEST_HOOK

#include <cstdint>
#include <expected>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <command_channel.h>
#include <dialog_answers.h>
#include <frame_times.h>
#include <image_scale.h>
#include <observation.h>
#include <preferences_choice.h>
#include <sequencer.h>
#include <test_socket_server.h>

struct ImGuiContext;
struct ImGuiIO;

namespace bitoscope::gui::linux_platform {

struct PresentModeControl {
  std::function<std::expected<std::string, std::string>(bitoscope::gui_test_input::PresentModeRequest)> apply;
  std::function<std::string()> current;
};

class TestHook {
 public:
  static std::unique_ptr<TestHook> createFromEnvironment(std::string& note);

  static bool requested();

  static bitoscope::gui_test_input::PreferencesChoice choosePreferencesFile(const std::filesystem::path& defaultFile);

  ~TestHook();
  TestHook(const TestHook&) = delete;
  TestHook& operator=(const TestHook&) = delete;

  bitoscope::gui_test_input::DialogAnswers& dialogAnswers() { return dialogAnswers_; }

  void setCaptureSource(std::function<std::vector<bitoscope::gui_test_input::SessionCapture>()> source);

  void setWakeCallback(std::function<void()> callback);

  void setPresentModeControl(PresentModeControl control);

  bitoscope::gui_test_input::FrameTimes& frameTimes() { return frameTimes_; }

  bool frameDistorted() const { return plan_.query.has_value(); }

  bool busy();

  void injectInput(ImGuiIO& io, std::uint64_t nowMs);

  bool wantsScreenshot() const;

  void answerQuery(ImGuiContext& context,
                   const std::function<std::expected<bitoscope::gui_test_input::RgbaPicture, std::string>()>& screenshot);

  void finishFrame(std::uint64_t nowMs);

  bool quitRequested() const { return quitRequested_; }

  bool takeCloseRequest() { return std::exchange(closeRequested_, false); }

 private:
  TestHook();
  void takeArrived();

  bitoscope::gui_test_input::CommandChannel channel_;
  bitoscope::gui_test_input::Sequencer sequencer_;
  bitoscope::gui_test_input::DialogAnswers dialogAnswers_;
  std::function<std::vector<bitoscope::gui_test_input::SessionCapture>()> captureSource_;
  PresentModeControl presentModeControl_;
  bitoscope::gui_test_input::FrameTimes frameTimes_;
  bitoscope::gui_test_input::Observation observation_;
  std::unique_ptr<bitoscope::gui_test_socket::TestSocketServer> server_;
  bitoscope::gui_test_input::FramePlan plan_;
  std::optional<std::string> queryReply_;
  bool quitRequested_ = false;
  bool closeRequested_ = false;
};

}

#endif

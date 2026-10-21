/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#ifdef BITOSCOPE_GUI_TEST_HOOK

#include <cstdint>
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
#include <observation.h>
#include <preferences_choice.h>
#include <sequencer.h>

#include "test_pipe_server.h"

struct ImGuiContext;
struct ImGuiIO;

namespace bitoscope::gui::win32 {

class D3d11Device;

class TestHook {
 public:
  static std::unique_ptr<TestHook> createFromEnvironment(std::string& note);

  ~TestHook();

  static bitoscope::gui_test_input::PreferencesChoice choosePreferencesFile(const std::filesystem::path& defaultFile);

  bitoscope::gui_test_input::DialogAnswers& dialogAnswers() { return dialogAnswers_; }

  void setCaptureSource(std::function<std::vector<bitoscope::gui_test_input::SessionCapture>()> source);

  void setWakeCallback(std::function<void()> callback);

  void injectInput(ImGuiIO& io, std::uint64_t nowMs);

  void answerQuery(ImGuiContext& context, D3d11Device& device);

  bitoscope::gui_test_input::FrameTimes& frameTimes() { return frameTimes_; }

  bool frameDistorted() const { return plan_.query.has_value(); }

  void finishFrame(std::uint64_t nowMs);

  bool quitRequested() const { return quitRequested_; }

  bool takeCloseRequest() { return std::exchange(closeRequested_, false); }

 private:
  TestHook();

  bitoscope::gui_test_input::CommandChannel channel_;
  bitoscope::gui_test_input::Sequencer sequencer_;
  bitoscope::gui_test_input::DialogAnswers dialogAnswers_;
  std::function<std::vector<bitoscope::gui_test_input::SessionCapture>()> captureSource_;
  bitoscope::gui_test_input::Observation observation_;
  bitoscope::gui_test_input::FrameTimes frameTimes_;
  std::unique_ptr<TestPipeServer> server_;
  bitoscope::gui_test_input::FramePlan plan_;
  std::optional<std::string> queryReply_;
  bool quitRequested_ = false;
  bool closeRequested_ = false;
};

}

#endif

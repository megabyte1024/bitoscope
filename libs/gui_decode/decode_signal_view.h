/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <expected>
#include <string>

#include "capture_outcome.h"
#include "decoder_runner.h"
#include "raw_buffer_signal_view.h"
#include "signal_view_source.h"

namespace bitoscope::gui_decode {

std::expected<bitoscope::capture::RawBufferSignalView, std::string> signalViewForOutcome(
    const bitoscope::gui_capture::CaptureOutcome& outcome);

std::expected<SignalViewSource, std::string> signalViewSourceForOutcome(
    const bitoscope::gui_capture::CaptureOutcome& outcome);

std::expected<RunCapture, std::string> runCaptureForOutcome(const bitoscope::gui_capture::CaptureOutcome& outcome);

}

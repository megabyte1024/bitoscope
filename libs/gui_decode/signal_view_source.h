/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <expected>
#include <functional>
#include <memory>
#include <string>

#include "decoder.h"

namespace bitoscope::gui_decode {

// WHY: Each call makes a NEW view: a view is not thread-safe, so every decoder thread needs its own.
using SignalViewSource =
  std::function<std::expected<std::shared_ptr<const bitoscope::decode::SignalView>, std::string>()>;

}

/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <utility>
#include <vector>

namespace bitoscope::gui_linux {

class DeferredReleases {
 public:
  void defer(std::function<void()> release);

  std::uint64_t submitted();

  std::uint64_t submittedSerial() const { return submittedSerial_; }

  void completedUpTo(std::uint64_t serial);

  void runAll();

  std::size_t pending() const { return releases_.size(); }

 private:
  void runReady(std::uint64_t upTo);

  std::uint64_t submittedSerial_ = 0;
  std::vector<std::pair<std::uint64_t, std::function<void()>>> releases_;
};

}

/*
 * SPDX-License-Identifier: MIT
 */

#include "deferred_releases.h"

#include <limits>

namespace bitoscope::gui_linux {

void DeferredReleases::defer(std::function<void()> release) {
  // WHY: Wait for the frame being built now too: it may already hold the object in its command buffer.
  releases_.emplace_back(submittedSerial_ + 1, std::move(release));
}

std::uint64_t DeferredReleases::submitted() {
  return ++submittedSerial_;
}

void DeferredReleases::completedUpTo(std::uint64_t serial) {
  runReady(serial);
}

void DeferredReleases::runAll() {
  runReady(std::numeric_limits<std::uint64_t>::max());
}

void DeferredReleases::runReady(std::uint64_t upTo) {
  // WHY: Take ready releases out before running them, so a release may itself defer another.
  std::vector<std::function<void()>> ready;
  std::erase_if(releases_, [&](std::pair<std::uint64_t, std::function<void()>>& entry) {
    if (entry.first > upTo) {
      return false;
    }
    ready.push_back(std::move(entry.second));
    return true;
  });
  for (std::function<void()>& release : ready) {
    release();
  }
}

}

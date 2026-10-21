/*
 * SPDX-License-Identifier: MIT
 */

#include "driver_watch_mark.h"

namespace bitoscope::gui_devices {

DriverWatchMark driverWatchMark(const DriverEntry& entry) {
  if (entry.watching) {
    return {WatchMark::Watched, {}};
  }
  if (entry.supportsWatch || !entry.watchProblem.empty()) {
    return {WatchMark::NotWatched, entry.watchProblem};
  }
  return {WatchMark::None, {}};
}

}

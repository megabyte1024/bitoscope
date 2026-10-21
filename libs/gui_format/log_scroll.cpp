/*
 * SPDX-License-Identifier: MIT
 */

#include "log_scroll.h"

namespace bitoscope::gui_format {

bool logShouldScrollToNewest(bool autoScroll, bool linesArrived, bool justBecameVisible) {
  return autoScroll && (linesArrived || justBecameVisible);
}

}

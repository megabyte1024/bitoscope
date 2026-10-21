/*
 * SPDX-License-Identifier: MIT
 */

#include "properties_layout.h"

#include <algorithm>

namespace bitoscope::gui_session {

std::vector<PropertiesColumn> propertiesLayout(bool onLeft, bool open) {
  // WHY: Activity bar sits on the OUTER side of the pane it controls: owner decision, never between pane and waveform.
  if (onLeft) {
    if (open) {
      return {PropertiesColumn::Bar, PropertiesColumn::Pane, PropertiesColumn::Splitter,
              PropertiesColumn::Waveform};
    }
    return {PropertiesColumn::Bar, PropertiesColumn::Waveform};
  }
  if (open) {
    return {PropertiesColumn::Waveform, PropertiesColumn::Splitter, PropertiesColumn::Pane,
            PropertiesColumn::Bar};
  }
  return {PropertiesColumn::Waveform, PropertiesColumn::Bar};
}

float propertiesWidthAtPointer(const SplitterDrag& drag, float pointerX) {
  pointerX -= drag.grabOffset;
  const float width = drag.onLeft ? pointerX - (drag.bodyLeft + drag.barWidth) - drag.splitterWidth * 0.5f
                                  : (drag.bodyRight - drag.barWidth) - pointerX - drag.splitterWidth * 0.5f;
  return (std::max)(drag.narrowest, (std::min)(width, drag.widest));
}

float centredOffset(float columnWidth, float itemWidth) { return (std::max)(0.0f, (columnWidth - itemWidth) * 0.5f); }

}

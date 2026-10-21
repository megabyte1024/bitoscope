/*
 * SPDX-License-Identifier: MIT
 */

#include "trigger_selection.h"

namespace bitoscope::waveform_view {

trigger::TriggerSpec buildTriggerSpec(const std::vector<trigger::MatchType>& perChannel,
                                       trigger::Combine combine) {
  trigger::TriggerSpec spec;
  spec.combine = combine;
  for (std::size_t i = 0; i < perChannel.size(); ++i) {
    if (perChannel[i] == trigger::MatchType::kNone) {
      continue;
    }
    spec.matches.push_back(trigger::ChannelMatch{static_cast<std::uint32_t>(i), perChannel[i]});
  }
  return spec;
}

trigger::MatchType toggleChannelTrigger(trigger::MatchType current, trigger::MatchType clicked) {
  return current == clicked ? trigger::MatchType::kNone : clicked;
}

}

/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <trigger_spec.h>

#include <cstdint>
#include <vector>

namespace bitoscope::waveform_view {

trigger::TriggerSpec buildTriggerSpec(const std::vector<trigger::MatchType>& perChannel,
                                       trigger::Combine combine = trigger::Combine::kAnd);

trigger::MatchType toggleChannelTrigger(trigger::MatchType current, trigger::MatchType clicked);

}

/*
 * SPDX-License-Identifier: MIT
 */

#include "capture_settings.h"

#include <algorithm>
#include <limits>

#include "capture_plan.h"
#include "ring_config.h"
#include "trigger_split.h"
#include "triggered_window.h"

namespace bitoscope::gui_capture {

namespace {

using bitoscope::host::pipe::LaCapabilities;

std::vector<uint64_t> sortedRates(const LaCapabilities& capabilities) {
  std::vector<uint64_t> rates = capabilities.supported_sample_rates_milli_hz;
  std::sort(rates.begin(), rates.end());
  rates.erase(std::unique(rates.begin(), rates.end()), rates.end());
  return rates;
}

bool listed(const LaCapabilities& capabilities, uint64_t rate) {
  const auto& table = capabilities.supported_sample_rates_milli_hz;
  return std::find(table.begin(), table.end(), rate) != table.end();
}

uint64_t defaultRate(const LaCapabilities& capabilities, uint32_t selectedChannels) {
  if (capabilities.default_sample_rate_milli_hz != 0) return capabilities.default_sample_rate_milli_hz;
  const std::vector<uint64_t> rates = sortedRates(capabilities);
  if (rates.empty()) return 0;
  const std::optional<uint64_t> ceiling = rateCeiling(capabilities, selectedChannels);
  uint64_t best = rates.front();
  for (uint64_t rate : rates) {
    if (!ceiling || rate <= *ceiling) best = rate;
  }
  return best;
}

}

std::expected<LaCapabilities, std::string> capabilitiesOf(const bitoscope::ipc::DeviceInfo& device) {
  for (const bitoscope::ipc::FacetInfo& facet : device.facets) {
    if (facet.name == bitoscope::ipc::la::kFacetName) {
      return bitoscope::host::pipe::LaAcquisitionClient::parseCapabilities(facet.info);
    }
  }
  return std::unexpected("device '" + device.id + "' has no " + std::string(bitoscope::ipc::la::kFacetName) +
                         " facet: it cannot capture");
}

CaptureSettings defaultSettings(const LaCapabilities& capabilities) {
  CaptureSettings settings;
  settings.channels.assign(capabilities.num_channels, true);
  settings.trigger.assign(capabilities.num_channels, bitoscope::trigger::MatchType::kNone);
  settings.sampleRateMilliHz = defaultRate(capabilities, capabilities.num_channels);
  return settings;
}

CaptureSettings fitToDevice(CaptureSettings settings, const LaCapabilities& capabilities) {
  settings.channels.resize(capabilities.num_channels, false);
  settings.trigger.resize(capabilities.num_channels, bitoscope::trigger::MatchType::kNone);
  if (!settings.channels.empty() && selectedChannelCount(settings) == 0) settings.channels[0] = true;
  if (!capabilities.supported_sample_rates_milli_hz.empty() && !listed(capabilities, settings.sampleRateMilliHz)) {
    settings.sampleRateMilliHz = defaultRate(capabilities, selectedChannelCount(settings));
  }
  return settings;
}

uint32_t selectedChannelCount(const CaptureSettings& settings) {
  return static_cast<uint32_t>(std::count(settings.channels.begin(), settings.channels.end(), true));
}

std::vector<RateChoice> rateChoices(const LaCapabilities& capabilities, uint32_t selectedChannels) {
  const std::optional<uint64_t> ceiling = rateCeiling(capabilities, selectedChannels);
  std::vector<RateChoice> choices;
  for (uint64_t rate : sortedRates(capabilities)) {
    choices.push_back(RateChoice{rate, !ceiling || rate <= *ceiling});
  }
  return choices;
}

std::optional<uint64_t> rateCeiling(const LaCapabilities& capabilities, uint32_t selectedChannels) {
  const auto& ceilings = capabilities.rate_ceilings_milli_hz;
  if (selectedChannels < 1 || selectedChannels > ceilings.size()) return std::nullopt;
  return ceilings[selectedChannels - 1];
}

std::expected<TriggerRange, std::string> triggerRange(uint64_t lengthSamples, uint32_t preTriggerPercent) {
  if (preTriggerPercent > 100) {
    return std::unexpected("pre-trigger ratio " + std::to_string(preTriggerPercent) + " is outside 0-100");
  }
  if (lengthSamples == 0) return std::unexpected("capture of zero samples has nothing to trigger in");
  const bitoscope::trigger::PrePostSplit split = bitoscope::trigger::splitPreTrigger(
      lengthSamples, bitoscope::trigger::preTriggerSamplesForRatio(lengthSamples, preTriggerPercent));
  return TriggerRange{split.pre, split.post};
}

std::expected<CaptureRequest, SettingsProblem> buildCaptureRequest(
    const CaptureSettings& settings, const LaCapabilities& capabilities, uint32_t hardwareThreads,
    const std::map<std::string, std::vector<ipc::props::PropertyValue>>& facetProperties) {
  using Kind = SettingsProblem::Kind;
  CaptureRequest built;
  uint32_t mask = 0;
  for (uint32_t c = 0; c < settings.channels.size(); ++c) {
    if (!settings.channels[c]) continue;
    if (c >= capabilities.num_channels || c >= 32) {
      return std::unexpected(SettingsProblem{Kind::TooManyChannels, c});
    }
    mask |= uint32_t{1} << c;
    built.deviceChannels.push_back(c);
  }
  const uint32_t channels = static_cast<uint32_t>(built.deviceChannels.size());
  if (channels == 0) return std::unexpected(SettingsProblem{Kind::NoChannel});
  const uint64_t rate = settings.sampleRateMilliHz;
  if (rate == 0) return std::unexpected(SettingsProblem{Kind::NoRate});
  if (!capabilities.supported_sample_rates_milli_hz.empty() && !listed(capabilities, rate)) {
    return std::unexpected(SettingsProblem{Kind::RateNotOffered, 0, channels, rate});
  }
  if (const auto ceiling = rateCeiling(capabilities, channels); ceiling && rate > *ceiling) {
    return std::unexpected(SettingsProblem{Kind::RateAboveCeiling, 0, channels, rate, *ceiling});
  }
  if (settings.lengthSamples == 0) return std::unexpected(SettingsProblem{Kind::NoLength});

  bitoscope::trigger::TriggerSpec spec;
  for (uint32_t c = 0; c < settings.trigger.size(); ++c) {
    const bitoscope::trigger::MatchType type = settings.trigger[c];
    if (type == bitoscope::trigger::MatchType::kNone) continue;
    const auto at = std::find(built.deviceChannels.begin(), built.deviceChannels.end(), c);
    if (at == built.deviceChannels.end()) return std::unexpected(SettingsProblem{Kind::TriggerNotCaptured, c});
    spec.matches.push_back(
        bitoscope::trigger::ChannelMatch{static_cast<uint32_t>(at - built.deviceChannels.begin()), type});
  }

  bitoscope::acquisition::AcquireRequest& r = built.request;
  r.channel_mask = mask;
  r.sample_rate_milli_hz = rate;
  r.total_samples = settings.lengthSamples;
  r.pyramid = true;
  r.live_tail = true;
  r.keep_on_memory_full = true;
  const uint64_t bytes_per_second = bitoscope::acquisition::captureBytesPerSecond(channels, rate);
  r.pack_workers = bitoscope::acquisition::defaultPackWorkers(hardwareThreads, bytes_per_second);
  r.clearing_workers = bitoscope::acquisition::defaultClearingWorkers(hardwareThreads);
  for (const auto& [facet, values] : facetProperties) {
    if (facet == bitoscope::ipc::la::kFacetName) {
      r.properties = values;
    } else {
      r.other_facet_properties[facet] = values;
    }
  }
  built.requestedSamples = settings.lengthSamples;
  // WHY: GUI sizes windows by time for every capture, triggered or not (owner decision); the CLI does not for untriggered.
  built.samplesPerWindow = bitoscope::capture::triggeredWindowSamples(rate);
  if (!spec.matches.empty()) {
    auto range = triggerRange(settings.lengthSamples, settings.preTriggerPercent);
    if (!range) return std::unexpected(SettingsProblem{Kind::BadPreTrigger});
    spec.combine = bitoscope::trigger::Combine::kAnd;
    spec.pre_trigger_samples = range->pre;
    spec.post_trigger_samples = range->post;
    spec.timeout_s = 0;
    r.trigger = spec;
    built.requestedSamples = range->pre + range->post;
  }
  return built;
}

Alternatives alternativesThatStore(const CaptureSettings& settings, const LaCapabilities& capabilities,
                                   const std::function<bool(const CaptureSettings&)>& storesSomething) {
  Alternatives found;
  std::vector<uint32_t> selected;
  for (uint32_t c = 0; c < settings.channels.size(); ++c) {
    if (settings.channels[c]) selected.push_back(c);
  }
  for (size_t count = selected.size(); count-- > 1;) {
    CaptureSettings candidate = settings;
    for (size_t i = count; i < selected.size(); ++i) {
      candidate.channels[selected[i]] = false;
      if (selected[i] < candidate.trigger.size()) candidate.trigger[selected[i]] = bitoscope::trigger::MatchType::kNone;
    }
    if (storesSomething(candidate)) {
      found.channels = static_cast<uint32_t>(count);
      break;
    }
  }
  const std::optional<uint64_t> ceiling = rateCeiling(capabilities, static_cast<uint32_t>(selected.size()));
  std::vector<uint64_t> rates = sortedRates(capabilities);
  for (auto it = rates.rbegin(); it != rates.rend(); ++it) {
    if (*it >= settings.sampleRateMilliHz || (ceiling && *it > *ceiling)) continue;
    CaptureSettings candidate = settings;
    candidate.sampleRateMilliHz = *it;
    if (storesSomething(candidate)) {
      found.rateMilliHz = *it;
      break;
    }
  }
  for (auto it = kLengthChoices.rbegin(); it != kLengthChoices.rend(); ++it) {
    if (*it >= settings.lengthSamples) continue;
    CaptureSettings candidate = settings;
    candidate.lengthSamples = *it;
    if (storesSomething(candidate)) {
      found.lengthSamples = *it;
      break;
    }
  }
  return found;
}

std::string channelDisplayName(const std::vector<std::string>& presetNames, std::uint32_t deviceChannel) {
  if (deviceChannel < presetNames.size() && !presetNames[deviceChannel].empty()) {
    return presetNames[deviceChannel];
  }
  return std::to_string(deviceChannel);
}

}

/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <array>
#include <cstdint>
#include <expected>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "acquisition.h"
#include "ipc_messages.h"
#include "ipc_properties.h"
#include "la_pipe_client.h"
#include "trigger_spec.h"

namespace bitoscope::gui_capture {

// WHY: Keep kLengthChoices ascending: alternativesThatStore() walks it from the end.
inline constexpr std::array<uint64_t, 9> kLengthChoices{
    100, 1'000, 10'000, 100'000, 1'000'000, 10'000'000, 100'000'000, 1'000'000'000, 10'000'000'000};
inline constexpr uint64_t kDefaultLengthSamples = 1'000'000;

inline constexpr uint32_t kDefaultPreTriggerPercent = 0;

struct CaptureSettings {
  std::vector<bool> channels;
  uint64_t sampleRateMilliHz = 0;
  uint64_t lengthSamples = kDefaultLengthSamples;
  std::vector<bitoscope::trigger::MatchType> trigger;
  uint32_t preTriggerPercent = kDefaultPreTriggerPercent;

  bool operator==(const CaptureSettings&) const = default;
};

std::expected<bitoscope::host::pipe::LaCapabilities, std::string> capabilitiesOf(
    const bitoscope::ipc::DeviceInfo& device);

CaptureSettings defaultSettings(const bitoscope::host::pipe::LaCapabilities& capabilities);

CaptureSettings fitToDevice(CaptureSettings settings, const bitoscope::host::pipe::LaCapabilities& capabilities);

uint32_t selectedChannelCount(const CaptureSettings& settings);

struct RateChoice {
  uint64_t milliHz = 0;
  bool allowed = true;
  bool operator==(const RateChoice&) const = default;
};
std::vector<RateChoice> rateChoices(const bitoscope::host::pipe::LaCapabilities& capabilities,
                                    uint32_t selectedChannels);

std::optional<uint64_t> rateCeiling(const bitoscope::host::pipe::LaCapabilities& capabilities,
                                    uint32_t selectedChannels);

struct TriggerRange {
  uint64_t pre = 0;
  uint64_t post = 0;
  bool operator==(const TriggerRange&) const = default;
};
std::expected<TriggerRange, std::string> triggerRange(uint64_t lengthSamples, uint32_t preTriggerPercent);

struct SettingsProblem {
  enum class Kind {
    NoChannel,
    TooManyChannels,
    NoRate,
    RateNotOffered,
    RateAboveCeiling,
    NoLength,
    TriggerNotCaptured,
    BadPreTrigger,
  };
  Kind kind = Kind::NoChannel;
  uint32_t channel = 0;
  uint32_t channels = 0;
  uint64_t rate = 0;
  uint64_t ceiling = 0;
  bool operator==(const SettingsProblem&) const = default;
};

struct CaptureRequest {
  bitoscope::acquisition::AcquireRequest request;
  uint32_t samplesPerWindow = 0;
  std::vector<uint32_t> deviceChannels;
  uint64_t requestedSamples = 0;
};

std::expected<CaptureRequest, SettingsProblem> buildCaptureRequest(
    const CaptureSettings& settings, const bitoscope::host::pipe::LaCapabilities& capabilities,
    uint32_t hardwareThreads,
    const std::map<std::string, std::vector<ipc::props::PropertyValue>>& facetProperties = {});

struct Alternatives {
  std::optional<uint32_t> channels;
  std::optional<uint64_t> rateMilliHz;
  std::optional<uint64_t> lengthSamples;
  bool operator==(const Alternatives&) const = default;
};
Alternatives alternativesThatStore(const CaptureSettings& settings,
                                   const bitoscope::host::pipe::LaCapabilities& capabilities,
                                   const std::function<bool(const CaptureSettings&)>& storesSomething);

std::string channelDisplayName(const std::vector<std::string>& presetNames, std::uint32_t deviceChannel);

}

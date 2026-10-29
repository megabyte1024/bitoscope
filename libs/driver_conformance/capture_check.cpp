/*
 * SPDX-License-Identifier: MIT
 */

#include "capture_check.h"

#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <expected>
#include <memory>
#include <thread>
#include <utility>
#include <variant>

#include "acquisition.h"
#include "capture_allocator.h"
#include "capture_plan.h"
#include "device_properties.h"
#include "facet_access.h"
#include "pipe_plugin_host.h"
#include "read_samples.h"
#include "system_memory_probe.h"
#include "test_pattern_check.h"

namespace bitoscope::driver_conformance {

namespace {

namespace pd = bitoscope::capture::plan_detail;

constexpr std::array kCaptureRules = {
    Rule{"sample-limit", true,
         "captures of 1, 63, 64, 65 and 1'000'003 samples on every channel end with exactly that many, whole"},
    Rule{"channel-masks", true,
         "captures on first N channels for every N, on top channel alone and on every other channel work, whole"},
    Rule{"stop", true, "STOP during capture is answered within host's wait and capture keeps what arrived, whole"},
    Rule{"content", true, "with test pattern on, every captured channel carries its counter bit for bit, in order"},
};

constexpr std::array<uint64_t, 5> kLimits = {1, 63, 64, 65, 1'000'003};
constexpr uint64_t kMaskSamples = 65'537;
constexpr uint64_t kStopSamples = uint64_t{1} << 26;
constexpr uint64_t kLowestCheckedRateMilliHz = 1'000'000'000;
constexpr std::chrono::milliseconds kOpenWait{30'000};

std::string channelList(uint32_t mask) {
  std::string text;
  for (uint32_t c = 0; c < 32; ++c) {
    if (((mask >> c) & 1u) == 0) continue;
    uint32_t last = c;
    while (last + 1 < 32 && ((mask >> (last + 1)) & 1u) != 0) ++last;
    text += (text.empty() ? "" : ", ") + std::to_string(c) + (last > c ? "-" + std::to_string(last) : "");
    c = last;
  }
  return (std::popcount(mask) == 1 ? "channel " : "channels ") + text;
}

std::vector<uint32_t> channelsOf(uint32_t mask) {
  std::vector<uint32_t> channels;
  for (uint32_t c = 0; c < 32; ++c) {
    if (((mask >> c) & 1u) != 0) channels.push_back(c);
  }
  return channels;
}

uint32_t firstChannels(uint32_t count) { return count >= 32 ? 0xFFFF'FFFFu : (uint32_t{1} << count) - 1; }

std::vector<uint32_t> maskShapes(uint32_t numChannels) {
  std::vector<uint32_t> masks;
  const auto add = [&](uint32_t mask) {
    if (std::ranges::find(masks, mask) == masks.end()) masks.push_back(mask);
  };
  for (uint32_t n = 1; n <= numChannels; ++n) add(firstChannels(n));
  add(uint32_t{1} << (numChannels - 1));
  add(firstChannels(numChannels) & 0x5555'5555u);
  return masks;
}

std::optional<uint64_t> checkedRate(const host::pipe::LaCapabilities& caps) {
  const auto allowed = [&](uint64_t rate) {
    const size_t counts = (std::min)(static_cast<size_t>(caps.num_channels), caps.rate_ceilings_milli_hz.size());
    for (size_t k = 0; k < counts; ++k) {
      if (rate > caps.rate_ceilings_milli_hz[k]) return false;
    }
    return true;
  };
  std::optional<uint64_t> lowestFast;
  std::optional<uint64_t> highest;
  for (const uint64_t rate : caps.supported_sample_rates_milli_hz) {
    if (!allowed(rate)) continue;
    if (rate >= kLowestCheckedRateMilliHz && (!lowestFast || rate < *lowestFast)) lowestFast = rate;
    if (!highest || rate > *highest) highest = rate;
  }
  return lowestFast ? lowestFast : highest;
}

bool listsTestPattern(const host::pipe::PluginDevice& device) {
  for (const ipc::FacetInfo& facet : device.device.facets) {
    if (facet.name != ipc::la::kFacetName) continue;
    const ipc::props::Property* property = device_properties::findProperty(facet.properties, kTestPatternProperty);
    return property != nullptr && std::holds_alternative<ipc::props::BooleanSpec>(property->type);
  }
  return false;
}

struct Rules {
  RuleResult limit{"sample-limit", true, Verdict::pass, {}, {}};
  RuleResult masks{"channel-masks", true, Verdict::pass, {}, {}};
  RuleResult stop{"stop", true, Verdict::pass, {}, {}};
  RuleResult content{"content", true, Verdict::pass, {}, {}};

  std::vector<RuleResult> all() const { return {limit, masks, stop, content}; }

  void skipAll(const std::string& reason) {
    for (RuleResult* rule : {&limit, &masks, &stop, &content}) {
      rule->verdict = Verdict::skip;
      rule->skipReason = reason;
    }
  }

  void failAll(const std::string& problem) {
    for (RuleResult* rule : {&limit, &masks, &stop, &content}) fail(*rule, problem);
  }

  static void fail(RuleResult& rule, std::string problem) {
    rule.verdict = Verdict::fail;
    rule.problems.push_back(std::move(problem));
  }
};

class Captures {
 public:
  Captures(host::PipePluginHost& host, host::pipe::PluginDevice device, uint64_t rate, bool testPattern,
           const CaptureCheckOptions& options)
      : host_(host), device_(std::move(device)), rate_(rate), testPattern_(testPattern), options_(options) {}

  std::string describe(uint32_t mask, uint64_t samples) const {
    return "capture of " + pd::withThousands(samples) + (samples == 1 ? " sample on " : " samples on ") +
           channelList(mask) + " at " + pd::formatRate(rate_);
  }

  std::expected<acquisition::AcquireResult, std::string> run(uint32_t mask, uint64_t samples, bool stopAtFirstChunk) {
    acquisition::AcquireRequest request;
    request.channel_mask = mask;
    request.sample_rate_milli_hz = rate_;
    request.total_samples = samples;
    request.start_retries = 0;
    if (testPattern_) request.properties.push_back(ipc::props::PropertyValue{std::string(kTestPatternProperty), true});

    bitoscope::capture::SystemMemoryProbe probe;
    bitoscope::capture::SystemBlockSource blocks;
    acquisition::AcquisitionSeams seams{probe, blocks};
    seams.now_ms = [] {
      return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
                                       std::chrono::steady_clock::now().time_since_epoch())
                                       .count());
    };
    seams.sleep_ms = [](uint64_t ms) { std::this_thread::sleep_for(std::chrono::milliseconds(ms)); };
    if (options_.inactivityTimeoutMsForTest) seams.inactivity_timeout_ms = *options_.inactivityTimeoutMsForTest;
    const std::shared_ptr<host::PluginConnection> connection = host_.connection(device_.plugin_id);
    seams.plugin_closed = [connection]() -> std::optional<std::string> {
      return connection ? connection->closedReason() : std::optional<std::string>("driver is not open");
    };
    seams.device_lost = [connection]() -> std::optional<std::string> {
      return connection ? connection->deviceLostReason() : std::nullopt;
    };
    std::shared_ptr<bitoscope::capture::CaptureSession> session;
    seams.on_session = [&session](std::shared_ptr<bitoscope::capture::CaptureSession> attached) {
      if (attached) session = std::move(attached);
    };
    if (stopAtFirstChunk) seams.stop_requested = [&session] { return session && session->chunksReceived() > 0; };

    acquisition::AcquisitionEvents events;
    return acquisition::acquire(request, device_, host_, seams, events);
  }

  std::optional<std::string> contentDifference(uint32_t mask, const acquisition::AcquireResult& result) const {
    const uint64_t count = result.summary.stored_samples;
    std::vector<uint8_t> bits(static_cast<size_t>((count + 7) / 8));
    const std::vector<uint32_t> channels = channelsOf(mask);
    for (uint32_t lane = 0; lane < channels.size(); ++lane) {
      if (auto read = bitoscope::capture::readSamples(result.session->buffer(), lane, 0, count, bits); !read) {
        return "stored capture could not be read: " + read.error();
      }
      if (auto difference = testPatternDifference(channels[lane], bits, count)) return difference;
    }
    return std::nullopt;
  }

 private:
  host::PipePluginHost& host_;
  host::pipe::PluginDevice device_;
  uint64_t rate_;
  bool testPattern_;
  const CaptureCheckOptions& options_;
};

void checkContent(Rules& rules, const Captures& captures, uint32_t mask, const acquisition::AcquireResult& result,
                  uint64_t& compared) {
  ++compared;
  if (auto difference = captures.contentDifference(mask, result)) {
    Rules::fail(rules.content, captures.describe(mask, result.requested_samples) + ": " + *difference);
  }
}

}

std::span<const Rule> captureRules() { return kCaptureRules; }

CaptureCheck runCaptureRules(const CaptureCheckOptions& options) {
  Rules rules;
  CaptureCheck check;
  host::PipePluginHostOptions hostOptions;
  hostOptions.pluginDirectory = options.driver.parent_path();
  hostOptions.onStderrLine = options.onDriverStderr ? options.onDriverStderr : [](std::string_view) {};
  hostOptions.startTimeout = kOpenWait;
  host::PipePluginHost host(std::move(hostOptions));
  const std::string driver = options.driver.stem().string();

  auto list = host.listDevices(driver);
  if (!list) {
    rules.skipAll("no device list to capture from: " + list.error());
    check.results = rules.all();
    return check;
  }
  const auto usable = std::ranges::find_if(list->devices, [](const ipc::DeviceInfo& device) {
    return device.unavailable == ipc::UnavailableReason::none &&
           std::ranges::any_of(device.facets, [](const ipc::FacetInfo& f) { return f.name == ipc::la::kFacetName; });
  });
  if (usable == list->devices.end()) {
    rules.skipAll("no available device with logic analyzer to capture from");
    check.results = rules.all();
    return check;
  }

  auto opened = host.open(driver, usable->id);
  const std::shared_ptr<host::PluginConnection> connection = opened ? host.connection(*opened) : nullptr;
  const std::optional<ipc::DeviceInfo> handshake = connection ? connection->device() : std::nullopt;
  if (!handshake) {
    rules.failAll("device " + usable->id + " could not be opened" + (opened ? "" : ": " + opened.error().message));
    check.results = rules.all();
    return check;
  }
  host::pipe::PluginDevice device{*opened, *handshake};
  auto la = acquisition::makeLaClient(device, host);
  if (!la) {
    rules.failAll("device " + usable->id + ": " + la.error());
    check.results = rules.all();
    return check;
  }
  const host::pipe::LaCapabilities caps = la->capabilities();
  const std::optional<uint64_t> rate = checkedRate(caps);
  if (caps.num_channels == 0 || caps.num_channels > 32 || !rate) {
    rules.failAll("device " + usable->id + " lists no sample rate that every channel count allows");
    check.results = rules.all();
    return check;
  }
  const bool testPattern = listsTestPattern(device);
  check.target = "capturing from device " + usable->id + " at " + pd::formatRate(*rate);
  Captures captures(host, device, *rate, testPattern, options);
  uint64_t compared = 0;
  const uint32_t allChannels = firstChannels(caps.num_channels);

  const auto captureWhole = [&](RuleResult& rule, uint32_t mask, uint64_t samples) {
    auto result = captures.run(mask, samples, false);
    if (!result) return Rules::fail(rule, captures.describe(mask, samples) + ": " + result.error());
    if (testPattern) checkContent(rules, captures, mask, *result, compared);
  };
  for (const uint64_t samples : kLimits) captureWhole(rules.limit, allChannels, samples);
  for (const uint32_t mask : maskShapes(caps.num_channels)) captureWhole(rules.masks, mask, kMaskSamples);

  const uint64_t stopSamples = options.stopSamplesForTest.value_or(kStopSamples);
  auto stopped = captures.run(allChannels, stopSamples, true);
  if (!stopped) {
    Rules::fail(rules.stop, captures.describe(allChannels, stopSamples) + ": " + stopped.error());
  } else if (stopped->summary.stored_samples >= stopSamples) {
    Rules::fail(rules.stop, captures.describe(allChannels, stopSamples) +
                                ": capture ended by itself before STOP was sent; STOP not checked");
  } else if (testPattern) {
    checkContent(rules, captures, allChannels, *stopped, compared);
  }

  if (!testPattern) {
    rules.content.verdict = Verdict::skip;
    rules.content.skipReason = "content not checked: driver offers no test pattern (boolean property " +
                               std::string(kTestPatternProperty) + ")";
  } else if (compared == 0) {
    rules.content.verdict = Verdict::skip;
    rules.content.skipReason = "content not checked: no capture completed";
  }
  check.results = rules.all();
  return check;
}

}

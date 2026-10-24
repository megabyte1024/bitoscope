/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <chrono>
#include <cstdint>
#include <expected>
#include <functional>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include "bitoscope_driver_cpp.h"
#include "demo_config.h"
#include "demo_stream.h"
#include "pattern_signals.h"
#include "random_signals.h"

namespace demo_plugin {

struct DemoHooks {
  std::function<std::chrono::steady_clock::time_point()> now;
  std::function<void(std::chrono::steady_clock::time_point)> wait_until;
  std::function<void(uint64_t first_sample, uint32_t samples)> on_chunk_written;
};

inline constexpr std::string_view kDemoDeviceId = "demo-0";
inline constexpr std::string_view kDemoDriverName = "Demo";
inline constexpr std::string_view kDemoDriverVersion = "0.1.0";
inline constexpr std::string_view kDemoDeviceName = "Demo signals";

std::vector<bitoscope::plugin::PropertyNode> demoProperties();

bitoscope::plugin::LogicAnalyzerInfo demoAnalyzerInfo();

std::expected<bitoscope::sim::DemoFacetConfig, std::string> demoConfigFrom(
    const std::map<std::string, bitoscope::plugin::PropertyValue, std::less<>>& values);

class DemoAnalyzer final : public bitoscope::plugin::LogicAnalyzer {
 public:
  explicit DemoAnalyzer(DemoHooks hooks = {});

  std::expected<void, std::string> prepare(const bitoscope::plugin::Capture& capture,
                                         bitoscope::plugin::Sink& sink) override;
  std::expected<void, std::string> capture(const bitoscope::plugin::Capture& capture,
                                           bitoscope::plugin::Sink& sink) override;

 private:
  using Stream = std::variant<bitoscope::sim::DemoStream, bitoscope::sim::RandomStream, bitoscope::sim::PatternStream>;

  enum class Written { more, last, failed };

  Written writeChunk(bitoscope::plugin::Sink& sink, uint64_t first, uint32_t n);
  void warnBehind(bitoscope::plugin::Sink& sink) const;
  std::string sourceName() const;
  std::chrono::steady_clock::time_point now() const;

  const DemoHooks hooks_;
  // WHY: captureConfig_ is written by prepare() only while no capture thread runs; capture() reads it without a
  // lock.
  bitoscope::sim::DemoFacetConfig captureConfig_;
  std::optional<Stream> stream_;
  uint64_t rateHz_ = 0;
  uint64_t limit_ = 0;
  uint32_t chunkSamples_ = 0;
};

class DemoSession final : public bitoscope::plugin::Session {
 public:
  explicit DemoSession(DemoHooks hooks = {});

  bitoscope::plugin::LogicAnalyzer* logicAnalyzer() override;

 private:
  DemoAnalyzer analyzer_;
};

bitoscope::plugin::DriverDescription demoDriver(DemoHooks hooks = {});

}

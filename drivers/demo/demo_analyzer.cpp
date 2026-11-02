/*
 * SPDX-License-Identifier: MIT
 */

#include "demo_analyzer.h"

#include <algorithm>
#include <array>
#include <bit>
#include <memory>
#include <utility>

#include "demo_pacer.h"
#include "display_text.h"

namespace sim = bitoscope::sim;
using bitoscope::plugin::Capture;
using bitoscope::plugin::Layout;
using bitoscope::plugin::PropertyNode;
using bitoscope::plugin::Sink;

namespace demo_driver {

namespace {

Layout layoutOf(sim::WireEncoding e) {
  return e == sim::WireEncoding::SampleMajor ? Layout::sampleMajor : Layout::channelMajor;
}

}

DemoAnalyzer::DemoAnalyzer(DemoHooks hooks) : hooks_(std::move(hooks)) {}


namespace {

using bitoscope::plugin::BooleanType;
using bitoscope::plugin::EnumerationType;
using bitoscope::plugin::IntegerType;
using bitoscope::plugin::Property;
using bitoscope::plugin::Unit;

const std::vector<std::string> kLayouts{"channel_major", "sample_major"};
const std::vector<std::string> kPacings{"realtime", "unpaced"};
const std::vector<std::string> kSources{"structured", "random", "pattern", "noise"};

PropertyNode node(std::string id, std::string name, bitoscope::plugin::PropertyType type, Unit unit, std::string help) {
  Property p;
  p.id = std::move(id);
  p.name = std::move(name);
  p.type = std::move(type);
  p.unit = unit;
  p.changeableDuringCapture = false;
  p.help = std::move(help);
  return PropertyNode{std::move(p)};
}

std::expected<uint32_t, std::string> itemOf(const Capture& capture, std::string_view id, size_t items) {
  const uint32_t item = capture.enumIndex(id);
  if (item >= items) return std::unexpected(std::string(id) + ": item " + std::to_string(item) + " does not exist");
  return item;
}

std::vector<bitoscope::plugin::ChannelPreset> demoChannelPresets() {
  const std::array<std::string_view, sim::kDemoChannels> names{
      "UART 115200 8N1", "UART 57600 8E1", "UART 38400 8N2",
      "SPI A CS", "SPI A SCLK mode 0", "SPI A MOSI", "SPI A MISO",
      "SPI B CS", "SPI B SCLK mode 3", "SPI B MOSI", "SPI B MISO",
      "I2C 0 SCL", "I2C 0 SDA", "I2C 1 SCL", "I2C 1 SDA",
      "Strobe 1024 us",
      "Counter bit 0", "Counter bit 1", "Counter bit 2", "Counter bit 3",
      "Counter bit 4", "Counter bit 5", "Counter bit 6", "Counter bit 7",
      "PWM 12.5 %", "PWM 25 %", "PWM 50 %", "PWM 75 %",
      "Clock 31.25 kHz", "Clock 15.625 kHz", "Clock 7.8125 kHz", "Clock 1.953125 kHz",
  };
  std::vector<bitoscope::plugin::ChannelPreset> presets;
  for (uint32_t c = 0; c < sim::kDemoChannels; ++c) {
    presets.push_back({c, bitoscope::display_text::displayText(names[c]), {}});
  }
  return presets;
}

}

std::vector<PropertyNode> demoProperties() {
  const sim::DemoFacetConfig d;
  return {
      node("layout", "Layout", EnumerationType{kLayouts, {}, 0, 0}, Unit::none,
           "How capture's samples are laid out: channel_major (each channel's bytes together) or sample_major"),
      node("pacing", "Pacing", EnumerationType{kPacings, {}, 0, 0}, Unit::none,
           "realtime: each chunk is written once its signal has happened, as device that cannot wait; unpaced: as "
           "fast as host takes it"),
      node("chunk_samples", "Chunk samples", IntegerType{0, int64_t{sim::kDemoMaxChunkSamples}, d.chunk_samples},
           Unit::samples, "Samples per chunk, multiple of 8; 0: as many as fill ring slot"),
      node("source", "Source", EnumerationType{kSources, {}, 0, 0}, Unit::none,
           bitoscope::display_text::displayText(
               "structured: UART, SPI, I2C and PWM traffic; random: seeded generator; pattern: seeded pulse train per "
               "channel; noise: seeded incompressible bytes per channel")),
      node("seed", "Seed", IntegerType{0, INT64_MAX, static_cast<int64_t>(d.seed)}, Unit::none,
           "Seed of random, pattern and noise sources"),
      node("marker", "Marker", BooleanType{d.marker, bitoscope::plugin::BooleanStyle::onOff}, Unit::none,
           "One rising edge on marker channel at marker position (pattern and noise only)"),
      node("marker_at", "Marker position",
           IntegerType{1, static_cast<int64_t>(sim::kMaxMarkerSample), static_cast<int64_t>(d.marker_at)},
           Unit::samples, "Sample marker rises at"),
      node("marker_channel", "Marker channel", IntegerType{0, 31, d.marker_channel}, Unit::none,
           "Physical channel marker is on"),
      node("test_pattern", "Test pattern", BooleanType{d.test_pattern, bitoscope::plugin::BooleanStyle::onOff},
           Unit::none, "Counter: physical channel c carries bit c of sample number; overrides source"),
  };
}

std::expected<sim::DemoFacetConfig, std::string> demoConfigFrom(const Capture& capture) {
  sim::DemoFacetConfig config;
  auto layout = itemOf(capture, "layout", kLayouts.size());
  if (!layout) return std::unexpected(layout.error());
  config.encoding = *layout == 1 ? sim::WireEncoding::SampleMajor : sim::WireEncoding::ChannelMajor;
  auto pacing = itemOf(capture, "pacing", kPacings.size());
  if (!pacing) return std::unexpected(pacing.error());
  config.realtime = *pacing == 0;
  auto source = itemOf(capture, "source", kSources.size());
  if (!source) return std::unexpected(source.error());
  config.source = static_cast<sim::DemoSource>(*source);
  config.chunk_samples = static_cast<uint32_t>(capture.integer("chunk_samples"));
  config.seed = static_cast<uint64_t>(capture.integer("seed"));
  config.marker = capture.boolean("marker");
  config.marker_at = static_cast<uint64_t>(capture.integer("marker_at"));
  config.marker_channel = static_cast<uint32_t>(capture.integer("marker_channel"));
  config.test_pattern = capture.boolean("test_pattern");
  return config;
}

bitoscope::plugin::LogicAnalyzerInfo demoAnalyzerInfo() {
  bitoscope::plugin::LogicAnalyzerInfo info;
  info.numChannels = sim::kDemoChannels;
  for (const uint64_t mhz : {1, 2, 5, 10, 20, 50, 100, 250, 500}) {
    info.sampleRatesMilliHz.push_back(mhz * 1'000'000ull * 1000);
  }
  info.properties = demoProperties();
  info.channelPresets = demoChannelPresets();
  return info;
}

std::expected<void, std::string> DemoAnalyzer::prepare(const Capture& capture, Sink& sink) {
  const auto refuse = [](std::string why) -> std::expected<void, std::string> {
    bitoscope_log(BITOSCOPE_LOG_ERROR, "START refused: %s", why.c_str());
    return std::unexpected(std::move(why));
  };
  if (capture.buffered && capture.sampleLimit == 0) return refuse("buffered mode requires nonzero sample_limit");
  const uint64_t hz = capture.sampleRateMilliHz / 1000;
  const auto read = demoConfigFrom(capture);
  if (!read) return refuse(read.error());
  const sim::DemoFacetConfig config = *read;
  if (auto checked = sim::checkDemoConfig(config); !checked) return refuse(checked.error());

  if (!sink.setDeviceBytes({layoutOf(config.encoding), capture.channelMask, 0})) {
    return refuse("SDK refused device bytes");
  }
  const uint64_t perSlot = sink.maxChunkSamples();
  auto chunk = sim::ringChunkSamples(config, hz, sink.sampleBytes(perSlot), capture.numCapturedChannels);
  if (!chunk) return refuse(chunk.error());
  const auto marker = sim::markerForCapture(config);
  if (!marker) return refuse(marker.error());

  std::optional<Stream> stream;
  if (config.test_pattern) {
    auto s = sim::CounterStream::create(capture.channelMask, config.encoding);
    if (!s) return refuse(s.error());
    stream.emplace(std::move(*s));
  } else if (config.source == sim::DemoSource::Random) {
    auto s = sim::RandomStream::create(config.seed, capture.channelMask, config.encoding, *chunk);
    if (!s) return refuse(s.error());
    stream.emplace(std::move(*s));
  } else if (config.source == sim::DemoSource::Pattern || config.source == sim::DemoSource::Noise) {
    auto s = config.source == sim::DemoSource::Noise
                 ? sim::PatternStream::createNoise(config.seed, capture.channelMask, config.encoding, *chunk, *marker)
                 : sim::PatternStream::create(config.seed, capture.channelMask, config.encoding, *chunk, *marker);
    if (!s) return refuse(s.error());
    stream.emplace(std::move(*s));
  } else {
    auto s = sim::DemoStream::create(hz, capture.channelMask, config.encoding, *chunk);
    if (!s) return refuse(s.error());
    stream.emplace(std::move(*s));
  }
  captureConfig_ = config;
  stream_ = std::move(stream);
  rateHz_ = hz;
  limit_ = capture.sampleLimit;
  chunkSamples_ = *chunk;

  const std::string line =
      "demo capture started: " + std::to_string(hz) + " Hz, " + std::to_string(capture.numCapturedChannels) + " channel(s), " +
      (config.encoding == sim::WireEncoding::ChannelMajor ? "channel-major, " : "sample-major, ") +
      (config.realtime ? "realtime, " : "unpaced, ") + std::to_string(*chunk) + " samples per chunk, " + sourceName() +
      ", written into bulk ring (" + std::to_string(sink.sampleBytes(perSlot)) + "-byte slots)";
  bitoscope_log(BITOSCOPE_LOG_INFO, "%s", line.c_str());
  return {};
}

std::string DemoAnalyzer::sourceName() const {
  const sim::DemoFacetConfig& c = captureConfig_;
  if (c.test_pattern) return "counter test pattern";
  std::string marker;
  if (c.marker) {
    marker = ", marker: channel " + std::to_string(c.marker_channel) + " rises at sample " + std::to_string(c.marker_at);
  }
  switch (c.source) {
    case sim::DemoSource::Random:
      return "random source (seed " + std::to_string(c.seed) + ")";
    case sim::DemoSource::Pattern:
      return "pattern source (seed " + std::to_string(c.seed) + marker + ")";
    case sim::DemoSource::Noise:
      return "noise source (seed " + std::to_string(c.seed) + marker + ")";
    case sim::DemoSource::Structured:
      break;
  }
  return "structured source";
}

std::chrono::steady_clock::time_point DemoAnalyzer::now() const {
  return hooks_.now ? hooks_.now() : std::chrono::steady_clock::now();
}

DemoAnalyzer::Written DemoAnalyzer::writeChunk(Sink& sink, uint64_t first, uint32_t n) {
  const uint64_t bytes = std::visit([n](const auto& s) { return s.payloadBytes(n); }, *stream_);
  const std::span<uint8_t> room = sink.begin(n, !captureConfig_.realtime);
  if (room.empty()) return Written::failed;
  if (room.size() != bytes) {
    sink.fail("demo: ring handed out " + std::to_string(room.size()) + " bytes for " + std::to_string(n) +
                     " samples, which take " + std::to_string(bytes));
    return Written::failed;
  }
  auto written = std::visit([n, room](auto& s) { return s.writeNext(n, room); }, *stream_);
  if (!written) {
    sink.fail("demo: " + written.error());
    return Written::failed;
  }
  const bool more = sink.commit(n);
  if (sink.failed()) return Written::failed;
  if (hooks_.on_chunk_written) hooks_.on_chunk_written(first, n);
  return more ? Written::more : Written::last;
}

void DemoAnalyzer::warnBehind(Sink& sink) const {
  std::string line = "demo stream is more than 200 ms behind real time - ";
  const std::optional<bitoscope::plugin::RingFill> fill = sink.ringFill();
  if (!fill.has_value()) {
    line += sourceName() + " cannot keep up (bulk ring is already let go of)";
  } else if (fill->highWaterSlots >= fill->slotCount) {
    line += "host cannot keep up: bulk ring has been full (" + std::to_string(fill->highWaterSlots) + " of " +
            std::to_string(fill->slotCount) + " slots)";
  } else {
    line += sourceName() + " cannot keep real time: bulk ring held at most " +
            std::to_string(fill->highWaterSlots) + " of " + std::to_string(fill->slotCount) +
            " slots, so host is keeping up";
  }
  bitoscope_log(BITOSCOPE_LOG_WARNING, "%s", line.c_str());
}

std::expected<void, std::string> DemoAnalyzer::capture(const Capture&, Sink& sink) {
  const auto start = now();
  ChunkPacer pacer(start, rateHz_);
  PreciseWait precise;
  bool warnedBehind = false;
  while (!sink.stopping()) {
    const uint64_t produced = std::visit([](const auto& s) { return s.samplesProduced(); }, *stream_);
    uint32_t n = chunkSamples_;
    if (limit_ != 0) {
      const uint64_t left = limit_ - produced;
      n = static_cast<uint32_t>((std::min<uint64_t>)(n, (left + 7) & ~uint64_t{7}));
    }
    if (captureConfig_.realtime) {
      const auto t = now();
      const auto due = pacer.dueOf(produced + n);
      if (!warnedBehind && t > due && t - due > std::chrono::milliseconds(200)) {
        warnedBehind = true;
        warnBehind(sink);
      }
      const auto at = pacer.next(produced + n, n, t);
      if (t < at) {
        if (hooks_.wait_until) {
          hooks_.wait_until(at);
        } else {
          precise.until(at);
        }
      }
    }
    if (writeChunk(sink, produced, n) != Written::more) break;
  }
  // WHY: The high-water mark log line is parsed by an acceptance script; changing its wording breaks that script.
  if (const auto fill = sink.ringFill(); fill.has_value()) {
    bitoscope_log(BITOSCOPE_LOG_INFO, "ring high-water mark: %llu of %llu slots",
                  static_cast<unsigned long long>(fill->highWaterSlots),
                  static_cast<unsigned long long>(fill->slotCount));
  }
  bitoscope_log(BITOSCOPE_LOG_INFO, "demo capture stopped");
  return {};
}

DemoSession::DemoSession(DemoHooks hooks) : analyzer_(std::move(hooks)) {}

bitoscope::plugin::LogicAnalyzer* DemoSession::logicAnalyzer() { return &analyzer_; }

bitoscope::plugin::DriverDescription demoDriver(DemoHooks hooks) {
  namespace plugin = bitoscope::plugin;
  plugin::DriverDescription description;
  description.displayName = kDemoDriverName;
  description.version = kDemoDriverVersion;
  description.transport = plugin::Transport::none;
  description.enumerate = [](const plugin::Connection&)
      -> std::expected<std::vector<plugin::EnumeratedDevice>, std::string> {
    return std::vector<plugin::EnumeratedDevice>{plugin::EnumeratedDevice{std::string(kDemoDeviceId), {}, {}}};
  };
  description.identify = [](const plugin::FoundDevice&, plugin::Device& device)
      -> std::expected<void, plugin::DeviceFailure> {
    (void)device.setName(kDemoDeviceName);
    if (auto added = device.add(demoAnalyzerInfo()); !added) {
      return std::unexpected(plugin::DeviceFailure{plugin::DeviceProblem::other, std::move(added.error())});
    }
    return {};
  };
  description.open = [hooks = std::move(hooks)](const plugin::FoundDevice&, plugin::Device&, plugin::Progress&)
      -> std::expected<std::unique_ptr<plugin::Session>, plugin::DeviceFailure> {
    return std::make_unique<DemoSession>(hooks);
  };
  return description;
}

}

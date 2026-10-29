/*
 * SPDX-License-Identifier: MIT
 */

#include "plugin_decoder.h"

#include <algorithm>
#include <array>
#include <optional>
#include <span>
#include <string_view>
#include <utility>
#include <variant>

#include "raw_buffer_signal_view.h"
#include "ring_feed.h"

namespace bitoscope::decoder_host {

namespace dec = ipc::decoder;

namespace {

std::optional<int64_t> limitOf(const dec::Limit& limit) {
  if (const auto* value = std::get_if<int64_t>(&limit)) return *value;
  return std::nullopt;
}

decode::ValueFormat formatOf(const dec::ValueFormat& format) {
  decode::ValueFormat out;
  if (const auto* hex = std::get_if<dec::HexValue>(&format)) {
    out.kind = decode::ValueFormatKind::Hex;
    out.hex_min_digits = hex->digits;
    out.hex_digits_option = hex->digitsOption;
  } else if (std::holds_alternative<dec::Address7Value>(format)) {
    out.kind = decode::ValueFormatKind::Address7;
  } else if (std::holds_alternative<dec::NOfMValue>(format)) {
    out.kind = decode::ValueFormatKind::NOfM;
  } else if (std::holds_alternative<dec::DecimalValue>(format)) {
    out.kind = decode::ValueFormatKind::Decimal;
  } else if (std::holds_alternative<dec::NOfMBytesValue>(format)) {
    out.kind = decode::ValueFormatKind::NOfMBytes;
  } else if (std::holds_alternative<dec::BytesValue>(format)) {
    out.kind = decode::ValueFormatKind::Bytes;
  }
  return out;
}

std::optional<decode::StreamType> streamTypeOf(dec::StreamType type) {
  switch (type) {
    case dec::StreamType::words:
      return decode::StreamType::Words;
    case dec::StreamType::spiTransfers:
      return decode::StreamType::SpiTransfers;
    case dec::StreamType::unspecified:
    default:
      return std::nullopt;
  }
}

dec::StreamType wireTypeOf(decode::StreamType type) {
  return type == decode::StreamType::SpiTransfers ? dec::StreamType::spiTransfers : dec::StreamType::words;
}

}

PluginDecoder::PluginDecoder(std::shared_ptr<DecoderPluginClient> client, dec::DecoderInfo info, PluginDecoderOptions options)
    : client_(std::move(client)), info_(std::move(info)), options_(std::move(options)) {}

PluginDecoder::PluginDecoder(std::shared_ptr<DecoderPluginPool> pool, dec::DecoderInfo info, PluginDecoderOptions options)
    : pool_(std::move(pool)), info_(std::move(info)), options_(std::move(options)) {}

PluginDecoder::PluginDecoder(std::nullptr_t, dec::DecoderInfo info, PluginDecoderOptions options)
    : PluginDecoder(std::shared_ptr<DecoderPluginClient>(), std::move(info), std::move(options)) {}

std::expected<PluginDecoder::JobClient, std::string> PluginDecoder::clientForJob() {
  JobClient on;
  if (pool_) {
    auto lease = pool_->take();
    if (!lease) return std::unexpected(noProcessMessage(info_.name, lease.error()));
    on.lease = std::move(*lease);
    on.client = &on.lease.client();
  } else {
    on.client = client_.get();
  }
  if (options_.on_job_client && on.client != nullptr) options_.on_job_client(*on.client);
  return on;
}

void PluginDecoder::setCancel(const StopFlag* cancel, std::chrono::milliseconds grace) {
  options_.job.cancel = cancel;
  options_.job.cancel_grace = grace;
}

std::string PluginDecoder::name() const { return info_.name; }

std::string PluginDecoder::pluginName() const {
  if (pool_ != nullptr) return pool_->name();
  return client_ != nullptr ? client_->name() : std::string();
}

capture::DecoderVersions savedVersionsOf(const decode::Decoder& decoder) {
  const auto* plugin = dynamic_cast<const PluginDecoder*>(&decoder);
  if (plugin == nullptr) return {};
  return capture::DecoderVersions{plugin->pluginVersion(), plugin->info().version};
}

std::string PluginDecoder::pluginVersion() const {
  if (pool_ != nullptr) return pool_->info().pluginVersion;
  return client_ != nullptr ? client_->info().pluginVersion : std::string();
}

std::optional<std::string> PluginDecoder::windowWarningText() const {
  if (!window_warning_) return std::nullopt;
  return "decoder '" + info_.name + "': " + window_warning_->text();
}

std::string PluginDecoder::display() const { return info_.display; }

std::vector<decode::ChannelSpec> PluginDecoder::channels() const {
  std::vector<decode::ChannelSpec> out;
  for (const dec::ChannelSpec& c : info_.channels) out.push_back({c.name, c.description, c.required, c.display});
  return out;
}

std::vector<decode::OptionSpec> PluginDecoder::options() const {
  std::vector<decode::OptionSpec> out;
  for (const dec::OptionSpec& o : info_.options) {
    decode::OptionSpec spec;
    spec.name = o.name;
    spec.description = o.description;
    spec.type = o.type == dec::OptionType::integer ? decode::OptionType::Integer : decode::OptionType::String;
    spec.default_value = o.defaultValue;
    spec.allowed_values = o.allowedValues;
    spec.min = limitOf(o.min);
    spec.max = limitOf(o.max);
    spec.unsupported_values = o.unsupportedValues;
    spec.display = o.display;
    out.push_back(std::move(spec));
  }
  return out;
}

std::vector<std::string> PluginDecoder::unsupportedOptions() const { return info_.unsupportedOptions; }

std::vector<std::string> PluginDecoder::protocols() const { return info_.protocols; }

bool PluginDecoder::allowsSharedChannels() const { return info_.allowsSharedChannels; }

std::vector<decode::AnnotationClass> PluginDecoder::annotationClasses() const {
  std::vector<decode::AnnotationClass> out;
  for (const dec::AnnotationClass& c : info_.annotationClasses) {
    out.push_back({c.id, c.name, formatOf(c.format), static_cast<decode::AnnotationRole>(c.role)});
  }
  return out;
}

std::vector<decode::AnnotationRow> PluginDecoder::rows() const {
  std::vector<decode::AnnotationRow> out;
  for (const dec::Row& r : info_.rows) out.push_back({r.id, r.name});
  return out;
}

std::vector<decode::RowSource> PluginDecoder::rowSources() const {
  std::vector<decode::RowSource> out;
  for (const dec::RowSource& s : info_.rowSources) {
    decode::RowSource r{s.row, s.cls, s.source.row, s.source.classes, s.enableOption, decode::RowSource::Unknown{}};
    if (std::holds_alternative<dec::WordListRule>(s.rule)) {
      r.rule = decode::RowSource::WordList{};
    } else if (const auto* grid = std::get_if<dec::BitGridRule>(&s.rule)) {
      r.rule = decode::RowSource::BitGrid{grid->rateOption, grid->offsetBits, grid->countOption, grid->orderOption};
    } else if (const auto* edges = std::get_if<dec::ClockEdgesRule>(&s.rule)) {
      r.rule = decode::RowSource::ClockEdges{edges->clockChannel, edges->dataChannel, edges->countOption, edges->count};
    }
    out.push_back(std::move(r));
  }
  return out;
}

std::vector<decode::OutputSpec> PluginDecoder::outputs() const {
  std::vector<decode::OutputSpec> out;
  for (const dec::OutputSpec& o : info_.outputs) {
    const auto type = streamTypeOf(o.type);
    if (!type) continue;
    decode::OutputSpec spec{o.name, o.description, *type, o.width, o.widthOption, o.requiresChannel, o.display};
    if (!o.carries.empty()) spec.carries = o.carries;
    if (!o.carriesInput.empty()) spec.carries_input = o.carriesInput;
    out.push_back(std::move(spec));
  }
  return out;
}

std::vector<decode::InputSpec> PluginDecoder::inputs() const {
  std::vector<decode::InputSpec> out;
  for (const dec::InputSpec& i : info_.inputs) {
    const auto type = streamTypeOf(i.type);
    if (!type) continue;
    out.push_back({i.name, i.description, *type, i.width, i.required, i.display});
  }
  return out;
}

dec::JobSetup PluginDecoder::jobSetup(const decode::CaptureFacts& facts, std::vector<std::string> wanted) const {
  dec::JobSetup setup;
  setup.decoder = info_.name;
  for (const dec::ChannelSpec& c : info_.channels) {
    if (const auto position = channel(c.name)) setup.channels.push_back({c.name, *position});
  }
  for (const auto& [option, value] : resolvedOptions()) setup.options.push_back({option, value});
  setup.facts = {facts.sample_rate_milli_hz, facts.channel_count, facts.total_samples, facts.total_is_bound};
  setup.outputs = std::move(wanted);
  const decode::InputMap& bound = boundInputs();
  for (const dec::InputSpec& i : info_.inputs) {
    const auto it = bound.find(i.name);
    if (it != bound.end()) setup.inputs.push_back({i.name, wireTypeOf(it->second.output.type), it->second.width});
  }
  return setup;
}

std::expected<void, std::string> PluginDecoder::checkCaptureFacts(const decode::CaptureFacts& facts) const {
  window_warning_.reset();
  if (facts.sample_rate_milli_hz == 0) return std::unexpected("capture has no sample rate");
  const dec::JobSetup setup = jobSetup(factsForJob(facts));
  auto answer = pool_ ? pool_->checkJob(setup) : client_->checkJob(setup);
  if (!answer) return std::unexpected(std::move(answer.error()));
  look_ahead_samples_ = answer->lookAheadSamples;
  if (!setup.channels.empty()) window_warning_ = sizedFor(options_.job, look_ahead_samples_).warning;
  facts_ = facts;
  return {};
}

std::expected<void, std::string> PluginDecoder::decodeCapture(const capture::RawCaptureBuffer& buffer,
                                                              capture::CaptureExtent extent, capture::ExtentTail tail,
                                                              decode::AnnotationSink& sink, decode::StreamOutputs* outputs) {
  auto view = capture::RawBufferSignalView::create(buffer, extent, tail);
  if (!view) return std::unexpected(std::move(view.error()));
  capture_ = CaptureSource{&*view, &buffer, extent, tail, nullptr, nullptr};
  auto decoded = decode(*view, sink, outputs);
  capture_ = CaptureSource{};
  return decoded;
}

namespace {

class GrowingFacts final : public decode::SignalView {
 public:
  GrowingFacts(const capture::CapturePlan& plan, uint64_t bound_samples)
      : channels_(plan.channels()), rate_milli_hz_(plan.sampleRateMilliHz()), bound_samples_(bound_samples) {}

  uint32_t channelCount() const override { return channels_; }
  uint64_t sampleCount() const override { return bound_samples_; }
  uint64_t sampleRateMilliHz() const override { return rate_milli_hz_; }
  bool level(uint32_t, uint64_t) const override { return false; }
  std::optional<uint64_t> nextEdge(uint32_t, uint64_t, decode::EdgeKind) const override { return std::nullopt; }

 private:
  uint32_t channels_;
  uint64_t rate_milli_hz_;
  uint64_t bound_samples_;
};

}

std::expected<void, std::string> PluginDecoder::decodeGrowingCapture(std::shared_ptr<const GrowingCapture> growing,
                                                                     std::shared_ptr<const CaptureEnd> end,
                                                                     uint64_t bound_samples, decode::AnnotationSink& sink,
                                                                     decode::StreamOutputs* outputs) {
  const GrowingFacts view(growing->plan(), bound_samples);
  capture_ = CaptureSource{&view, nullptr, {}, capture::ExtentTail::Exact, std::move(growing), std::move(end)};
  auto decoded = decode(view, sink, outputs);
  capture_ = CaptureSource{};
  return decoded;
}

decode::CaptureFacts PluginDecoder::factsForJob(decode::CaptureFacts facts) const {
  if (capture_.growing) facts.total_is_bound = true;
  return facts;
}

std::expected<void, std::string> PluginDecoder::decodeCaptureMixed(const capture::RawCaptureBuffer& buffer,
                                                                   capture::CaptureExtent extent, capture::ExtentTail tail,
                                                                   decode::StreamInputs& inputs,
                                                                   decode::AnnotationSink& sink,
                                                                   decode::StreamOutputs* outputs) {
  auto view = capture::RawBufferSignalView::create(buffer, extent, tail);
  if (!view) return std::unexpected(std::move(view.error()));
  capture_ = CaptureSource{&*view, &buffer, extent, tail, nullptr, nullptr};
  const decode::CaptureFacts facts{view->sampleRateMilliHz(), view->channelCount(), view->sampleCount()};
  auto decoded = decodeMixed(*view, inputs, facts, sink, outputs);
  capture_ = CaptureSource{};
  return decoded;
}

namespace {

class AnnotationsOnward {
 public:
  AnnotationsOnward(decode::AnnotationSink& sink, std::vector<uint32_t> bytesClasses)
      : sink_(sink), bytesClasses_(std::move(bytesClasses)) {}
  AnnotationHandler handler() {
    return [this](std::span<const dec::Annotation> batch) {
      size_t n = 0;
      size_t used = 0;
      for (const dec::Annotation& a : batch) {
        const decode::Annotation annotation{a.begin, a.end, a.row, a.cls, a.value};
        if (!bytesClasses_.empty() && std::find(bytesClasses_.begin(), bytesClasses_.end(), a.cls) != bytesClasses_.end()) {
          if (n != 0) sink_.add(std::span<const decode::Annotation>(chunk_.data(), n));
          n = 0;
          sink_.addBytes(annotation, values_[used++]);
          continue;
        }
        chunk_[n++] = annotation;
        if (n == chunk_.size()) {
          sink_.add(std::span<const decode::Annotation>(chunk_.data(), n));
          n = 0;
        }
      }
      if (n != 0) sink_.add(std::span<const decode::Annotation>(chunk_.data(), n));
      values_ = {};
    };
  }
  ByteValuesHandler byteValuesHandler() {
    return [this](std::span<const std::vector<uint8_t>> values) { values_ = values; };
  }
  SettledHandler settledHandler() {
    return [this](uint64_t watermark) {
      if (watermark <= passed_) return;
      passed_ = watermark;
      sink_.settled(watermark);
    };
  }

 private:
  decode::AnnotationSink& sink_;
  std::vector<uint32_t> bytesClasses_;
  std::span<const std::vector<uint8_t>> values_;
  std::array<decode::Annotation, 256> chunk_{};
  uint64_t passed_ = 0;
};

}

std::vector<uint32_t> PluginDecoder::bytesClasses() const {
  std::vector<uint32_t> classes;
  for (const dec::AnnotationClass& k : info_.annotationClasses) {
    if (std::holds_alternative<dec::BytesValue>(k.format)) classes.push_back(k.id);
  }
  return classes;
}

std::expected<void, std::string> PluginDecoder::checkSinkTakesBytes(const decode::AnnotationSink& sink) const {
  const std::vector<uint32_t> classes = bytesClasses();
  if (classes.empty() || sink.takesBytes()) return {};
  return std::unexpected("decoder '" + info_.name + "': its class " + std::to_string(classes.front()) +
                         " shows bytes, but whoever takes its annotations cannot take bytes");
}

std::vector<std::string> PluginDecoder::wantedOutputs() {
  std::vector<std::string> wanted;
  for (const decode::OutputSpec& o : outputs()) {
    if (output(o.name) != nullptr) wanted.push_back(o.name);
  }
  return wanted;
}

ItemHandler PluginDecoder::itemsOnward() {
  return [this](std::string_view name, std::span<const dec::StreamItem> batch) {
    decode::StreamWriter* writer = output(name);
    if (writer == nullptr) return;
    for (const dec::StreamItem& i : batch) writer->put({i.begin, i.end, i.value, i.kind, i.flags});
  };
}

std::expected<void, std::string> PluginDecoder::runStreams(decode::AnnotationSink& sink) {
  const dec::JobSetup setup = jobSetup(facts_, wantedOutputs());
  ItemInputs inputs;
  for (const dec::InputBinding& b : setup.inputs) inputs[b.name] = input(b.name);
  if (auto taken = checkSinkTakesBytes(sink); !taken) return taken;
  AnnotationsOnward annotations(sink, bytesClasses());
  auto on = clientForJob();
  if (!on) return std::unexpected(std::move(on.error()));
  DecodeJobOptions job = options_.job;
  job.on_byte_values = annotations.byteValuesHandler();
  auto result = on->client->decodeItems(setup, inputs, job, annotations.handler(), itemsOnward(),
                                        annotations.settledHandler());
  if (!result) return std::unexpected(std::move(result.error().message));
  return {};
}

std::expected<FeedFunction, std::string> PluginDecoder::feedOf(const decode::SignalView& view,
                                                                const dec::JobSetup& setup) const {
  std::vector<uint32_t> bound;
  for (const dec::ChannelBinding& c : setup.channels) bound.push_back(c.channel);
  if (capture_.view == &view) {
    CaptureFeedOptions feed_options;
    feed_options.min_skip_samples = options_.min_skip_samples;
    feed_options.before_allocate = options_.job.before_allocate;
    feed_options.before_feed = options_.before_feed;
    if (capture_.growing) {
      return growingCaptureFeed(capture_.growing, capture_.end, std::move(bound), std::move(feed_options));
    }
    return captureFeed(*capture_.buffer, capture_.extent, capture_.tail, std::move(bound), std::move(feed_options));
  }
  return viewFeed(view, std::move(bound), options_.job.before_allocate);
}

std::expected<void, std::string> PluginDecoder::run(const decode::SignalView& view, decode::AnnotationSink& sink) {
  const dec::JobSetup setup =
      jobSetup(factsForJob({view.sampleRateMilliHz(), view.channelCount(), view.sampleCount()}), wantedOutputs());
  auto feed = feedOf(view, setup);
  if (!feed) return std::unexpected(std::move(feed.error()));
  DecodeJobOptions job = sizedFor(options_.job, look_ahead_samples_).job;
  if (auto taken = checkSinkTakesBytes(sink); !taken) return taken;
  AnnotationsOnward annotations(sink, bytesClasses());
  job.on_byte_values = annotations.byteValuesHandler();
  auto on = clientForJob();
  if (!on) return std::unexpected(std::move(on.error()));
  auto result = on->client->decode(setup, std::move(*feed), job, annotations.handler(), itemsOnward(),
                                   annotations.settledHandler());
  if (!result) return std::unexpected(std::move(result.error().message));
  return {};
}

std::expected<void, std::string> PluginDecoder::runMixed(const decode::SignalView& view, decode::AnnotationSink& sink) {
  const dec::JobSetup setup = jobSetup({view.sampleRateMilliHz(), view.channelCount(), view.sampleCount()}, wantedOutputs());
  ItemInputs inputs;
  for (const dec::InputBinding& b : setup.inputs) inputs[b.name] = input(b.name);
  if (auto taken = checkSinkTakesBytes(sink); !taken) return taken;
  AnnotationsOnward annotations(sink, bytesClasses());
  DecodeJobOptions itemsJob = options_.job;
  itemsJob.on_byte_values = annotations.byteValuesHandler();
  if (setup.channels.empty()) {
    auto on = clientForJob();
    if (!on) return std::unexpected(std::move(on.error()));
    auto result = on->client->decodeItems(setup, inputs, itemsJob, annotations.handler(), itemsOnward(),
                                          annotations.settledHandler());
    if (!result) return std::unexpected(std::move(result.error().message));
    return {};
  }
  auto feed = feedOf(view, setup);
  if (!feed) return std::unexpected(std::move(feed.error()));
  DecodeJobOptions job = sizedFor(options_.job, look_ahead_samples_).job;
  job.on_byte_values = annotations.byteValuesHandler();
  auto on = clientForJob();
  if (!on) return std::unexpected(std::move(on.error()));
  auto result = on->client->decodeMixed(setup, std::move(*feed), inputs, job, annotations.handler(), itemsOnward(),
                                        annotations.settledHandler());
  if (!result) return std::unexpected(std::move(result.error().message));
  return {};
}

std::expected<void, std::string> addPluginDecoders(decode::DecoderRegistry& registry,
                                                   std::shared_ptr<DecoderPluginClient> client,
                                                   PluginDecoderOptions options) {
  for (const dec::DecoderInfo& info : client->info().decoders) {
    auto added = registry.add(info.name, [client, info, options] {
      return std::make_unique<PluginDecoder>(client, info, options);
    });
    if (!added) return added;
  }
  return {};
}

std::expected<void, std::string> addPluginDecoders(decode::DecoderRegistry& registry,
                                                   std::shared_ptr<DecoderPluginPool> pool,
                                                   PluginDecoderOptions options) {
  for (const dec::DecoderInfo& info : pool->info().decoders) {
    auto added = registry.add(info.name, [pool, info, options] {
      return std::make_unique<PluginDecoder>(pool, info, options);
    });
    if (!added) return added;
  }
  return {};
}

}

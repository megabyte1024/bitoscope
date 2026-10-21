/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <algorithm>
#include <array>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <expected>
#include <format>
#include <iterator>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include "option_text.h"
#include "row_source.h"
#include "stream.h"

namespace bitoscope::decode {

inline constexpr uint32_t kMaxChannels = 32;

struct ChannelSpec {
  std::string name;
  std::string description;
  bool required = true;
  std::string display{};
};

enum class OptionType { Integer, String };

struct OptionSpec {
  std::string name;
  std::string description;
  OptionType type = OptionType::String;
  std::string default_value{};
  std::vector<std::string> allowed_values{};
  std::optional<int64_t> min{};
  std::optional<int64_t> max{};
  std::vector<std::string> unsupported_values{};
  std::string display{};
};

struct Annotation {
  uint64_t begin = 0;
  uint64_t end = 0;
  uint32_t row = 0;
  uint32_t cls = 0;
  uint64_t value = 0;
  bool operator==(const Annotation&) const = default;
};

enum class ValueFormatKind { None, Hex, Address7, NOfM, Decimal, NOfMBytes, Bytes };

struct ValueFormat {
  ValueFormatKind kind = ValueFormatKind::None;
  uint32_t hex_min_digits = 0;
  std::string hex_digits_option{};
  bool operator==(const ValueFormat&) const = default;
};

enum class AnnotationRole : uint8_t { None, Data, Start, Stop, Ok, Error, Control, Address, Bit, Info };

struct AnnotationClass {
  uint32_t id = 0;
  std::string name;
  ValueFormat format{};
  AnnotationRole role = AnnotationRole::None;
  bool operator==(const AnnotationClass&) const = default;
};

struct AnnotationRow {
  uint32_t id = 0;
  std::string name;
  bool operator==(const AnnotationRow&) const = default;
};

enum class EdgeKind { Rising, Falling, Any };

class SignalView {
 public:
  virtual ~SignalView() = default;
  virtual uint32_t channelCount() const = 0;
  virtual uint64_t sampleCount() const = 0;
  virtual uint64_t sampleRateMilliHz() const = 0;
  virtual bool level(uint32_t channel, uint64_t index) const = 0;
  virtual std::optional<uint64_t> nextEdge(uint32_t channel, uint64_t from, EdgeKind edge) const = 0;

  virtual std::string readError() const { return {}; }
};

class AnnotationSink {
 public:
  virtual ~AnnotationSink() = default;
  virtual void add(std::span<const Annotation> batch) = 0;
  virtual bool takesBytes() const { return false; }
  virtual void addBytes(const Annotation& annotation, std::span<const uint8_t> bytes) {
    (void)annotation;
    (void)bytes;
  }
  virtual void settled(uint64_t) {}
};

inline constexpr uint64_t kWatermarkStride = uint64_t{1} << 16;

template <std::size_t N = 64>
class AnnotationBatch {
 public:
  explicit AnnotationBatch(AnnotationSink& sink) : sink_(sink) {}
  ~AnnotationBatch() { flush(); }
  AnnotationBatch(const AnnotationBatch&) = delete;
  AnnotationBatch& operator=(const AnnotationBatch&) = delete;

  void emit(Annotation a) {
    buffer_[count_++] = a;
    if (count_ == N) flush();
  }
  void settle(uint64_t watermark) {
    watermark_ = watermark;
    if (watermark_ >= passed_ + kWatermarkStride) flush();
  }
  void flush() {
    if (count_ > 0) {
      sink_.add(std::span<const Annotation>(buffer_.data(), count_));
      count_ = 0;
    }
    if (watermark_ != passed_) {
      sink_.settled(watermark_);
      passed_ = watermark_;
    }
  }

 private:
  AnnotationSink& sink_;
  std::array<Annotation, N> buffer_{};
  std::size_t count_ = 0;
  uint64_t watermark_ = 0;
  uint64_t passed_ = 0;
};

using ChannelMap = std::map<std::string, uint32_t>;
using OptionMap = std::map<std::string, std::string>;

class Decoder;

struct BoundInput {
  std::string source;
  OutputSpec output;
  uint32_t width = 0;
  std::string width_note;
  const Decoder* from = nullptr;
};
using InputMap = std::map<std::string, BoundInput>;

struct StreamOutputs {
  StreamSink& sink;
  std::vector<std::string> wanted;
};

struct CaptureFacts {
  uint64_t sample_rate_milli_hz = 0;
  uint32_t channel_count = 0;
  uint64_t total_samples = 0;
};

std::string displayOf(const Decoder& decoder);

enum class MixedDeclarations { Refused, Allowed };

std::expected<void, std::string> checkDeclaration(const Decoder& decoder,
                                                  MixedDeclarations mixed = MixedDeclarations::Refused);

enum class Reads { Samples, Streams, SamplesAndStreams };

class Decoder {
 public:
  virtual ~Decoder() = default;
  virtual std::string name() const = 0;
  virtual std::string display() const { return {}; }
  virtual std::vector<ChannelSpec> channels() const = 0;
  virtual std::vector<OptionSpec> options() const = 0;
  // IMPLEMENT: Option ids the reference decoder has and this one does not implement yet.
  // Naming one in -P is refused with "not supported yet", not with "has no option".
  virtual std::vector<std::string> unsupportedOptions() const { return {}; }
  // IMPLEMENT: Two channels of one decoder may not share a capture channel unless the decoder says so.
  virtual bool allowsSharedChannels() const { return false; }
  // IMPLEMENT: Every AnnotationClass this decoder can emit, declared up front like channels() and options().
  // decode() refuses an annotation whose cls is not one of these.
  virtual std::vector<AnnotationClass> annotationClasses() const = 0;
  // IMPLEMENT: The rows annotations are drawn in, in DISPLAY order (not id order). Empty, the default: none declared,
  // and a viewer shows one unnamed row per row id the decoder emits.
  virtual std::vector<AnnotationRow> rows() const { return {}; }
  // IMPLEMENT: Rows whose content comes from another row's words (row_source.h). Empty, the default: none.
  virtual std::vector<RowSource> rowSources() const { return {}; }
  // IMPLEMENT: The streams this decoder writes and reads. A decoder declares channels, inputs or both; reads() says which.
  virtual std::vector<OutputSpec> outputs() const { return {}; }
  virtual std::vector<InputSpec> inputs() const { return {}; }
  // IMPLEMENT: The lower decoders this one is designed for, as decoder ids ("spi", "qspi"). Empty, the default, fits any.
  // An id no installed decoder has is allowed.
  virtual std::vector<std::string> protocols() const { return {}; }

  Reads reads() const {
    if (inputs().empty()) return Reads::Samples;
    return channels().empty() ? Reads::Streams : Reads::SamplesAndStreams;
  }

  const OptionMap& resolvedOptions() const { return resolved_; }

  const ChannelMap& boundChannels() const { return bound_; }

  const InputMap& boundInputs() const { return bound_inputs_; }

  // WHY: isConfigured() is its own flag: empty boundInputs() or resolvedOptions() does not tell configured from not.
  bool isConfigured() const { return configured_; }

  std::expected<void, std::string> configure(const ChannelMap& channels, const OptionMap& options,
                                             const InputMap& inputs = {}) {
    if (auto r = checkDeclaration(*this, MixedDeclarations::Allowed); !r) return r;
    const std::vector<ChannelSpec> channel_specs = this->channels();
    const std::vector<OptionSpec> option_specs = this->options();

    for (const auto& [name, index] : channels) {
      if (!hasChannel(channel_specs, name)) {
        return std::unexpected(std::format("decoder '{}' has no channel '{}'", this->name(), name));
      }
      if (index >= kMaxChannels) {
        return std::unexpected(std::format("channel '{}' is bound to {}, capture has at most {} channels",
                                           name, index, kMaxChannels));
      }
    }
    for (const ChannelSpec& spec : channel_specs) {
      if (spec.required && !channels.contains(spec.name)) {
        return std::unexpected(std::format("channel '{}' is required by decoder '{}'", spec.name,
                                           this->name()));
      }
    }
    if (!allowsSharedChannels()) {
      for (auto a = channels.begin(); a != channels.end(); ++a) {
        for (auto b = std::next(a); b != channels.end(); ++b) {
          if (a->second == b->second) {
            return std::unexpected(std::format("channels '{}' and '{}' are both bound to channel {}",
                                               a->first, b->first, a->second));
          }
        }
      }
    }

    const std::vector<InputSpec> input_specs = this->inputs();
    for (const auto& [name, bound] : inputs) {
      const auto spec = std::find_if(input_specs.begin(), input_specs.end(),
                                     [&](const InputSpec& s) { return s.name == name; });
      if (spec == input_specs.end()) {
        return std::unexpected(std::format("decoder '{}' has no input '{}'", this->name(), name));
      }
      if (auto r = checkBinding(displayOf(*this), *spec, bound.source, bound.output, bound.width, bound.width_note);
          !r) {
        return r;
      }
    }
    for (const InputSpec& spec : input_specs) {
      if (spec.required && !inputs.contains(spec.name)) {
        return std::unexpected(std::format("input '{}' is required by decoder '{}'", spec.name, this->name()));
      }
    }

    OptionMap resolved;
    for (const OptionSpec& spec : option_specs) resolved[spec.name] = spec.default_value;
    for (const auto& [name, value] : options) {
      const auto spec = std::find_if(option_specs.begin(), option_specs.end(),
                                     [&](const OptionSpec& s) { return s.name == name; });
      if (spec == option_specs.end()) {
        const std::vector<std::string> missing = unsupportedOptions();
        if (std::find(missing.begin(), missing.end(), name) != missing.end()) {
          return std::unexpected(
              std::format("decoder '{}': option '{}' is not supported yet", this->name(), name));
        }
        return std::unexpected(std::format("decoder '{}' has no option '{}'", this->name(), name));
      }
      if (spec->type == OptionType::Integer && !isInteger(value)) {
        return std::unexpected(std::format("option '{}' needs whole number, got '{}'", name, value));
      }
      auto accepted = checkValue(*spec, value);
      if (!accepted) return std::unexpected(std::move(accepted.error()));
      resolved[name] = std::move(*accepted);
    }

    bound_ = channels;
    bound_inputs_ = inputs;
    resolved_ = std::move(resolved);
    configured_ = true;
    return {};
  }

  std::expected<void, std::string> checkCapture(const CaptureFacts& facts) const {
    if (!configured_) {
      return std::unexpected(std::format("decoder '{}' is not configured", name()));
    }
    for (const auto& [channel_name, index] : bound_) {
      if (index >= facts.channel_count) {
        return std::unexpected(std::format("channel '{}' is bound to {}, capture has {} channels",
                                           channel_name, index, facts.channel_count));
      }
    }
    return checkCaptureFacts(facts);
  }

  std::expected<void, std::string> decode(const SignalView& view, AnnotationSink& sink,
                                          StreamOutputs* outputs = nullptr) {
    if (auto r = requireEntry(Reads::Samples); !r) return r;
    if (auto r = checkCapture({view.sampleRateMilliHz(), view.channelCount(), view.sampleCount()}); !r) {
      return r;
    }
    OutputsScope scope(*this);
    if (auto r = openOutputs(outputs, view.sampleCount()); !r) return r;
    live_.samples = view.sampleCount();
    GuardedSink guard(sink, view.sampleCount(), annotationClasses());
    auto ran = run(view, guard);
    const std::string stream_error = scope.close();
    // WHY: A view that failed to read fails the decode first: its answers were not the capture's samples.
    if (const std::string unread = view.readError(); !unread.empty()) {
      return std::unexpected(std::format("decoder '{}': {}", name(), unread));
    }
    return finish(ran, guard, stream_error);
  }

  std::expected<void, std::string> decodeStreams(StreamInputs& inputs, const CaptureFacts& facts,
                                                 AnnotationSink& sink, StreamOutputs* outputs = nullptr) {
    if (auto r = requireEntry(Reads::Streams); !r) return r;
    if (auto r = checkCapture(facts); !r) return r;
    if (auto r = checkInputsGiven(inputs); !r) return r;
    OutputsScope scope(*this);
    if (auto r = openOutputs(outputs, facts.total_samples); !r) return r;
    live_.inputs = &inputs;
    live_.samples = facts.total_samples;
    GuardedSink guard(sink, facts.total_samples, annotationClasses());
    auto ran = runStreams(guard);
    const std::string stream_error = scope.close();
    return finish(ran, guard, stream_error);
  }

  std::expected<void, std::string> decodeMixed(const SignalView& view, StreamInputs& inputs, const CaptureFacts& facts,
                                               AnnotationSink& sink, StreamOutputs* outputs = nullptr) {
    if (auto r = requireEntry(Reads::SamplesAndStreams); !r) return r;
    if (auto r = checkCapture(facts); !r) return r;
    if (view.sampleCount() != facts.total_samples || view.channelCount() != facts.channel_count ||
        view.sampleRateMilliHz() != facts.sample_rate_milli_hz) {
      return std::unexpected(std::format(
          "decoder '{}': capture facts ({} samples, {} channels, {} mHz) do not describe signal view ({} samples, {} "
          "channels, {} mHz)",
          name(), facts.total_samples, facts.channel_count, facts.sample_rate_milli_hz, view.sampleCount(),
          view.channelCount(), view.sampleRateMilliHz()));
    }
    if (auto r = checkInputsGiven(inputs); !r) return r;
    OutputsScope scope(*this);
    if (auto r = openOutputs(outputs, facts.total_samples); !r) return r;
    live_.inputs = &inputs;
    live_.samples = facts.total_samples;
    GuardedSink guard(sink, facts.total_samples, annotationClasses());
    auto ran = runMixed(view, guard);
    const std::string stream_error = scope.close();
    // WHY: A view that failed to read is reported before anything the decoder computed from its answers.
    if (const std::string unread = view.readError(); !unread.empty()) {
      return std::unexpected(std::format("decoder '{}': {}", name(), unread));
    }
    return finish(ran, guard, stream_error);
  }

  uint32_t resolvedWidth(const OutputSpec& output) const {
    if (output.width_option.empty()) return output.width;
    return static_cast<uint32_t>(optionInt(output.width_option));
  }
  std::string widthNote(const OutputSpec& output) const {
    if (output.width_option.empty()) return {};
    return std::format("{}={}", output.width_option, optionString(output.width_option));
  }
  bool outputAvailable(const OutputSpec& output) const {
    return output.requires_channel.empty() || bound_.contains(output.requires_channel);
  }

 protected:
  // IMPLEMENT: A decoder's own refusals that depend on the capture (a sample rate too low for its baud
  // rate, "one of these channels is needed"). Called through checkCapture(), never directly.
  virtual std::expected<void, std::string> checkCaptureFacts(const CaptureFacts&) const { return {}; }

  // IMPLEMENT: A sample decoder's work (decode()); an upper decoder's is runStreams() (decodeStreams()), and a decoder's
  // that reads both is runMixed() (decodeMixed()). Each has a default refusal so a decoder implements only
  // the one it is.
  virtual std::expected<void, std::string> run(const SignalView&, AnnotationSink&) {
    return std::unexpected(std::format("decoder '{}' does not decode samples", name()));
  }
  virtual std::expected<void, std::string> runStreams(AnnotationSink&) {
    return std::unexpected(std::format("decoder '{}' does not decode streams", name()));
  }
  virtual std::expected<void, std::string> runMixed(const SignalView&, AnnotationSink&) {
    return std::unexpected(std::format("decoder '{}' does not decode samples and streams", name()));
  }

  StreamWriter* output(std::string_view name) {
    for (std::size_t i = 0; i < live_.output_names.size(); ++i) {
      if (live_.output_names[i] == name) return live_.writers[i].get();
    }
    return nullptr;
  }
  StreamInput* input(std::string_view name) {
    if (live_.inputs == nullptr) return nullptr;
    if (!bound_inputs_.contains(std::string(name))) return nullptr;
    const auto it = live_.inputs->find(std::string(name));
    return it == live_.inputs->end() ? nullptr : it->second;
  }
  uint64_t captureSamples() const { return live_.samples; }

  std::optional<uint32_t> channel(std::string_view name) const {
    const auto it = bound_.find(std::string(name));
    if (it == bound_.end()) return std::nullopt;
    return it->second;
  }
  int64_t optionInt(std::string_view name) const {
    const auto it = resolved_.find(std::string(name));
    return it == resolved_.end() ? 0 : std::strtoll(it->second.c_str(), nullptr, 10);
  }
  std::string_view optionString(std::string_view name) const {
    const auto it = resolved_.find(std::string(name));
    return it == resolved_.end() ? std::string_view() : std::string_view(it->second);
  }

 private:
  std::expected<void, std::string> requireEntry(Reads entry) const {
    switch (reads()) {
      case Reads::Samples:
        if (entry == Reads::Samples) return {};
        return std::unexpected(std::format("decoder '{}' reads samples, not streams: run it with decode()", name()));
      case Reads::Streams:
        if (entry == Reads::Streams) return {};
        return std::unexpected(
            std::format("decoder '{}' reads streams, not samples: run it with decodeStreams()", name()));
      case Reads::SamplesAndStreams:
        if (entry == Reads::SamplesAndStreams) return {};
        return std::unexpected(
            std::format("decoder '{}' reads samples and streams: run it with decodeMixed()", name()));
    }
    return std::unexpected(std::format("decoder '{}' reads nothing this library knows", name()));
  }

  std::expected<void, std::string> checkInputsGiven(const StreamInputs& inputs) const {
    for (const auto& [bound, fed_by] : bound_inputs_) {
      const auto it = inputs.find(bound);
      if (it == inputs.end() || it->second == nullptr) {
        return std::unexpected(std::format("decoder '{}': input '{}' was given no stream", name(), bound));
      }
    }
    return {};
  }

  std::expected<void, std::string> openOutputs(StreamOutputs* outputs, uint64_t sample_count) {
    const std::vector<OutputSpec> specs = this->outputs();
    live_.output_names.clear();
    for (const OutputSpec& spec : specs) live_.output_names.push_back(spec.name);
    live_.writers.clear();
    live_.writers.resize(specs.size());
    if (outputs == nullptr) return {};
    std::vector<GuardedOutput> guarded;
    for (const OutputSpec& spec : specs) guarded.push_back({spec.name, spec.type, resolvedWidth(spec), false});
    for (const std::string& wanted : outputs->wanted) {
      const auto it = std::find(live_.output_names.begin(), live_.output_names.end(), wanted);
      if (it == live_.output_names.end()) {
        return std::unexpected(std::format("decoder '{}' has no output '{}'", name(), wanted));
      }
      const auto index = static_cast<std::size_t>(it - live_.output_names.begin());
      if (!outputAvailable(specs[index])) {
        return std::unexpected(std::format("{}'s output '{}' needs its channel '{}' bound", displayOf(*this),
                                           wanted, specs[index].requires_channel));
      }
      guarded[index].wanted = true;
    }
    live_.guard = std::make_unique<GuardedStreamSink>(outputs->sink, sample_count, std::move(guarded));
    for (std::size_t i = 0; i < specs.size(); ++i) {
      if (std::find(outputs->wanted.begin(), outputs->wanted.end(), specs[i].name) != outputs->wanted.end()) {
        live_.writers[i] = std::make_unique<StreamWriter>(*live_.guard, static_cast<uint32_t>(i));
      }
    }
    return {};
  }

  // WHY: The destructor drops writers WITHOUT flushing: an abandoned decode must not reach the caller's sink.
  class OutputsScope {
   public:
    explicit OutputsScope(Decoder& d) : d_(d) {}
    ~OutputsScope() {
      for (auto& w : d_.live_.writers) {
        if (w) w->discard();
      }
      reset();
    }
    OutputsScope(const OutputsScope&) = delete;
    OutputsScope& operator=(const OutputsScope&) = delete;
    std::string close() {
      for (auto& w : d_.live_.writers) {
        if (w) w->flush();
      }
      std::string error = d_.live_.guard ? d_.live_.guard->error() : std::string();
      reset();
      return error;
    }

   private:
    void reset() {
      d_.live_.writers.clear();
      d_.live_.guard.reset();
      d_.live_.inputs = nullptr;
      d_.live_.samples = 0;
    }
    Decoder& d_;
  };

  template <typename Guard>
  std::expected<void, std::string> finish(const std::expected<void, std::string>& ran, const Guard& guard,
                                          const std::string& stream_error) const {
    if (!ran) return ran;
    if (!guard.error().empty()) {
      return std::unexpected(std::format("decoder '{}': {}", name(), guard.error()));
    }
    if (!stream_error.empty()) return std::unexpected(std::format("decoder '{}': {}", name(), stream_error));
    return {};
  }

  class GuardedSink final : public AnnotationSink {
   public:
    GuardedSink(AnnotationSink& out, uint64_t sample_count, std::vector<AnnotationClass> classes)
        : out_(out), sample_count_(sample_count), classes_(std::move(classes)) {}
    void add(std::span<const Annotation> batch) override {
      if (!error_.empty()) return;
      for (std::size_t i = 0; i < batch.size(); ++i) {
        error_ = problemWith(batch[i]);
        if (!error_.empty()) {
          out_.add(batch.subspan(0, i));
          return;
        }
      }
      out_.add(batch);
    }
    bool takesBytes() const override { return out_.takesBytes(); }
    void addBytes(const Annotation& annotation, std::span<const uint8_t> bytes) override {
      if (!error_.empty()) return;
      error_ = problemWith(annotation);
      if (error_.empty() && !classShowsBytes(annotation.cls)) {
        error_ = std::format("annotation cls {} [{}, {}) carries bytes, but its class does not show bytes",
                             annotation.cls, annotation.begin, annotation.end);
      }
      if (error_.empty()) out_.addBytes(annotation, bytes);
    }
    // WHY: settled() is not forwarded after a refused batch: its dropped rest may begin before the watermark.
    void settled(uint64_t watermark) override {
      if (error_.empty()) out_.settled(watermark);
    }
    const std::string& error() const { return error_; }

   private:
    std::string problemWith(const Annotation& a) const {
      if (a.begin >= a.end || a.end > sample_count_) {
        return std::format("annotation cls {} [{}, {}) is not inside capture's {} samples", a.cls, a.begin, a.end,
                           sample_count_);
      }
      if (!declared(a.cls)) {
        return std::format("annotation cls {} [{}, {}) is not class this decoder declared", a.cls, a.begin, a.end);
      }
      return {};
    }
    bool declared(uint32_t cls) const {
      return std::any_of(classes_.begin(), classes_.end(),
                         [&](const AnnotationClass& c) { return c.id == cls; });
    }
    bool classShowsBytes(uint32_t cls) const {
      return std::any_of(classes_.begin(), classes_.end(), [&](const AnnotationClass& c) {
        return c.id == cls && c.format.kind == ValueFormatKind::Bytes;
      });
    }

    AnnotationSink& out_;
    uint64_t sample_count_;
    std::vector<AnnotationClass> classes_;
    std::string error_;
  };

  static bool hasChannel(const std::vector<ChannelSpec>& specs, const std::string& name) {
    return std::any_of(specs.begin(), specs.end(), [&](const ChannelSpec& s) { return s.name == name; });
  }
  // WHY: A String option value '1.0' is accepted as '1': old setups keep UART stop_bits written as a float.
  static std::expected<std::string, std::string> checkValue(const OptionSpec& spec, const std::string& value) {
    const auto allowedList = [&spec] {
      std::string list;
      for (const std::string& allowed : spec.allowed_values) {
        if (!list.empty()) list += ", ";
        list += allowed;
      }
      return list;
    };
    const auto listed = [](const std::vector<std::string>& list, const std::string& text) {
      return std::find(list.begin(), list.end(), text) != list.end();
    };
    const std::string canonical = spec.type == OptionType::String ? canonicalOptionText(value) : value;
    if (listed(spec.unsupported_values, value) || listed(spec.unsupported_values, canonical)) {
      return std::unexpected(std::format("option '{}': '{}' is not supported yet, allowed: {}", spec.name,
                                         value, allowedList()));
    }
    std::string accepted = value;
    if (!spec.allowed_values.empty() && !listed(spec.allowed_values, value)) {
      if (!listed(spec.allowed_values, canonical)) {
        return std::unexpected(
            std::format("option '{}' cannot be '{}', allowed: {}", spec.name, value, allowedList()));
      }
      accepted = canonical;
    }
    if (spec.type == OptionType::Integer && (spec.min || spec.max)) {
      int64_t number = 0;
      std::from_chars(value.data(), value.data() + value.size(), number);
      if ((spec.min && number < *spec.min) || (spec.max && number > *spec.max)) {
        const std::string low = spec.min ? std::to_string(*spec.min) : std::string("any");
        const std::string high = spec.max ? std::to_string(*spec.max) : std::string("any");
        return std::unexpected(std::format("option '{}' cannot be {}, allowed: {} to {}", spec.name,
                                           value, low, high));
      }
    }
    return accepted;
  }
  static bool isInteger(const std::string& text) {
    int64_t value = 0;
    const char* end = text.data() + text.size();
    const auto r = std::from_chars(text.data(), end, value);
    return !text.empty() && r.ec == std::errc() && r.ptr == end;
  }

  bool configured_ = false;
  ChannelMap bound_;
  InputMap bound_inputs_;
  OptionMap resolved_;
  struct LiveStreams {
    LiveStreams() = default;
    LiveStreams(const LiveStreams&) {}
    LiveStreams& operator=(const LiveStreams&) { return *this; }
    std::vector<std::string> output_names;
    std::vector<std::unique_ptr<StreamWriter>> writers;
    std::unique_ptr<GuardedStreamSink> guard;
    StreamInputs* inputs = nullptr;
    uint64_t samples = 0;
  } live_;
};

inline std::expected<void, std::string> checkDeclaration(const Decoder& decoder, MixedDeclarations mixed) {
  const std::string name = decoder.name();
  const std::vector<ChannelSpec> channels = decoder.channels();
  const std::vector<InputSpec> inputs = decoder.inputs();
  const std::vector<OptionSpec> options = decoder.options();
  if (mixed == MixedDeclarations::Refused && !channels.empty() && !inputs.empty()) {
    return std::unexpected(
        std::format("decoder '{}' declares both channels and inputs; decoder reads samples or streams", name));
  }

  struct Named {
    std::string_view name;
    std::string_view kind;
  };
  std::vector<Named> names;
  for (const ChannelSpec& c : channels) names.push_back({c.name, "channel"});
  for (const InputSpec& i : inputs) names.push_back({i.name, "input"});
  for (const OptionSpec& o : options) names.push_back({o.name, "option"});
  for (std::size_t i = 0; i < names.size(); ++i) {
    if (names[i].name == "id") {
      return std::unexpected(std::format("decoder '{}': {} name 'id' is reserved", name, names[i].kind));
    }
    for (std::size_t j = 0; j < i; ++j) {
      if (names[j].name == names[i].name) {
        return std::unexpected(std::format("decoder '{}' uses name '{}' twice ({} and {})", name, names[i].name,
                                           names[j].kind, names[i].kind));
      }
    }
  }

  const std::vector<OutputSpec> outputs = decoder.outputs();
  for (std::size_t i = 0; i < outputs.size(); ++i) {
    const OutputSpec& out = outputs[i];
    for (std::size_t j = 0; j < i; ++j) {
      if (outputs[j].name == out.name) {
        return std::unexpected(std::format("decoder '{}' declares output '{}' twice", name, out.name));
      }
    }
    if (out.width_option.empty()) {
      if (out.width < 1 || out.width > 32) {
        return std::unexpected(std::format(
            "decoder '{}': output '{}' needs width of 1 to 32 or Integer option that sets it", name, out.name));
      }
    } else if (std::none_of(options.begin(), options.end(), [&](const OptionSpec& o) {
                 return o.name == out.width_option && o.type == OptionType::Integer;
               })) {
      return std::unexpected(std::format("decoder '{}': output '{}' takes its width from '{}', which is not "
                                         "Integer option",
                                         name, out.name, out.width_option));
    }
    if (!out.requires_channel.empty() &&
        std::none_of(channels.begin(), channels.end(),
                     [&](const ChannelSpec& c) { return c.name == out.requires_channel; })) {
      return std::unexpected(std::format("decoder '{}': output '{}' needs channel '{}', which it does not have", name,
                                         out.name, out.requires_channel));
    }
    if (out.carries && out.carries_input) {
      return std::unexpected(std::format(
          "decoder '{}': output '{}' declares both protocol it carries and input it passes through", name, out.name));
    }
    if (out.carries && out.carries->empty()) {
      return std::unexpected(std::format("decoder '{}': output '{}' carries protocol with no name", name, out.name));
    }
    if (out.carries_input) {
      if (out.carries_input->empty()) {
        return std::unexpected(
            std::format("decoder '{}': output '{}' passes through input with no name", name, out.name));
      }
      if (std::none_of(inputs.begin(), inputs.end(),
                       [&](const InputSpec& i) { return i.name == *out.carries_input; })) {
        return std::unexpected(std::format("decoder '{}': output '{}' passes through input '{}', which it does not have",
                                           name, out.name, *out.carries_input));
      }
    }
  }
  return checkRowSources(decoder);
}

}

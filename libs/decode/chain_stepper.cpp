/*
 * SPDX-License-Identifier: MIT
 */

#include "chain_stepper.h"

#include <algorithm>
#include <exception>
#include <format>
#include <memory>
#include <new>
#include <string>
#include <string_view>
#include <utility>

namespace bitoscope::decode {

namespace {

class StreamCollector final : public StreamSink {
 public:
  StreamCollector(std::size_t instance, std::vector<std::string> names, std::vector<std::vector<StreamItem>*> targets,
                  const std::function<void(std::size_t, std::string_view, std::size_t)>& before_collect)
      : instance_(instance), names_(std::move(names)), targets_(std::move(targets)), before_collect_(before_collect) {}

  void add(uint32_t output, std::span<const StreamItem> batch) override {
    if (!out_of_memory_.empty() || output >= targets_.size() || targets_[output] == nullptr) return;
    try {
      if (before_collect_) before_collect_(instance_, names_[output], batch.size());
      targets_[output]->insert(targets_[output]->end(), batch.begin(), batch.end());
    } catch (const std::bad_alloc&) {
      out_of_memory_ = names_[output];
      for (std::vector<StreamItem>* target : targets_) {
        if (target != nullptr) std::vector<StreamItem>().swap(*target);
      }
    }
  }
  const std::string& outOfMemory() const { return out_of_memory_; }

 private:
  std::size_t instance_;
  std::vector<std::string> names_;
  std::vector<std::vector<StreamItem>*> targets_;
  const std::function<void(std::size_t, std::string_view, std::size_t)>& before_collect_;
  std::string out_of_memory_;
};

std::size_t wantedIndex(const ChainInstance& instance, std::string_view output) {
  const auto it = std::find(instance.wanted.begin(), instance.wanted.end(), output);
  return static_cast<std::size_t>(it - instance.wanted.begin());
}

struct Freed {
  std::size_t from = 0;
  std::size_t output = 0;
  std::size_t held = 0;
  std::size_t capacity_left = 0;
  std::vector<StreamItem> items;
};

ChainOutcome outOfMemory(const std::string& id) {
  try {
    return std::unexpected(std::format("decoder '{}': out of memory", id));
  } catch (const std::bad_alloc&) {
    return std::unexpected(std::string("out of memory"));
  }
}

// WHY: Last words fit std::string's own buffer, so copying them cannot throw std::bad_alloc.
ChainOutcome copyOutcome(ChainOutcome& outcome, const std::string& id) {
  try {
    return outcome;
  } catch (const std::bad_alloc&) {
  }
  outcome = outOfMemory(id);
  try {
    return outcome;
  } catch (const std::bad_alloc&) {
  }
  outcome = std::unexpected(std::string("out of memory"));
  return outcome;
}

}

ChainStepper::ChainStepper(std::span<ChainInstance> instances, const SignalView& view,
                           std::function<AnnotationSink&(std::size_t)> sink_for, ChainRunHooks hooks)
    : instances_(instances),
      view_(view),
      sink_for_(std::move(sink_for)),
      hooks_(std::move(hooks)),
      streams_(instances.size()),
      phase_(instances.size(), Phase::NotStarted),
      outcomes_(instances.size()),
      readers_left_(instances.size()) {
  for (std::size_t i = 0; i < instances_.size(); ++i) {
    streams_[i].resize(instances_[i].wanted.size());
    readers_left_[i].assign(instances_[i].wanted.size(), 0);
  }
  for (std::size_t i = 0; i < instances_.size(); ++i) {
    for (const ChainBinding& b : instances_[i].inputs) {
      ++readers_left_[b.from][wantedIndex(instances_[b.from], b.output)];
    }
  }
}

std::size_t ChainStepper::size() const { return instances_.size(); }

bool ChainStepper::inputsEndedLocked(std::size_t instance) const {
  const std::vector<ChainBinding>& inputs = instances_[instance].inputs;
  return std::all_of(inputs.begin(), inputs.end(),
                     [this](const ChainBinding& b) { return phase_[b.from] == Phase::Ended; });
}

const ChainBinding* ChainStepper::failedInputLocked(std::size_t instance) const {
  for (const ChainBinding& b : instances_[instance].inputs) {
    if (!outcomes_[b.from].has_value()) return &b;
  }
  return nullptr;
}

std::vector<std::size_t> ChainStepper::ready() const {
  const std::lock_guard<std::mutex> lock(mutex_);
  std::vector<std::size_t> out;
  for (std::size_t i = 0; i < phase_.size(); ++i) {
    if (phase_[i] == Phase::NotStarted && inputsEndedLocked(i) && failedInputLocked(i) == nullptr) out.push_back(i);
  }
  return out;
}

std::optional<ChainStep> ChainStepper::take() {
  const std::lock_guard<std::mutex> lock(mutex_);
  for (std::size_t i = 0; i < phase_.size(); ++i) {
    if (phase_[i] != Phase::NotStarted || !inputsEndedLocked(i)) continue;
    phase_[i] = Phase::Taken;
    return ChainStep{i, failedInputLocked(i) == nullptr};
  }
  return std::nullopt;
}

std::expected<ChainOutcome, ChainStepRefusal> ChainStepper::run(std::size_t i) {
  const ChainBinding* failed = nullptr;
  {
    const std::lock_guard<std::mutex> lock(mutex_);
    if (i >= phase_.size()) return std::unexpected(ChainStepRefusal::NoSuchInstance);
    if (phase_[i] == Phase::Running) return std::unexpected(ChainStepRefusal::Running);
    if (phase_[i] == Phase::Ended) return std::unexpected(ChainStepRefusal::Ended);
    if (phase_[i] == Phase::NotStarted && !inputsEndedLocked(i)) return std::unexpected(ChainStepRefusal::Waiting);
    phase_[i] = Phase::Running;
    failed = failedInputLocked(i);
  }

  const ChainInstance& own = instances_[i];
  ChainOutcome outcome;
  if (failed != nullptr) {
    const std::string& from = instances_[failed->from].id;
    try {
      outcome = std::unexpected(std::format("input '{}' ({}.{}) is not available: decoder '{}' failed", failed->input,
                                            from, failed->output, from));
    } catch (const std::bad_alloc&) {
      outcome = outOfMemory(own.id);
    }
  } else if (ChainOutcome go = hooks_.before_instance ? hooks_.before_instance(i) : ChainOutcome{}; !go) {
    outcome = std::move(go);
  } else {
    outcome = decode(i);
  }

  std::vector<Freed> freed;
  bool room = true;
  try {
    freed.reserve(streams_[i].size() + own.inputs.size());
  } catch (const std::bad_alloc&) {
    room = false;
    if (outcome) outcome = outOfMemory(own.id);
  }
  {
    const std::lock_guard<std::mutex> lock(mutex_);
    const auto take_stream = [&](std::size_t from, std::size_t output) {
      if (!room) {
        std::vector<StreamItem>().swap(streams_[from][output]);
        return;
      }
      Freed f{from, output, streams_[from][output].size(), 0, {}};
      f.items.swap(streams_[from][output]);
      f.capacity_left = streams_[from][output].capacity();
      freed.push_back(std::move(f));
    };
    if (!outcome) {
      for (std::size_t k = 0; k < streams_[i].size(); ++k) take_stream(i, k);
    }
    for (const ChainBinding& b : own.inputs) --readers_left_[b.from][wantedIndex(instances_[b.from], b.output)];
    for (const ChainBinding& b : own.inputs) {
      const std::size_t k = wantedIndex(instances_[b.from], b.output);
      if (readers_left_[b.from][k] == 0) take_stream(b.from, k);
    }
  }
  for (Freed& f : freed) {
    std::vector<StreamItem>().swap(f.items);
    if (hooks_.on_release) hooks_.on_release(f.from, instances_[f.from].wanted[f.output], f.held, f.capacity_left);
  }
  if (hooks_.after_instance) hooks_.after_instance(i, outcome);

  ChainOutcome kept = copyOutcome(outcome, own.id);
  {
    const std::lock_guard<std::mutex> lock(mutex_);
    outcomes_[i] = std::move(kept);
    phase_[i] = Phase::Ended;
    ++ended_;
  }
  return outcome;
}

ChainOutcome ChainStepper::decode(std::size_t i) {
  ChainInstance& instance = instances_[i];
  Decoder& decoder = *instance.decoder;
  AnnotationSink& sink = sink_for_(i);
  try {
    std::vector<OutputSpec> specs;
    Reads reads = Reads::Samples;
    try {
      specs = decoder.outputs();
      reads = decoder.reads();
    } catch (const std::bad_alloc&) {
      throw;
    } catch (const std::exception& e) {
      return std::unexpected(std::format("decoder '{}': declaration failed: {}", instance.id, e.what()));
    } catch (...) {
      return std::unexpected(std::format("decoder '{}': declaration failed: unknown exception", instance.id));
    }
    std::vector<std::string> names;
    std::vector<std::vector<StreamItem>*> targets(specs.size(), nullptr);
    for (std::size_t j = 0; j < specs.size(); ++j) {
      names.push_back(specs[j].name);
      const std::size_t k = wantedIndex(instance, specs[j].name);
      if (k < instance.wanted.size()) targets[j] = &streams_[i][k];
    }
    StreamCollector collector(i, std::move(names), std::move(targets), hooks_.before_collect);
    StreamOutputs outputs{collector, instance.wanted};
    StreamOutputs* const wanted = instance.wanted.empty() ? nullptr : &outputs;

    ChainOutcome ran;
    if (reads == Reads::Samples) {
      ran = hooks_.decode_samples ? hooks_.decode_samples(i, decoder, sink, wanted) : decoder.decode(view_, sink, wanted);
    } else {
      std::vector<std::unique_ptr<VectorStreamInput>> readers;
      StreamInputs inputs;
      for (const ChainBinding& b : instance.inputs) {
        const std::vector<StreamItem>& items = streams_[b.from][wantedIndex(instances_[b.from], b.output)];
        readers.push_back(std::make_unique<VectorStreamInput>(items));
        inputs[b.input] = readers.back().get();
      }
      const CaptureFacts facts{view_.sampleRateMilliHz(), view_.channelCount(), view_.sampleCount()};
      if (reads == Reads::SamplesAndStreams) {
        ran = hooks_.decode_mixed ? hooks_.decode_mixed(i, decoder, inputs, facts, sink, wanted)
                                  : decoder.decodeMixed(view_, inputs, facts, sink, wanted);
      } else {
        ran = hooks_.decode_streams ? hooks_.decode_streams(i, decoder, inputs, facts, sink, wanted)
                                    : decoder.decodeStreams(inputs, facts, sink, wanted);
      }
    }
    if (!ran) return ran;
    if (!collector.outOfMemory().empty()) {
      return std::unexpected(
          std::format("decoder '{}': out of memory collecting its output '{}'", instance.id, collector.outOfMemory()));
    }
    return {};
  } catch (const std::bad_alloc&) {
    return outOfMemory(instance.id);
  }
}

std::optional<ChainOutcome> ChainStepper::outcome(std::size_t instance) const {
  const std::lock_guard<std::mutex> lock(mutex_);
  if (instance >= phase_.size() || phase_[instance] != Phase::Ended) return std::nullopt;
  return outcomes_[instance];
}

bool ChainStepper::finished() const {
  const std::lock_guard<std::mutex> lock(mutex_);
  return ended_ == phase_.size();
}

}

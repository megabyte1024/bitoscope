/*
 * SPDX-License-Identifier: MIT
 */

#include "decoder_runner.h"

#include <algorithm>
#include <chrono>
#include <exception>
#include <memory>
#include <mutex>
#include <new>
#include <optional>
#include <span>
#include <string>
#include <thread>
#include <utility>

#include "chain_stepper.h"
#include "decode_budget.h"
#include "decoded_annotations.h"
#include "gui_event_handlers.h"
#include "plugin_decoder.h"

namespace bitoscope::gui_decode {

namespace {

namespace decode = bitoscope::decode;
using Result = std::expected<std::shared_ptr<const DecodedAnnotations>, std::string>;

struct RunShared {
  std::uint64_t run;
  gui_events::GuiEventQueue& queue;
  std::uint64_t budgetBytes;
  // WHY: Shared: a posted snapshot returns its bytes when its last holder drops it, maybe after the runner is gone.
  std::shared_ptr<LockedMemory> memory;
  ProgressClock clock;
  const std::atomic<bool>& cancel;
  // WHY: Held while progress is checked against cancel and posted, so none is posted after cancel() returns.
  std::mutex progressMutex{};
};

void post(gui_events::GuiEventQueue& queue, std::uint64_t run, std::size_t index, Result result) {
  auto shared = std::make_shared<const Result>(std::move(result));
  queue.post([run, index, shared](const gui_events::GuiEventHandlers& handlers) {
    if (handlers.onDecoderResult) handlers.onDecoderResult(run, index, *shared);
  });
}

void postProgress(gui_events::GuiEventQueue& queue, std::uint64_t run, std::size_t index,
                  std::shared_ptr<const DecodeProgress> progress) {
  queue.post([run, index, progress = std::move(progress)](const gui_events::GuiEventHandlers& handlers) {
    if (handlers.onDecoderProgress) handlers.onDecoderProgress(run, index, *progress);
  });
}

// WHY: Snapshot has its own share: a refused MemoryShare refuses its build for good, which must not end its decoder.
struct SnapshotCharge {
  explicit SnapshotCharge(std::shared_ptr<LockedMemory> locked)
      : memory(std::move(locked)), share(*memory, MemoryShare::Kind::Snapshot) {}
  std::shared_ptr<LockedMemory> memory;
  MemoryShare share;
};

struct HeldSnapshot {
  HeldSnapshot(waveform_view::AnnotationGroups groups, std::shared_ptr<SnapshotCharge> owner)
      : bytes(groups.memoryBytes()), annotations(std::move(groups)), charge(std::move(owner)) {}
  ~HeldSnapshot() { charge->share.release(bytes); }
  HeldSnapshot(const HeldSnapshot&) = delete;
  HeldSnapshot& operator=(const HeldSnapshot&) = delete;
  std::size_t bytes;
  DecodedAnnotations annotations;
  std::shared_ptr<SnapshotCharge> charge;
};

// WHY: While a snapshot is made, requests go to its share: snapshot() charges the builder's own memory.
class RoutedMemory final : public waveform_view::AnnotationMemory {
 public:
  explicit RoutedMemory(waveform_view::AnnotationMemory& to) : to_(&to) {}
  void routeTo(waveform_view::AnnotationMemory& to) { to_ = &to; }
  bool acquire(std::size_t bytes) override { return to_->acquire(bytes); }
  void release(std::size_t bytes) override { to_->release(bytes); }

 private:
  waveform_view::AnnotationMemory* to_;
};

// WHY: A refused build drops later batches and reports at finish(): a running decoder cannot be stopped,
// and an exception could leave AnnotationBatch's destructor.
class GroupingSink final : public decode::AnnotationSink {
 public:
  GroupingSink(RunShared& shared, std::size_t index, std::uint64_t captureSamples)
      : shared_(shared), index_(index), captureSamples_(captureSamples), share_(*shared.memory), route_(share_),
        builder_(route_) {}
  ~GroupingSink() override { giveBackSnapshot(); }
  GroupingSink(const GroupingSink&) = delete;
  GroupingSink& operator=(const GroupingSink&) = delete;

  void add(std::span<const decode::Annotation> batch) override { (void)builder_.add(batch); }
  bool takesBytes() const override { return true; }
  void addBytes(const decode::Annotation& annotation, std::span<const std::uint8_t> bytes) override {
    (void)builder_.addBytes(annotation, bytes);
  }
  void settled(std::uint64_t watermark) override {
    if (watermark <= shown_) return;
    const std::chrono::nanoseconds now = shared_.clock();
    if (!pacer_.due(now) || shared_.cancel.load()) return;
    shown_ = watermark;
    snapshot(watermark);
    pacer_.taken(now, shared_.clock());
  }
  void start() { pacer_ = SnapshotPacer(shared_.clock()); }
  std::expected<waveform_view::AnnotationGroups, waveform_view::GroupsRefusal> finish() { return builder_.finish(); }
  waveform_view::AnnotationMemory& memory() { return share_; }

 private:
  void giveBackSnapshot() {
    if (!charge_) return;
    charge_->share.giveBack();
    charge_.reset();
  }

  void snapshot(std::uint64_t p) {
    giveBackSnapshot();
    std::shared_ptr<SnapshotCharge> charge;
    try {
      charge = std::make_shared<SnapshotCharge>(shared_.memory);
    } catch (const std::bad_alloc&) {
      return;
    }
    route_.routeTo(charge->share);
    auto groups = builder_.snapshot(p);
    route_.routeTo(share_);
    if (!groups) return;
    std::shared_ptr<const HeldSnapshot> held;
    try {
      held = std::make_shared<const HeldSnapshot>(std::move(*groups), charge);
    } catch (const std::bad_alloc&) {
      charge->share.release(groups->memoryBytes());
      return;
    }
    try {
      auto progress = std::make_shared<const DecodeProgress>(
        DecodeProgress{p, captureSamples_, std::shared_ptr<const DecodedAnnotations>(held, &held->annotations)});
      std::lock_guard<std::mutex> lock(shared_.progressMutex);
      if (shared_.cancel.load()) return;
      postProgress(shared_.queue, shared_.run, index_, std::move(progress));
    } catch (const std::exception&) {
      return;
    }
    charge_ = std::move(charge);
  }

  RunShared& shared_;
  const std::size_t index_;
  const std::uint64_t captureSamples_;
  MemoryShare share_;
  RoutedMemory route_;
  waveform_view::AnnotationGroupsBuilder builder_;
  SnapshotPacer pacer_;
  std::uint64_t shown_ = 0;
  std::shared_ptr<SnapshotCharge> charge_;
};

decode::ChainPlan independent(std::vector<ConfiguredDecoderRun> decoders) {
  decode::ChainPlan plan;
  for (ConfiguredDecoderRun& d : decoders) {
    std::string id = d.decoder->name();
    plan.instances.push_back({{}, std::move(id), std::move(d.decoder), {}, {}});
  }
  return plan;
}

SignalViewSource borrowed(const decode::SignalView& view) {
  return [&view]() -> std::expected<std::shared_ptr<const decode::SignalView>, std::string> {
    return std::shared_ptr<const decode::SignalView>(std::shared_ptr<void>(), &view);
  };
}

decode::CaptureFacts factsOf(const decode::SignalView& view) {
  return {view.sampleRateMilliHz(), view.channelCount(), view.sampleCount()};
}

class UnusedView final : public decode::SignalView {
 public:
  uint32_t channelCount() const override { return 0; }
  uint64_t sampleCount() const override { return 0; }
  uint64_t sampleRateMilliHz() const override { return 0; }
  bool level(uint32_t, uint64_t) const override { return false; }
  std::optional<uint64_t> nextEdge(uint32_t, uint64_t, decode::EdgeKind) const override { return std::nullopt; }
};

struct Slot {
  std::string refusal;
  std::shared_ptr<const decode::SignalView> view;
  std::unique_ptr<GroupingSink> sink;
  std::shared_ptr<const DecodedAnnotations> finished;
  bool started = false;
  std::atomic<bool> posted{false};
  std::atomic<bool> stranded{false};
};

template <typename Run>
decode::ChainOutcome guarded(Run&& run) {
  try {
    return run();
  } catch (const std::bad_alloc&) {
    throw;
  } catch (const std::exception& e) {
    return std::unexpected(std::string("decoder failed: ") + e.what());
  } catch (...) {
    return std::unexpected(std::string("decoder failed with unknown error"));
  }
}

decoder_host::PluginDecoder* pluginOf(decode::Decoder& decoder) {
  return dynamic_cast<decoder_host::PluginDecoder*>(&decoder);
}

template <typename Body>
void quietly(Body&& body) {
  try {
    body();
  } catch (...) {
  }
}

}

struct DecoderRunner::Work {
  Work(std::uint64_t runId, SignalViewSource source, bool oneView, decode::ChainPlan chain,
       gui_events::GuiEventQueue& events, DecodeMemory budget, DecodeThreadSetup threadSetup, ProgressClock clock,
       std::function<void(std::size_t)> beforeStart, std::optional<RunCapture> fromCapture,
       const std::atomic<bool>& cancel)
      : views(std::move(source)),
        oneAfterAnother(oneView),
        setUp(std::move(threadSetup)),
        beforeThreadStart(std::move(beforeStart)),
        capture(std::move(fromCapture)),
        plan(std::move(chain)),
        shared{runId, events, budget.budgetBytes,
               std::make_shared<LockedMemory>(
                 budget.memory ? std::move(budget.memory)
                               : std::make_shared<waveform_view::AnnotationBudget>(budget.budgetBytes, budget.heldBytes)),
               clock ? std::move(clock) : steadyProgressClock(), cancel},
        slots(plan.instances.size()),
        stepper(plan.instances, unused, [this](std::size_t i) -> decode::AnnotationSink& { return *slots[i].sink; },
                makeHooks()) {
    // WHY: Reserve all threads up front: join() runs while later ones are added, so they must never move.
    threads.reserve(oneAfterAnother ? 1 : plan.instances.size());
  }

  void begin() {
    if (!oneAfterAnother) {
      quietly([this] { startReady(); });
      return;
    }
    const std::lock_guard<std::mutex> lock(threadsMutex);
    threads.emplace_back([this] {
      quietly([this] { startReady(); });
      ++returned;
    });
  }

  void startReady() {
    while (!shared.cancel.load()) {
      const std::optional<decode::ChainStep> next = stepper.take();
      if (!next) return;
      const std::size_t i = next->instance;
      if (next->decodes && !oneAfterAnother) {
        if (startThread(i)) continue;
      } else if (next->decodes) {
        slots[i].refusal = prepare(i);
      }
      step(i);
    }
  }

  bool startThread(std::size_t i) {
    try {
      if (beforeThreadStart) beforeThreadStart(i);
      const std::lock_guard<std::mutex> lock(threadsMutex);
      threads.emplace_back([this, i] {
        quietly([this, i] {
          slots[i].refusal = prepare(i);
          step(i);
          startReady();
        });
        // WHY: Increment last: done() must not see every thread returned while this one may still start another.
        ++returned;
      });
      return true;
    } catch (const std::exception& e) {
      slots[i].refusal = std::string("decode thread could not start: ") + e.what();
      return false;
    }
  }

  std::string prepare(std::size_t i) {
    try {
      if (setUp) setUp();
    } catch (const std::exception& e) {
      return std::string("decode thread set-up failed: ") + e.what();
    } catch (...) {
      return "decode thread set-up failed with unknown error";
    }
    try {
      auto view = views();
      if (!view) return std::move(view.error());
      slots[i].sink = std::make_unique<GroupingSink>(shared, i, (*view)->sampleCount());
      slots[i].view = std::move(*view);
      return {};
    } catch (const std::bad_alloc&) {
      return "out of memory for signal view";
    } catch (const std::exception& e) {
      return std::string("decoder failed: ") + e.what();
    } catch (...) {
      return "decoder failed with unknown error";
    }
  }

  void step(std::size_t i) {
    try {
      (void)stepper.run(i);
    } catch (const std::exception& e) {
      strand(i, std::string("decoder failed: ") + e.what());
    } catch (...) {
      strand(i, "decoder failed with unknown error");
    }
  }

  void strand(std::size_t i, const std::string& text) {
    const decode::ChainOutcome failed = std::unexpected(text);
    slots[i].stranded = true;
    postOnce(i, failed, nullptr);
    for (std::size_t j = i + 1; j < slots.size(); ++j) {
      const std::vector<decode::ChainBinding>& inputs = plan.instances[j].inputs;
      const bool reads = std::any_of(inputs.begin(), inputs.end(),
                                     [this](const decode::ChainBinding& b) { return slots[b.from].stranded.load(); });
      if (!reads) continue;
      slots[j].stranded = true;
      postOnce(j, failed, nullptr);
    }
  }

  // WHY: No exception may leave a stepper hook, or its instance never ends; a result that cannot be queued is lost.
  void postOnce(std::size_t i, const decode::ChainOutcome& outcome, std::shared_ptr<const DecodedAnnotations> result) {
    if (slots[i].posted.exchange(true)) return;
    try {
      post(shared.queue, shared.run, i, outcome ? Result(std::move(result)) : Result(std::unexpected(outcome.error())));
    } catch (...) {
    }
  }

  decode::ChainOutcome store(std::size_t i, decode::Decoder& decoder, decode::ChainOutcome outcome) {
    if (!outcome) return outcome;
    Slot& slot = slots[i];
    auto groups = slot.sink->finish();
    if (!groups) return std::unexpected(budgetFailureText(decoder, groups.error(), shared.budgetBytes));
    const std::size_t bytes = groups->memoryBytes();
    const std::size_t stored = groups->size();
    try {
      slot.finished = std::make_shared<const DecodedAnnotations>(std::move(*groups));
    } catch (const std::bad_alloc&) {
      slot.sink->memory().release(bytes);
      return std::unexpected(
        budgetFailureText(decoder, {waveform_view::GroupsRefusal::Reason::OutOfMemory, stored}, shared.budgetBytes));
    }
    return {};
  }

  decode::ChainRunHooks makeHooks() {
    decode::ChainRunHooks hooks;
    hooks.before_instance = [this](std::size_t i) -> decode::ChainOutcome {
      Slot& slot = slots[i];
      if (shared.cancel.load()) return std::unexpected(std::string("cancelled"));
      if (!slot.refusal.empty()) return std::unexpected(slot.refusal);
      if (auto* plugin = pluginOf(*plan.instances[i].decoder)) plugin->setCancel(&shared.cancel, kCancelLimit);
      slot.started = true;
      slot.sink->start();
      return {};
    };
    hooks.decode_samples = [this](std::size_t i, decode::Decoder& decoder, decode::AnnotationSink& sink,
                                  decode::StreamOutputs* outputs) {
      return guarded([&] {
        auto* plugin = pluginOf(decoder);
        if (plugin != nullptr && capture) {
          return store(i, decoder, plugin->decodeCapture(*capture->buffer, capture->extent, capture->tail, sink, outputs));
        }
        return store(i, decoder, decoder.decode(*slots[i].view, sink, outputs));
      });
    };
    hooks.decode_streams = [this](std::size_t i, decode::Decoder& decoder, decode::StreamInputs& inputs,
                                  const decode::CaptureFacts&, decode::AnnotationSink& sink,
                                  decode::StreamOutputs* outputs) {
      return guarded([&] {
        return store(i, decoder, decoder.decodeStreams(inputs, factsOf(*slots[i].view), sink, outputs));
      });
    };
    hooks.decode_mixed = [this](std::size_t i, decode::Decoder& decoder, decode::StreamInputs& inputs,
                                const decode::CaptureFacts&, decode::AnnotationSink& sink,
                                decode::StreamOutputs* outputs) {
      return guarded([&] {
        const decode::SignalView& view = *slots[i].view;
        auto* plugin = pluginOf(decoder);
        if (plugin != nullptr && capture) {
          return store(i, decoder,
                       plugin->decodeCaptureMixed(*capture->buffer, capture->extent, capture->tail, inputs, sink, outputs));
        }
        return store(i, decoder, decoder.decodeMixed(view, inputs, factsOf(view), sink, outputs));
      });
    };
    hooks.after_instance = [this](std::size_t i, const decode::ChainOutcome& outcome) {
      Slot& slot = slots[i];
      std::shared_ptr<const DecodedAnnotations> result = std::move(slot.finished);
      slot.sink.reset();
      slot.view.reset();
      if (shared.cancel.load() && (!slot.started || !outcome)) return;
      postOnce(i, outcome, std::move(result));
    };
    return hooks;
  }

  const SignalViewSource views;
  const bool oneAfterAnother;
  const DecodeThreadSetup setUp;
  const std::function<void(std::size_t)> beforeThreadStart;
  const std::optional<RunCapture> capture;
  decode::ChainPlan plan;
  RunShared shared;
  std::vector<Slot> slots;
  const UnusedView unused{};
  decode::ChainStepper stepper;
  std::atomic<std::size_t> returned{0};
  std::mutex threadsMutex;
  std::vector<std::thread> threads;
};

DecoderRunner::DecoderRunner(std::uint64_t run, const decode::SignalView& view,
                             std::vector<ConfiguredDecoderRun> decoders, gui_events::GuiEventQueue& queue,
                             DecodeMemory memory)
    : DecoderRunner(run, view, independent(std::move(decoders)), queue, std::move(memory)) {}

DecoderRunner::DecoderRunner(std::uint64_t run, const decode::SignalView& view, decode::ChainPlan plan,
                             gui_events::GuiEventQueue& queue, DecodeMemory memory)
    : work_(std::make_unique<Work>(run, borrowed(view), true, std::move(plan), queue, std::move(memory),
                                   DecodeThreadSetup{}, ProgressClock{}, std::function<void(std::size_t)>{}, std::nullopt,
                                   cancel_)) {
  work_->begin();
}

DecoderRunner::DecoderRunner(std::uint64_t run, SignalViewSource views, decode::ChainPlan plan,
                             gui_events::GuiEventQueue& queue, DecodeMemory memory, DecodeThreadSetup threadSetup,
                             ProgressClock clock, std::function<void(std::size_t)> beforeThreadStart,
                             std::optional<RunCapture> capture)
    : work_(std::make_unique<Work>(run, std::move(views), false, std::move(plan), queue, std::move(memory),
                                   std::move(threadSetup), std::move(clock), std::move(beforeThreadStart),
                                   std::move(capture), cancel_)) {
  work_->begin();
}

DecoderRunner::~DecoderRunner() {
  cancel();
  join();
}

void DecoderRunner::cancel() {
  std::lock_guard<std::mutex> lock(work_->shared.progressMutex);
  cancel_ = true;
}

bool DecoderRunner::done() const {
  const std::lock_guard<std::mutex> lock(work_->threadsMutex);
  return work_->returned.load() == work_->threads.size();
}

void DecoderRunner::join() {
  for (std::size_t k = 0;; ++k) {
    std::thread* thread = nullptr;
    {
      const std::lock_guard<std::mutex> lock(work_->threadsMutex);
      if (k == work_->threads.size()) return;
      thread = &work_->threads[k];
    }
    if (thread->joinable()) thread->join();
  }
}

}

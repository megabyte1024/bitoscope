/*
 * SPDX-License-Identifier: MIT
 */

#pragma once


#include <cstddef>
#include <cstdint>
#include <expected>
#include <functional>
#include <mutex>
#include <optional>
#include <span>
#include <vector>

#include "decoder.h"
#include "decoder_chain.h"
#include "stream.h"

namespace bitoscope::decode {

enum class ChainStepRefusal {
  NoSuchInstance,
  Waiting,
  Running,
  Ended,
};

struct ChainStep {
  std::size_t instance = 0;
  bool decodes = true;
};

class ChainStepper {
 public:
  ChainStepper(std::span<ChainInstance> instances, const SignalView& view,
               std::function<AnnotationSink&(std::size_t)> sink_for, ChainRunHooks hooks = {});
  ChainStepper(const ChainStepper&) = delete;
  ChainStepper& operator=(const ChainStepper&) = delete;

  std::size_t size() const;

  std::vector<std::size_t> ready() const;

  std::optional<ChainStep> take();

  std::expected<ChainOutcome, ChainStepRefusal> run(std::size_t instance);

  std::optional<ChainOutcome> outcome(std::size_t instance) const;

  bool finished() const;

 private:
  enum class Phase : std::uint8_t { NotStarted, Taken, Running, Ended };

  bool inputsEndedLocked(std::size_t instance) const;
  const ChainBinding* failedInputLocked(std::size_t instance) const;
  ChainOutcome decode(std::size_t instance);

  const std::span<ChainInstance> instances_;
  const SignalView& view_;
  const std::function<AnnotationSink&(std::size_t)> sink_for_;
  const ChainRunHooks hooks_;

  std::vector<std::vector<std::vector<StreamItem>>> streams_;

  mutable std::mutex mutex_;
  std::vector<Phase> phase_;
  std::vector<ChainOutcome> outcomes_;
  std::vector<std::vector<std::size_t>> readers_left_;
  std::size_t ended_ = 0;
};

}

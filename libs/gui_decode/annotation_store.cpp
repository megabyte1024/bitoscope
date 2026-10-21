/*
 * SPDX-License-Identifier: MIT
 */

#include "annotation_store.h"

#include <algorithm>
#include <limits>
#include <utility>

namespace bitoscope::gui_decode {

void AnnotationStore::ensureSize(std::size_t index) {
  if (index >= decoders_.size()) decoders_.resize(index + 1);
}

void AnnotationStore::set(std::size_t index, std::shared_ptr<const DecodedAnnotations> annotations,
                          DecoderConfig::AnnotationFormatContext format, DerivationContext derivation) {
  ensureSize(index);
  decoders_[index].current = ShownResult{std::move(annotations), std::move(format), std::move(derivation)};
  decoders_[index].previous = {};
  decoders_[index].progress.reset();
  decoders_[index].state = DecoderLaneState::Ready;
  decoders_[index].error.clear();
}

void AnnotationStore::set(std::size_t index, std::vector<bitoscope::decode::Annotation> annotations) {
  set(index, std::make_shared<const DecodedAnnotations>(std::move(annotations)));
}

void AnnotationStore::startDecoding(std::size_t index) {
  ensureSize(index);
  decoders_[index].current = {};
  decoders_[index].previous = {};
  decoders_[index].progress.reset();
  decoders_[index].state = DecoderLaneState::Decoding;
  decoders_[index].error.clear();
}

void AnnotationStore::captureLetGo() {
  clearAll();
  run_ = 0;
  runIndex_.clear();
}

void AnnotationStore::beginRun(std::uint64_t run, std::vector<std::size_t> configIndices,
                               std::vector<DecoderConfig::AnnotationFormatContext> formats,
                               std::vector<DerivationContext> derivations) {
  for (Entry& entry : decoders_) entry.progress.reset();
  for (std::size_t k = 0; k < configIndices.size(); ++k) {
    const std::size_t index = configIndices[k];
    ensureSize(index);
    Entry& entry = decoders_[index];
    // WHY: Only a Ready result replaces the previous one: a superseded run must leave the last READY result to draw.
    if (entry.state == DecoderLaneState::Ready && entry.current.annotations) entry.previous = std::move(entry.current);
    entry.current = {};
    entry.runFormat = k < formats.size() ? std::move(formats[k]) : DecoderConfig::AnnotationFormatContext{};
    entry.runDerivation = k < derivations.size() ? std::move(derivations[k]) : DerivationContext{};
    entry.state = DecoderLaneState::Decoding;
    entry.error.clear();
  }
  run_ = run;
  runIndex_ = std::move(configIndices);
  dropSetAside();
}

void AnnotationStore::setAsideForRecompute() {
  for (Entry& entry : decoders_) {
    ShownResult shown;
    if (entry.state == DecoderLaneState::Ready && entry.current.annotations) {
      shown = std::move(entry.current);
    } else if (entry.state == DecoderLaneState::Decoding) {
      shown = std::move(entry.previous);
    }
    entry = Entry{};
    entry.previous = std::move(shown);
  }
  run_ = 0;
  runIndex_.clear();
}

void AnnotationStore::dropSetAside() {
  for (Entry& entry : decoders_) {
    if (entry.state != DecoderLaneState::Decoding) entry.previous = {};
  }
}

void AnnotationStore::removeDecoder(std::size_t index) {
  if (index < decoders_.size()) decoders_.erase(decoders_.begin() + static_cast<std::ptrdiff_t>(index));
  run_ = 0;
  runIndex_.clear();
}

bool AnnotationStore::resultArrived(
  std::uint64_t runId, std::size_t runPosition,
  const std::expected<std::shared_ptr<const DecodedAnnotations>, std::string>& result) {
  if (runId == 0 || runId != run_ || runPosition >= runIndex_.size()) return false;
  const std::size_t configIndex = runIndex_[runPosition];
  if (result.has_value()) {
    set(configIndex, *result, std::move(decoders_[configIndex].runFormat),
        std::move(decoders_[configIndex].runDerivation));
  } else {
    fail(configIndex, result.error());
  }
  return true;
}

namespace {

int decodePercent(std::uint64_t watermark, std::uint64_t captureSamples) {
  if (captureSamples == 0) return 0;
  if (watermark >= captureSamples) return 99;
  constexpr std::uint64_t kNoWrap = std::numeric_limits<std::uint64_t>::max() / 100;
  // WHY: Divide captureSamples by 100 before multiplying: watermark * 100 could wrap 64 bits.
  const std::uint64_t percent =
    watermark > kNoWrap ? watermark / (captureSamples / 100) : watermark * 100 / captureSamples;
  return static_cast<int>((std::min)(percent, std::uint64_t{99}));
}

}

bool AnnotationStore::progressArrived(std::uint64_t runId, std::size_t runPosition, const DecodeProgress& progress) {
  if (runId == 0 || runId != run_ || runPosition >= runIndex_.size()) return false;
  const std::size_t index = runIndex_[runPosition];
  // WHY: state() reads Ready past the end because clearAll() keeps the run but not its decoders.
  if (state(index) != DecoderLaneState::Decoding) return false;
  Entry& entry = decoders_[index];
  entry.progress = HeldProgress{LaneProgress{progress.snapshot, progress.watermark, progress.captureSamples,
                                             decodePercent(progress.watermark, progress.captureSamples)},
                                ShownResult{progress.snapshot, entry.runFormat, entry.runDerivation}};
  return true;
}

const LaneProgress* AnnotationStore::progress(std::size_t index) const {
  if (index >= decoders_.size() || !decoders_[index].progress) return nullptr;
  return &decoders_[index].progress->progress;
}

bool AnnotationStore::showsPrefix(std::size_t index) const {
  return progress(index) != nullptr && drawn(index) == nullptr;
}

const ShownResult* AnnotationStore::progressShown(std::size_t index) const {
  if (index >= decoders_.size() || !decoders_[index].progress) return nullptr;
  return &decoders_[index].progress->shown;
}

void AnnotationStore::fail(std::size_t index, std::string error) {
  ensureSize(index);
  decoders_[index].current = {};
  decoders_[index].previous = {};
  decoders_[index].progress.reset();
  decoders_[index].state = DecoderLaneState::Failed;
  decoders_[index].error = std::move(error);
}

void AnnotationStore::clear(std::size_t index) {
  if (index < decoders_.size()) {
    decoders_[index].current = {};
    decoders_[index].previous = {};
    decoders_[index].progress.reset();
    decoders_[index].state = DecoderLaneState::Ready;
    decoders_[index].error.clear();
  }
}

void AnnotationStore::clearAll() { decoders_.clear(); }

const DecodedAnnotations* AnnotationStore::decoded(std::size_t index) const {
  if (index >= decoders_.size()) return nullptr;
  return decoders_[index].current.annotations.get();
}

const ShownResult* AnnotationStore::drawn(std::size_t index) const {
  if (index >= decoders_.size()) return nullptr;
  const Entry& entry = decoders_[index];
  if (entry.state == DecoderLaneState::Ready && entry.current.annotations) return &entry.current;
  if (entry.state == DecoderLaneState::Decoding && entry.previous.annotations) return &entry.previous;
  return nullptr;
}

std::vector<bitoscope::decode::Annotation> AnnotationStore::query(std::size_t index, std::uint64_t begin,
                                                                   std::uint64_t end) const {
  const DecodedAnnotations* data = decoded(index);
  if (data == nullptr) return {};
  std::vector<bitoscope::decode::Annotation> out = data->groups().collect(data->groups().range(begin, end));
  std::erase_if(out, [begin](const bitoscope::decode::Annotation& a) { return a.end <= begin; });
  return out;
}

std::uint64_t AnnotationStore::heldBytes() const {
  std::uint64_t bytes = 0;
  for (const Entry& entry : decoders_) {
    if (entry.current.annotations) bytes += entry.current.annotations->groups().memoryBytes();
    if (entry.previous.annotations) bytes += entry.previous.annotations->groups().memoryBytes();
  }
  return bytes;
}

DecoderLaneState AnnotationStore::state(std::size_t index) const {
  if (index >= decoders_.size()) return DecoderLaneState::Ready;
  return decoders_[index].state;
}

const std::string& AnnotationStore::error(std::size_t index) const {
  static const std::string kEmpty;
  if (index >= decoders_.size() || decoders_[index].state != DecoderLaneState::Failed) return kEmpty;
  return decoders_[index].error;
}

bool laneHeaderShowsDecoding(const AnnotationStore& store, std::size_t index) {
  return store.state(index) == DecoderLaneState::Decoding && store.drawn(index) == nullptr;
}

DecoderConfig::AnnotationFormatContext laneFormatContext(const AnnotationStore& store, std::size_t index,
                                                         const DecoderConfig& config,
                                                         const bitoscope::decode::DecoderRegistry& registry) {
  if (const ShownResult* shown = store.drawn(index)) return shown->format;
  return config.annotationFormatContext(registry);
}

}

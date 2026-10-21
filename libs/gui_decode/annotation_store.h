/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "decode_progress.h"
#include "decoded_annotations.h"
#include "decoder.h"
#include "decoder_config.h"
#include "derivation_context.h"

namespace bitoscope::gui_decode {

enum class DecoderLaneState { Decoding, Ready, Failed };

// WHY: ShownResult keeps the contexts of the run that decoded it, not the config's current (maybe newer) ones.
struct ShownResult {
  std::shared_ptr<const DecodedAnnotations> annotations;
  DecoderConfig::AnnotationFormatContext format;
  DerivationContext derivation;
};

struct LaneProgress {
  std::shared_ptr<const DecodedAnnotations> snapshot;
  std::uint64_t watermark = 0;
  std::uint64_t captureSamples = 0;
  int percent = 0;
};

class AnnotationStore {
 public:
  void set(std::size_t index, std::shared_ptr<const DecodedAnnotations> annotations,
           DecoderConfig::AnnotationFormatContext format = {}, DerivationContext derivation = {});
  void set(std::size_t index, std::vector<bitoscope::decode::Annotation> annotations);

  void startDecoding(std::size_t index);

  void fail(std::size_t index, std::string error);

  void clear(std::size_t index);
  void clearAll();

  std::vector<bitoscope::decode::Annotation> query(std::size_t index, std::uint64_t begin,
                                                     std::uint64_t end) const;

  void captureLetGo();
  void beginRun(std::uint64_t run, std::vector<std::size_t> configIndices,
                std::vector<DecoderConfig::AnnotationFormatContext> formats = {},
                std::vector<DerivationContext> derivations = {});
  void setAsideForRecompute();
  void dropSetAside();
  void removeDecoder(std::size_t index);
  std::uint64_t run() const { return run_; }
  bool resultArrived(std::uint64_t runId, std::size_t runPosition,
                     const std::expected<std::shared_ptr<const DecodedAnnotations>, std::string>& result);
  bool progressArrived(std::uint64_t runId, std::size_t runPosition, const DecodeProgress& progress);
  const LaneProgress* progress(std::size_t index) const;
  bool showsPrefix(std::size_t index) const;
  const ShownResult* progressShown(std::size_t index) const;

  const DecodedAnnotations* decoded(std::size_t index) const;
  const ShownResult* drawn(std::size_t index) const;

  std::size_t decoderCount() const { return decoders_.size(); }
  std::uint64_t heldBytes() const;

  DecoderLaneState state(std::size_t index) const;
  const std::string& error(std::size_t index) const;

 private:
  struct HeldProgress {
    LaneProgress progress;
    ShownResult shown;
  };
  struct Entry {
    ShownResult current;
    ShownResult previous;
    DecoderConfig::AnnotationFormatContext runFormat;
    DerivationContext runDerivation;
    std::optional<HeldProgress> progress;
    DecoderLaneState state = DecoderLaneState::Ready;
    std::string error;
  };
  void ensureSize(std::size_t index);

  std::vector<Entry> decoders_;
  std::uint64_t run_ = 0;
  std::vector<std::size_t> runIndex_;
};

bool laneHeaderShowsDecoding(const AnnotationStore& store, std::size_t index);

DecoderConfig::AnnotationFormatContext laneFormatContext(const AnnotationStore& store, std::size_t index,
                                                         const DecoderConfig& config,
                                                         const bitoscope::decode::DecoderRegistry& registry);

}

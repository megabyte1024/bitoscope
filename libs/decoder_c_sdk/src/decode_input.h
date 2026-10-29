/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <expected>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "annotation_blocks.h"
#include "decode_ring_reader.h"
#include "bitoscope_decoder.h"
#include "bitoscope_decoder_testing.h"

namespace bitoscope::plugin_c_sdk {

inline constexpr uint32_t kHeartbeatEvery = 1024;
inline constexpr size_t kAnnotationBatch = 1024;
inline constexpr size_t kEmitRoom = 256;
inline constexpr uint64_t kWatermarkStride = uint64_t{1} << 16;
inline constexpr uint64_t kDefaultProgressStep = 4096;
inline constexpr size_t kItemBatch = 1024;
inline constexpr uint64_t kInputWaitSliceMs = 50;
inline constexpr size_t kMemoryItemBatch = 1024;

struct ItemBatchIn {
  std::vector<bitoscope_stream_item> items;
  bool last = false;
};

class ItemSource {
 public:
  virtual ~ItemSource() = default;
  virtual std::expected<void, std::string> request(uint32_t input) = 0;
  virtual std::expected<std::optional<ItemBatchIn>, std::string> await(uint32_t input, uint64_t wait_ms) = 0;
};

struct ItemInputSpec {
  std::string name;
  bool bound = false;
};

std::unique_ptr<ItemSource> memoryItems(std::vector<std::vector<bitoscope_stream_item>> items);

}

struct bitoscope_decode_input {
 public:
  static std::unique_ptr<bitoscope_decode_input> fromMemory(const uint8_t* const* planes, uint32_t num_channels,
                                                            uint64_t num_samples,
                                                            std::span<const bitoscope_memory_record> records);
  static std::unique_ptr<bitoscope_decode_input> fromRing(bitoscope::plugin_ring::DecodeRingReader reader);
  static std::unique_ptr<bitoscope_decode_input> fromItems(uint64_t total_samples,
                                                           std::vector<bitoscope::plugin_c_sdk::ItemInputSpec> inputs,
                                                           std::unique_ptr<bitoscope::plugin_c_sdk::ItemSource> source);
  std::expected<void, std::string> addItems(std::vector<bitoscope::plugin_c_sdk::ItemInputSpec> inputs,
                                            std::unique_ptr<bitoscope::plugin_c_sdk::ItemSource> source);

  bitoscope_decode_input(const bitoscope_decode_input&) = delete;
  bitoscope_decode_input& operator=(const bitoscope_decode_input&) = delete;
  ~bitoscope_decode_input();

  uint32_t channels() const { return channels_; }

  void countScans(uint64_t* counter) { scanned_ = counter; }
  uint64_t* scanCounter() const { return scanned_; }
  void noteScanned(uint64_t words) {
    if (scanned_ != nullptr) *scanned_ += words;
  }

  void setProgressStep(uint64_t step) { progress_step_ = step; }
  void setBound(uint64_t samples) { bound_ = samples; }
  uint64_t progressStep() const { return progress_step_; }
  uint64_t effectiveProgressStep() const {
    return progress_step_ != 0 ? progress_step_ : bitoscope::plugin_c_sdk::kDefaultProgressStep;
  }
  uint64_t watermarkStride() const {
    return progress_step_ != 0 ? progress_step_ : bitoscope::plugin_c_sdk::kWatermarkStride;
  }

  bool failed() const { return failed_; }
  const std::string& error() const { return error_; }
  void fail(std::string reason);
  void failIntegrity(std::string reason);
  bool integrityFailed() const { return integrity_; }
  void failThrown() noexcept;
  // WHY: cancel() is the only member that may be called from any thread.
  void cancel() { cancelled_.store(true, std::memory_order_relaxed); }
  // WHY: heartbeat() may run only while no other thread uses the input.
  void heartbeat() {
    if (reader_) reader_->heartbeat();
  }

  void tick() {
    if (--countdown_ == 0) beat();
  }
  void tick(uint32_t n) {
    if (countdown_ > n) {
      countdown_ -= n;
    } else {
      countdown_ = 1;
      tick();
    }
  }
  const bitoscope_span* spanAt(uint64_t sample);
  bool checkChannel(uint32_t channel);
  void releaseBefore(uint64_t sample);
  uint64_t arrived() const { return arrived_; }
  bool ended() const { return ended_; }
  void takeArrived();
  bool reaches(uint64_t samples);
  bool drain();
  const uint64_t& releasedMark() const { return mark_; }

  bool itemInput() const { return items_ != nullptr; }
  bool holdsSamples() const { return samples_; }
  bool readsRing() const { return reader_.has_value(); }
  int nextItem(uint32_t input, bitoscope_stream_item* item);
  uint64_t itemsRead() const;

  uint64_t waits() const { return waits_; }
  size_t windowRecords() const { return window_.size(); }

 private:
  struct Span {
    bitoscope_span view{};
    uint64_t slot = 0;
    uint64_t end() const { return view.first_sample + view.num_samples; }
  };

  bitoscope_decode_input() = default;

  void beat();
  bool fetch(uint64_t wanted);
  void hold(const bitoscope::plugin_ring::DecodeRecord& record);
  void trim();
  void letGoOfOlderHalf(uint64_t wanted);
  void failLookBack(uint64_t sample);

  uint64_t* scanned_ = nullptr;
  uint32_t channels_ = 0;
  bool samples_ = true;
  std::deque<Span> window_;
  uint64_t window_base_ = 0;
  uint64_t hint_ = 0;
  uint64_t mark_ = 0;
  uint64_t released_by_decoder_ = 0;
  uint64_t let_go_for_ = 0;
  uint64_t arrived_ = 0;
  bool ended_ = false;
  bool failed_ = false;
  bool integrity_ = false;
  std::string error_;
  std::atomic<bool> cancelled_{false};
  uint32_t countdown_ = bitoscope::plugin_c_sdk::kHeartbeatEvery;
  uint64_t waits_ = 0;
  uint64_t progress_step_ = 0;
  std::optional<uint64_t> bound_;
  std::optional<bitoscope::plugin_ring::DecodeRingReader> reader_;
  std::vector<std::vector<uint64_t>> storage_;
  struct Items;
  std::unique_ptr<Items> items_;
};

struct bitoscope_annotations {
 public:
  using Deliver =
      std::function<std::expected<void, std::string>(std::span<const bitoscope_annotation>, uint64_t settled)>;
  using DeliverItems =
      std::function<std::expected<void, std::string>(uint32_t output, std::span<const bitoscope_stream_item>)>;
  using DeliverBytes = std::function<std::expected<void, std::string>(std::span<const std::vector<uint8_t>>)>;
  using CheckBatch =
      std::function<std::optional<std::string>(std::span<const bitoscope_annotation>, size_t byte_values)>;

  explicit bitoscope_annotations(bitoscope_decode_input* in, Deliver deliver = {},
                                 size_t batch = bitoscope::plugin_c_sdk::kAnnotationBatch);
  ~bitoscope_annotations();
  bitoscope_annotations(const bitoscope_annotations&) = delete;
  bitoscope_annotations& operator=(const bitoscope_annotations&) = delete;

  bitoscope_decode_input* input() const { return in_; }
  int emit(uint64_t begin, uint64_t end, uint32_t row, uint32_t cls, uint64_t value);
  bitoscope_emit_cursor* room() { return &cursor_; }
  int commitRoom();
  int emitBytes(uint64_t begin, uint64_t end, uint32_t row, uint32_t cls, std::span<const uint8_t> bytes);
  void onBytes(DeliverBytes deliver) { deliver_bytes_ = std::move(deliver); }
  void checkBatches(CheckBatch check) { check_ = std::move(check); }
  std::span<const uint8_t> collectedBytes(uint64_t reference) const;
  int settle(uint64_t sample);
  uint64_t settleDue() const;
  uint64_t settleCalls() const { return settle_calls_; }
  int markEnd(uint64_t end);
  int finish();
  const bitoscope::plugin_c_sdk::AnnotationBlocks& collected() const { return collected_; }
  const std::vector<bitoscope_settled>& settles() const { return settles_; }
  size_t batches() const { return batches_; }

  std::expected<void, std::string> openOutputs(std::span<const bitoscope_job_output> outputs, uint64_t samples,
                                               DeliverItems deliver = {},
                                               size_t batch = bitoscope::plugin_c_sdk::kItemBatch);
  int put(uint32_t output, const bitoscope_stream_item& item);
  bool itemsEndWithin(uint64_t samples);
  std::span<const bitoscope_stream_item> collectedItems(uint32_t output) const;

 private:
  struct Items;

  int admit(const bitoscope_annotation& a);
  int flushRoom();
  void openRoom();
  void closeRoom();
  int closed();
  int closedIfFailed(int status);
  uint64_t endLimit() const;
  bool admits(const bitoscope_annotation& a, uint64_t limit) const;
  void refuse(const bitoscope_annotation& a);
  bool handOn(size_t n);
  bool handOnItems(uint32_t output);
  bool releaseWaiting();
  bool strideDue() const;

  bitoscope_decode_input* in_;
  Deliver deliver_;
  size_t batch_;
  std::vector<bitoscope_annotation> pending_;
  std::vector<bitoscope_annotation> room_;
  bitoscope_emit_cursor cursor_{};
  size_t checked_ = 0;
  bitoscope::plugin_c_sdk::AnnotationBlocks collected_;
  DeliverBytes deliver_bytes_;
  CheckBatch check_;
  std::vector<std::vector<uint8_t>> byte_values_;
  std::vector<uint64_t> byte_owners_;
  std::vector<std::vector<uint8_t>> collected_bytes_;
  std::vector<bitoscope_settled> settles_;
  uint64_t handed_ = 0;
  uint64_t given_ = 0;
  uint64_t ready_ = 0;
  uint64_t passed_ = 0;
  uint64_t waiting_ = 0;
  uint64_t waiting_at_ = 0;
  size_t batches_ = 0;
  uint64_t settle_calls_ = 0;
  bool finished_ = false;
  std::unique_ptr<Items> items_;
};

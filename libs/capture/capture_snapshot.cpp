/*
 * SPDX-License-Identifier: MIT
 */

#include "capture_snapshot.h"

#include <algorithm>
#include <format>
#include <new>
#include <span>
#include <thread>

#include "sample_window.h"

namespace bitoscope::capture {

namespace {

constexpr uint8_t kHasTriggerOrigin = 1;
constexpr uint8_t kHasTriggerSampleIndex = 2;
constexpr uint64_t kWordSamples = 64;

uint64_t wordsFor(uint64_t samples) { return (samples + kWordSamples - 1) / kWordSamples; }

uint8_t presentOf(const std::optional<uint64_t>& trigger_origin, const std::optional<uint64_t>& trigger_sample_index) {
  return static_cast<uint8_t>((trigger_origin ? kHasTriggerOrigin : 0) |
                              (trigger_sample_index ? kHasTriggerSampleIndex : 0));
}

uint64_t wordOfBytes(std::span<const uint8_t> bytes, uint64_t word, uint64_t samples) {
  const uint64_t first = word * 8;
  // WHY: Stop at the tail's last byte: a channel's bytes can end there, and a whole word would read past them.
  const uint64_t end = (std::min)(first + 8, (samples + 7) / 8);
  uint64_t value = 0;
  for (uint64_t b = first; b < end; ++b) value |= uint64_t{bytes[static_cast<size_t>(b)]} << (8 * (b - first));
  const uint64_t bits = samples - word * kWordSamples;
  return bits >= kWordSamples ? value : value & ((uint64_t{1} << bits) - 1);
}

}

bool CaptureTail::level(uint32_t channel, uint64_t sample) const {
  const uint64_t at = sample - first_sample;
  if (at >= count) return false;
  return ((word(channel, at / kWordSamples) >> (at % kWordSamples)) & 1u) != 0;
}

uint64_t CaptureTail::word(uint32_t channel, uint64_t index) const {
  if (channel >= channels || index >= words_per_channel) return 0;
  return words[static_cast<size_t>(channel * words_per_channel + index)];
}

std::expected<void, std::string> PublishedCaptureSnapshot::reserveTail(uint32_t channels, uint32_t window_samples) {
  const uint64_t stride = wordsFor(window_samples);
  try {
    tail_words_ = std::make_unique<std::atomic<uint64_t>[]>(static_cast<size_t>(uint64_t{channels} * stride));
  } catch (const std::bad_alloc&) {
    return std::unexpected(std::format(
        "out of memory for live view of window still being filled ({} channels of {} samples)", channels,
        window_samples));
  }
  tail_channels_ = channels;
  tail_capacity_ = window_samples;
  tail_stride_ = stride;
  return {};
}

void PublishedCaptureSnapshot::publish(CaptureExtent extent, std::optional<uint64_t> trigger_origin,
                                       std::optional<uint64_t> trigger_sample_index) {
  (void)store(extent, trigger_origin, trigger_sample_index, nullptr, 0);
}

bool PublishedCaptureSnapshot::publishTail(CaptureExtent extent, std::optional<uint64_t> trigger_origin,
                                           std::optional<uint64_t> trigger_sample_index,
                                           const bitoscope::host::WindowLease* window, uint64_t tail_samples) {
  if (tail_samples > tail_capacity_ || (tail_samples != 0 && window == nullptr)) return false;
  return store(extent, trigger_origin, trigger_sample_index, window, tail_samples);
}

bool PublishedCaptureSnapshot::holds(CaptureExtent extent, uint8_t present, uint64_t origin, uint64_t index,
                                     uint64_t tail_samples) const {
  return first_sample_.load(std::memory_order_relaxed) == extent.first_sample &&
         count_.load(std::memory_order_relaxed) == extent.count &&
         present_.load(std::memory_order_relaxed) == present &&
         trigger_origin_.load(std::memory_order_relaxed) == origin &&
         trigger_sample_index_.load(std::memory_order_relaxed) == index &&
         tail_samples_.load(std::memory_order_relaxed) == tail_samples;
}

bool PublishedCaptureSnapshot::store(CaptureExtent extent, std::optional<uint64_t> trigger_origin,
                                     std::optional<uint64_t> trigger_sample_index,
                                     const bitoscope::host::WindowLease* window, uint64_t tail_samples) {
  const uint8_t present = presentOf(trigger_origin, trigger_sample_index);
  const uint64_t origin = trigger_origin.value_or(0);
  const uint64_t index = trigger_sample_index.value_or(0);
  if (holds(extent, present, origin, index, tail_samples)) return false;
  // WHY: One writer at a time (the caller's lock): the sequence is bumped by plain stores, not read-modify-write.
  const uint64_t sequence = sequence_.load(std::memory_order_relaxed);
  sequence_.store(sequence + 1, std::memory_order_relaxed);
  // WHY: The release fence keeps the field stores below from moving before the odd sequence store.
  std::atomic_thread_fence(std::memory_order_release);
  first_sample_.store(extent.first_sample, std::memory_order_relaxed);
  count_.store(extent.count, std::memory_order_relaxed);
  trigger_origin_.store(origin, std::memory_order_relaxed);
  trigger_sample_index_.store(index, std::memory_order_relaxed);
  present_.store(present, std::memory_order_relaxed);
  tail_samples_.store(tail_samples, std::memory_order_relaxed);
  if (tail_samples != 0) {
    const uint64_t words = wordsFor(tail_samples);
    for (uint32_t c = 0; c < tail_channels_; ++c) {
      const std::span<const uint8_t> bytes = window->channel(c);
      std::atomic<uint64_t>* out = &tail_words_[static_cast<size_t>(c * tail_stride_)];
      for (uint64_t w = 0; w < words; ++w) {
        out[w].store(wordOfBytes(bytes, w, tail_samples), std::memory_order_relaxed);
      }
    }
  }
  sequence_.store(sequence + 2, std::memory_order_release);
  return true;
}

CaptureSnapshot PublishedCaptureSnapshot::read() const { return readInto(nullptr); }

CaptureSnapshot PublishedCaptureSnapshot::read(CaptureTail& tail) const { return readInto(&tail); }

void PublishedCaptureSnapshot::copyTail(CaptureTail& tail, uint64_t tail_samples) const {
  const uint64_t words = wordsFor(tail_samples);
  tail.words.reserve(static_cast<size_t>(uint64_t{tail_channels_} * tail_stride_));
  tail.words.resize(static_cast<size_t>(uint64_t{tail_channels_} * words));
  for (uint32_t c = 0; c < tail_channels_; ++c) {
    const std::atomic<uint64_t>* in = &tail_words_[static_cast<size_t>(c * tail_stride_)];
    uint64_t* out = tail.words.data() + static_cast<size_t>(c * words);
    for (uint64_t w = 0; w < words; ++w) out[w] = in[w].load(std::memory_order_relaxed);
  }
}

CaptureSnapshot PublishedCaptureSnapshot::readInto(CaptureTail* tail) const {
  for (;;) {
    const uint64_t before = sequence_.load(std::memory_order_acquire);
    if (before % 2 != 0) {
      std::this_thread::yield();
      continue;
    }
    const uint64_t generation = before / 2;
    CaptureSnapshot snapshot;
    snapshot.extent.first_sample = first_sample_.load(std::memory_order_relaxed);
    snapshot.extent.count = count_.load(std::memory_order_relaxed);
    const uint64_t origin = trigger_origin_.load(std::memory_order_relaxed);
    const uint64_t index = trigger_sample_index_.load(std::memory_order_relaxed);
    const uint8_t present = present_.load(std::memory_order_relaxed);
    const bool copy = tail != nullptr && tail->generation != generation;
    const uint64_t tail_samples = copy ? tail_samples_.load(std::memory_order_relaxed) : 0;
    if (copy) copyTail(*tail, tail_samples);
    // WHY: The acquire fence keeps the relaxed loads above from moving past the second sequence load.
    std::atomic_thread_fence(std::memory_order_acquire);
    if (sequence_.load(std::memory_order_relaxed) != before) continue;
    if ((present & kHasTriggerOrigin) != 0) snapshot.trigger_origin = origin;
    if ((present & kHasTriggerSampleIndex) != 0) snapshot.trigger_sample_index = index;
    snapshot.generation = generation;
    if (copy) {
      tail->generation = generation;
      tail->first_sample = snapshot.extent.first_sample + snapshot.extent.count;
      tail->count = tail_samples;
      tail->channels = tail_channels_;
      tail->words_per_channel = wordsFor(tail_samples);
    }
    return snapshot;
  }
}

}

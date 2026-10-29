/*
 * SPDX-License-Identifier: MIT
 */

#include "counter_signals.h"

#include <algorithm>
#include <bit>
#include <vector>

#include "channel_pack.h"

namespace bitoscope::sim {

std::expected<CounterStream, std::string> CounterStream::create(uint32_t channel_mask, WireEncoding encoding) {
  if (channel_mask == 0) return std::unexpected(std::string("channel mask selects no channels"));
  CounterStream stream;
  stream.channel_mask_ = channel_mask;
  stream.num_channels_ = static_cast<uint32_t>(std::popcount(channel_mask));
  stream.encoding_ = encoding;
  return stream;
}

uint64_t CounterStream::payloadBytes(uint64_t num_samples) const {
  return encoding_ == WireEncoding::SampleMajor ? num_samples * ((num_channels_ + 7) / 8)
                                                : num_samples / 8 * num_channels_;
}

std::expected<void, std::string> CounterStream::writeNext(uint64_t num_samples, std::span<uint8_t> out) {
  if (out.size() < payloadBytes(num_samples)) {
    return std::unexpected("counter: " + std::to_string(out.size()) + " bytes cannot hold " +
                           std::to_string(num_samples) + " samples");
  }
  std::vector<Word> words(static_cast<size_t>(num_samples));
  for (size_t i = 0; i < words.size(); ++i) words[i] = static_cast<Word>(produced_ + i);
  const std::vector<Word> selected = extractChannels(words, channel_mask_);
  std::vector<uint8_t> packed;
  if (encoding_ == WireEncoding::SampleMajor) {
    packed = packSampleMajor(selected, num_channels_);
  } else {
    auto channelMajor = packChannelMajor(selected, num_channels_);
    if (!channelMajor) return std::unexpected("counter: " + channelMajor.error());
    packed = std::move(*channelMajor);
  }
  std::copy(packed.begin(), packed.end(), out.begin());
  produced_ += num_samples;
  return {};
}

}

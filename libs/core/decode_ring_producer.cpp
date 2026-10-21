/*
 * SPDX-License-Identifier: MIT
 */

#include "decode_ring_producer.h"

#include <algorithm>
#include <cstring>
#include <limits>
#include <utility>

#include "sample_encoding.h"

namespace bitoscope::host {

namespace br = bitoscope::bulk_ring;

namespace {

constexpr uint32_t kChannelPlanes = static_cast<uint32_t>(SampleEncoding::ChannelPlanes);
constexpr uint32_t kSkipRecord = static_cast<uint32_t>(SampleEncoding::SkipRecord);

std::unexpected<DecodeRingError> error(DecodeRingErrorKind kind, std::string message) {
  return std::unexpected(DecodeRingError{kind, std::move(message)});
}

void copyPlane(std::span<uint8_t> dst, std::span<const uint8_t> src, uint64_t first_bit, uint64_t num_samples) {
  const size_t bytes = static_cast<size_t>((num_samples + 7) / 8);
  const size_t from = static_cast<size_t>(first_bit / 8);
  const unsigned shift = static_cast<unsigned>(first_bit % 8);
  if (shift == 0) {
    std::memcpy(dst.data(), src.data() + from, bytes);
  } else {
    const size_t last = src.size() - 1;
    for (size_t i = 0; i < bytes; ++i) {
      const unsigned lo = static_cast<unsigned>(src[from + i]) >> shift;
      const unsigned hi = from + i + 1 <= last ? static_cast<unsigned>(src[from + i + 1]) << (8 - shift) : 0u;
      dst[i] = static_cast<uint8_t>(lo | hi);
    }
  }
  if (num_samples % 8 != 0) {
    dst[bytes - 1] = static_cast<uint8_t>(dst[bytes - 1] & ((1u << (num_samples % 8)) - 1u));
  }
  std::memset(dst.data() + bytes, 0, dst.size() - bytes);
}

void appendBits(std::span<uint8_t> dst, uint64_t to, std::span<const uint8_t> src, uint64_t from, uint64_t num_samples) {
  if (to % 8 != 0 && num_samples != 0) {
    const unsigned at = static_cast<unsigned>(to % 8);
    const unsigned shift = static_cast<unsigned>(from % 8);
    const unsigned n = static_cast<unsigned>((std::min)(uint64_t{8 - at}, num_samples));
    unsigned bits = static_cast<unsigned>(src[static_cast<size_t>(from / 8)]) >> shift;
    if (shift + n > 8) bits |= static_cast<unsigned>(src[static_cast<size_t>(from / 8) + 1]) << (8 - shift);
    bits &= (1u << n) - 1u;
    dst[static_cast<size_t>(to / 8)] = static_cast<uint8_t>(dst[static_cast<size_t>(to / 8)] | (bits << at));
    to += n;
    from += n;
    num_samples -= n;
  }
  if (num_samples == 0) {
    return;
  }
  uint8_t* out = dst.data() + static_cast<size_t>(to / 8);
  const size_t bytes = static_cast<size_t>((num_samples + 7) / 8);
  const size_t first = static_cast<size_t>(from / 8);
  const unsigned shift = static_cast<unsigned>(from % 8);
  if (shift == 0) {
    std::memcpy(out, src.data() + first, bytes);
  } else {
    const size_t last = src.size() - 1;
    for (size_t i = 0; i < bytes; ++i) {
      const unsigned lo = static_cast<unsigned>(src[first + i]) >> shift;
      const unsigned hi = first + i + 1 <= last ? static_cast<unsigned>(src[first + i + 1]) << (8 - shift) : 0u;
      out[i] = static_cast<uint8_t>(lo | hi);
    }
  }
  if (num_samples % 8 != 0) {
    out[bytes - 1] = static_cast<uint8_t>(out[bytes - 1] & ((1u << (num_samples % 8)) - 1u));
  }
}

void appendLevel(std::span<uint8_t> dst, uint64_t to, uint64_t num_samples, bool level) {
  if (to % 8 != 0 && num_samples != 0) {
    const unsigned at = static_cast<unsigned>(to % 8);
    const unsigned n = static_cast<unsigned>((std::min)(uint64_t{8 - at}, num_samples));
    if (level) {
      dst[static_cast<size_t>(to / 8)] =
          static_cast<uint8_t>(dst[static_cast<size_t>(to / 8)] | (((1u << n) - 1u) << at));
    }
    to += n;
    num_samples -= n;
  }
  const size_t whole = static_cast<size_t>(num_samples / 8);
  uint8_t* out = dst.data() + static_cast<size_t>(to / 8);
  std::memset(out, level ? 0xFF : 0x00, whole);
  if (num_samples % 8 != 0) {
    out[whole] = level ? static_cast<uint8_t>((1u << (num_samples % 8)) - 1u) : uint8_t{0};
  }
}

}

std::expected<DecodeRingProducer, std::string> DecodeRingProducer::create(std::string_view name, uint64_t job_id,
                                                                          const DecodeRingSpec& spec,
                                                                          br::Timing timing) {
  if (spec.num_channels == 0) {
    return std::unexpected(std::string("decode job's ring needs at least one channel"));
  }
  const uint64_t words = spec.slot_payload_bytes / (uint64_t{spec.num_channels} * 8);
  if (words == 0) {
    return std::unexpected("cannot create decode ring \"" + std::string(name) + "\": slot of " +
                           std::to_string(spec.slot_payload_bytes) + " bytes cannot hold one 64-bit word of " +
                           std::to_string(spec.num_channels) + " channels (" +
                           std::to_string(uint64_t{spec.num_channels} * 8) + " bytes)");
  }
  auto ring = br::BulkRing::create(name, spec.slot_count, spec.slot_payload_bytes, job_id, std::move(timing),
                                   spec.flush_after_ms, br::CreatorRole::Producer);
  if (!ring) {
    return std::unexpected(ring.error());
  }
  DecodeRingProducer producer(std::move(*ring), spec.num_channels, spec.sample_rate_milli_hz);
  producer.max_chunk_samples_ = words > std::numeric_limits<uint64_t>::max() / 64 ? std::numeric_limits<uint64_t>::max() / 64 * 64
                                                                                    : words * 64;
  producer.fill_slots_ = spec.fill_slots;
  return producer;
}

DecodeRingProducer::DecodeRingProducer(br::BulkRing ring, uint32_t num_channels, uint64_t sample_rate_milli_hz)
    : ring_(std::move(ring)), num_channels_(num_channels), sample_rate_milli_hz_(sample_rate_milli_hz) {}

DecodeRingProducer::DecodeRingProducer(DecodeRingProducer&& other) noexcept
    : ring_(std::move(other.ring_)),
      live_(std::exchange(other.live_, false)),
      num_channels_(other.num_channels_),
      sample_rate_milli_hz_(other.sample_rate_milli_hz_),
      max_chunk_samples_(other.max_chunk_samples_),
      next_sample_(other.next_sample_),
      fill_slots_(other.fill_slots_),
      gathering_(std::exchange(other.gathering_, Gathering{})) {}

DecodeRingProducer& DecodeRingProducer::operator=(DecodeRingProducer&& other) noexcept {
  if (this != &other) {
    ring_ = std::move(other.ring_);
    live_ = std::exchange(other.live_, false);
    num_channels_ = other.num_channels_;
    sample_rate_milli_hz_ = other.sample_rate_milli_hz_;
    max_chunk_samples_ = other.max_chunk_samples_;
    next_sample_ = other.next_sample_;
    fill_slots_ = other.fill_slots_;
    gathering_ = std::exchange(other.gathering_, Gathering{});
  }
  return *this;
}

DecodeRingProducer::~DecodeRingProducer() {
  if (live_ && ring_.producerState() == br::ProducerState::Attached) {
    (void)commitGathered();
    (void)ring_.markFailed(next_sample_, 0, "host abandoned decode job before end of its samples");
  }
}

std::expected<std::span<uint8_t>, DecodeRingError> DecodeRingProducer::room(uint64_t bytes, uint64_t pending,
                                                                            uint64_t deadline_ms) {
  auto slot = ring_.tryAcquire(bytes);
  if (!slot && slot.error().kind == br::AcquireErrorKind::Full) {
    slot = ring_.acquire(bytes, deadline_ms);
  }
  if (slot) {
    return *slot;
  }
  const br::AcquireError& e = slot.error();
  switch (e.kind) {
    case br::AcquireErrorKind::Timeout:
      return error(DecodeRingErrorKind::Timeout, e.message + "; samples before " + std::to_string(next_sample_) +
                                                     " were delivered, rest was not written");
    case br::AcquireErrorKind::ConsumerClosed:
      return error(DecodeRingErrorKind::ConsumerClosed,
                   e.message + "; nothing from sample " + std::to_string(next_sample_) + " on is delivered");
    case br::AcquireErrorKind::ConsumerStale:
      (void)ring_.markFailed(next_sample_, pending, "decoder plugin stopped reading (its heartbeat went stale)");
      return error(DecodeRingErrorKind::ConsumerStale,
                   e.message + "; nothing from sample " + std::to_string(next_sample_) + " on is delivered");
    case br::AcquireErrorKind::ConsumerInvalid:
      (void)ring_.markFailed(next_sample_, pending, "decoder plugin broke its ring (read count no reader can have)");
      return error(DecodeRingErrorKind::ConsumerInvalid,
                   "decoder plugin went wrong: " + e.message + "; nothing from sample " +
                       std::to_string(next_sample_) + " on is delivered");
    case br::AcquireErrorKind::ProducerFinished:
      return error(DecodeRingErrorKind::Finished, e.message);
    case br::AcquireErrorKind::Full:
    case br::AcquireErrorKind::TooLarge:
      break;
  }
  return error(DecodeRingErrorKind::BadInput, e.message);
}

std::expected<void, DecodeRingError> DecodeRingProducer::commitRecord(uint32_t encoding, uint64_t num_samples,
                                                                      uint64_t bytes) {
  if (auto committed = ring_.commit(br::ChunkHeader{next_sample_, num_samples, num_channels_, encoding,
                                                    sample_rate_milli_hz_, ring_.captureId(), bytes});
      !committed) {
    return error(DecodeRingErrorKind::BadInput, committed.error());
  }
  next_sample_ += num_samples;
  return {};
}

std::expected<void, DecodeRingError> DecodeRingProducer::writePlanes(std::span<const std::span<const uint8_t>> planes,
                                                                     uint64_t first_bit, uint64_t num_samples,
                                                                     uint64_t deadline_ms) {
  if (planes.size() != num_channels_) {
    return error(DecodeRingErrorKind::BadInput, std::to_string(planes.size()) + " planes were given for job of " +
                                                    std::to_string(num_channels_) + " channels");
  }
  if (num_samples == 0) {
    return {};
  }
  if (first_bit > std::numeric_limits<uint64_t>::max() - num_samples - 7) {
    return error(DecodeRingErrorKind::BadInput, "planes' bit range does not fit 64 bits");
  }
  const uint64_t needed = (first_bit + num_samples + 7) / 8;
  for (uint32_t c = 0; c < num_channels_; ++c) {
    if (planes[c].size() < needed) {
      return error(DecodeRingErrorKind::BadInput,
                   "channel " + std::to_string(c) + "'s plane holds " + std::to_string(planes[c].size()) +
                       " bytes; samples from bit " + std::to_string(first_bit) + " for " +
                       std::to_string(num_samples) + " samples need " + std::to_string(needed));
    }
  }
  if (fill_slots_) {
    return writePlanesWhole(planes, first_bit, num_samples, deadline_ms);
  }
  uint64_t done = 0;
  while (done < num_samples) {
    const uint64_t n = (std::min)(max_chunk_samples_, num_samples - done);
    const uint64_t stride = (n + 63) / 64 * 8;
    const uint64_t bytes = stride * num_channels_;
    auto slot = room(bytes, num_samples - done, deadline_ms);
    if (!slot) {
      return std::unexpected(std::move(slot.error()));
    }
    for (uint32_t c = 0; c < num_channels_; ++c) {
      copyPlane(slot->subspan(static_cast<size_t>(c * stride), static_cast<size_t>(stride)), planes[c],
                first_bit + done, n);
    }
    if (auto committed = commitRecord(kChannelPlanes, n, bytes); !committed) {
      return committed;
    }
    done += n;
  }
  return {};
}

std::expected<void, DecodeRingError> DecodeRingProducer::writeSkip(std::span<const uint8_t> levels,
                                                                   uint64_t num_samples, uint64_t deadline_ms) {
  const uint64_t bytes = (uint64_t{num_channels_} + 7) / 8;
  if (levels.size() < bytes) {
    return error(DecodeRingErrorKind::BadInput, "skip record of " + std::to_string(num_channels_) +
                                                    " channels needs " + std::to_string(bytes) +
                                                    " bytes of levels; " + std::to_string(levels.size()) +
                                                    " were given");
  }
  if (num_samples == 0) {
    return {};
  }
  if (fill_slots_) {
    return writeSkipWhole(levels, num_samples, deadline_ms);
  }
  auto slot = room(bytes, num_samples, deadline_ms);
  if (!slot) {
    return std::unexpected(std::move(slot.error()));
  }
  std::memcpy(slot->data(), levels.data(), static_cast<size_t>(bytes));
  if (num_channels_ % 8 != 0) {
    (*slot)[bytes - 1] = static_cast<uint8_t>((*slot)[bytes - 1] & ((1u << (num_channels_ % 8)) - 1u));
  }
  return commitRecord(kSkipRecord, num_samples, bytes);
}

std::expected<void, DecodeRingError> DecodeRingProducer::gather(uint64_t pending, uint64_t deadline_ms) {
  if (gathering_.held) {
    return {};
  }
  auto slot = room(max_chunk_samples_ / 8 * num_channels_, pending, deadline_ms);
  if (!slot) {
    return std::unexpected(std::move(slot.error()));
  }
  gathering_ = Gathering{*slot, 0, true};
  return {};
}

std::expected<void, DecodeRingError> DecodeRingProducer::commitGathered() {
  if (!gathering_.held) {
    return {};
  }
  gathering_.held = false;
  const uint64_t n = gathering_.samples;
  if (n == 0) {
    return {};
  }
  const uint64_t stride = (n + 63) / 64 * 8;
  if (n != max_chunk_samples_) {
    const uint64_t full = max_chunk_samples_ / 8;
    const uint64_t used = (n + 7) / 8;
    uint8_t* const base = gathering_.room.data();
    for (uint32_t c = 0; c < num_channels_; ++c) {
      if (c != 0 && stride != full) {
        std::memmove(base + c * stride, base + c * full, static_cast<size_t>(used));
      }
      std::memset(base + c * stride + used, 0, static_cast<size_t>(stride - used));
    }
  }
  const uint64_t bytes = stride * num_channels_;
  if (auto committed = ring_.commit(br::ChunkHeader{next_sample_ - n, n, num_channels_, kChannelPlanes,
                                                    sample_rate_milli_hz_, ring_.captureId(), bytes});
      !committed) {
    return error(DecodeRingErrorKind::BadInput, committed.error());
  }
  ring_.flush();
  return {};
}

std::expected<void, DecodeRingError> DecodeRingProducer::writePlanesWhole(
    std::span<const std::span<const uint8_t>> planes, uint64_t first_bit, uint64_t num_samples, uint64_t deadline_ms) {
  const uint64_t stride = max_chunk_samples_ / 8;
  uint64_t done = 0;
  while (done < num_samples) {
    if (auto held = gather(num_samples - done, deadline_ms); !held) {
      return held;
    }
    const uint64_t n = (std::min)(max_chunk_samples_ - gathering_.samples, num_samples - done);
    for (uint32_t c = 0; c < num_channels_; ++c) {
      appendBits(gathering_.room.subspan(static_cast<size_t>(c * stride), static_cast<size_t>(stride)),
                 gathering_.samples, planes[c], first_bit + done, n);
    }
    gathering_.samples += n;
    next_sample_ += n;
    done += n;
    if (gathering_.samples == max_chunk_samples_) {
      if (auto committed = commitGathered(); !committed) {
        return committed;
      }
    }
  }
  return {};
}

std::expected<void, DecodeRingError> DecodeRingProducer::gatherLevels(std::span<const uint8_t> levels,
                                                                     uint64_t num_samples) {
  const uint64_t stride = max_chunk_samples_ / 8;
  for (uint32_t c = 0; c < num_channels_; ++c) {
    appendLevel(gathering_.room.subspan(static_cast<size_t>(c * stride), static_cast<size_t>(stride)),
                gathering_.samples, num_samples, ((levels[c / 8] >> (c % 8)) & 1u) != 0);
  }
  gathering_.samples += num_samples;
  next_sample_ += num_samples;
  return gathering_.samples == max_chunk_samples_ ? commitGathered() : std::expected<void, DecodeRingError>{};
}

std::expected<void, DecodeRingError> DecodeRingProducer::writeSkipWhole(std::span<const uint8_t> levels,
                                                                       uint64_t num_samples, uint64_t deadline_ms) {
  uint64_t left = num_samples;
  if (gathering_.held) {
    const uint64_t n = (std::min)(max_chunk_samples_ - gathering_.samples, left);
    if (auto gathered = gatherLevels(levels, n); !gathered) {
      return gathered;
    }
    left -= n;
  }
  if (left == 0) {
    return {};
  }
  // WHY: A skip record shorter than a slot's planes would leave its slot holding less than they do.
  if (left < max_chunk_samples_) {
    if (auto held = gather(left, deadline_ms); !held) {
      return held;
    }
    return gatherLevels(levels, left);
  }
  const uint64_t bytes = (uint64_t{num_channels_} + 7) / 8;
  auto slot = room(bytes, left, deadline_ms);
  if (!slot) {
    return std::unexpected(std::move(slot.error()));
  }
  std::memcpy(slot->data(), levels.data(), static_cast<size_t>(bytes));
  if (num_channels_ % 8 != 0) {
    (*slot)[bytes - 1] = static_cast<uint8_t>((*slot)[bytes - 1] & ((1u << (num_channels_ % 8)) - 1u));
  }
  if (auto committed = commitRecord(kSkipRecord, left, bytes); !committed) {
    return committed;
  }
  ring_.flush();
  return {};
}

void DecodeRingProducer::flush() { ring_.flush(); }

std::expected<void, DecodeRingError> DecodeRingProducer::end() {
  if (auto committed = commitGathered(); !committed) {
    return committed;
  }
  if (auto ended = ring_.end(); !ended) {
    return error(DecodeRingErrorKind::Finished, ended.error());
  }
  return {};
}

std::expected<void, DecodeRingError> DecodeRingProducer::fail(std::string_view reason) {
  if (auto committed = commitGathered(); !committed) {
    return committed;
  }
  if (auto failed = ring_.markFailed(next_sample_, 0, reason); !failed) {
    return error(DecodeRingErrorKind::Finished, failed.error());
  }
  return {};
}

}

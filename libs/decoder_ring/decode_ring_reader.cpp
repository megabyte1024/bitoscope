/*
 * SPDX-License-Identifier: MIT
 */

#include "decode_ring_reader.h"

#include <utility>

#include "ring_encoding.h"

namespace bitoscope::plugin_ring {

namespace br = bitoscope::bulk_ring;

namespace {

std::string hexId(uint64_t v) {
  static constexpr char kDigits[] = "0123456789abcdef";
  std::string out = "0x";
  for (int shift = 60; shift >= 0; shift -= 4) {
    out += kDigits[(v >> shift) & 0xFu];
  }
  return out;
}

std::string inQuotes(std::string_view name) { return "\"" + std::string(name) + "\""; }

}

std::span<const uint8_t> DecodeRecord::plane(uint32_t channel) const {
  if (kind != DecodeRecordKind::Planes || channel >= num_channels) {
    return {};
  }
  return payload.subspan(static_cast<size_t>(channel * plane_stride), static_cast<size_t>(plane_stride));
}

bool DecodeRecord::level(uint32_t channel) const {
  if (kind != DecodeRecordKind::Skip || channel >= num_channels) {
    return false;
  }
  return ((payload[channel / 8] >> (channel % 8)) & 1u) != 0;
}

std::expected<DecodeRingReader, std::string> DecodeRingReader::open(std::string_view name, uint64_t job_id,
                                                                    uint32_t num_channels, br::Timing timing) {
  if (num_channels == 0) {
    return std::unexpected("cannot read decode ring " + inQuotes(name) + " for no channels");
  }
  auto ring = br::BulkRing::open(name, std::move(timing));
  if (!ring) {
    return std::unexpected("cannot read decode ring " + inQuotes(name) + ": " + ring.error());
  }
  if (ring->captureId() != job_id) {
    // WHY: Foreign ring: read nothing and do not close it, since its reader is someone else.
    return std::unexpected("decode ring " + inQuotes(name) + " was created for job " + hexId(ring->captureId()) +
                           ", but this is job " + hexId(job_id));
  }
  return DecodeRingReader(std::move(*ring), num_channels);
}

DecodeRingReader::DecodeRingReader(br::BulkRing ring, uint32_t num_channels)
    : ring_(std::move(ring)), num_channels_(num_channels) {}

DecodeRingReader::DecodeRingReader(DecodeRingReader&& other) noexcept
    : ring_(std::move(other.ring_)),
      live_(std::exchange(other.live_, false)),
      num_channels_(other.num_channels_),
      next_sample_(other.next_sample_),
      held_(std::exchange(other.held_, false)),
      held_record_(other.held_record_),
      invalid_(std::move(other.invalid_)) {}

DecodeRingReader& DecodeRingReader::operator=(DecodeRingReader&& other) noexcept {
  if (this != &other) {
    close();
    ring_ = std::move(other.ring_);
    live_ = std::exchange(other.live_, false);
    num_channels_ = other.num_channels_;
    next_sample_ = other.next_sample_;
    held_ = std::exchange(other.held_, false);
    held_record_ = other.held_record_;
    invalid_ = std::move(other.invalid_);
  }
  return *this;
}

DecodeRingReader::~DecodeRingReader() { close(); }

void DecodeRingReader::close() {
  if (live_) {
    ring_.close();
  }
}

DecodeRead DecodeRingReader::next(uint64_t deadline_ms) {
  if (!invalid_.empty()) {
    DecodeRead read;
    read.status = DecodeReadStatus::Invalid;
    read.error = invalid_;
    return read;
  }
  if (held_) {
    DecodeRead read;
    read.status = DecodeReadStatus::Record;
    read.record = held_record_;
    return read;
  }
  DecodeRead read = fetch(deadline_ms);
  if (read.status == DecodeReadStatus::Record) {
    held_ = true;
  }
  return read;
}

DecodeRead DecodeRingReader::take(uint64_t deadline_ms) {
  DecodeRead read;
  if (!invalid_.empty()) {
    read.status = DecodeReadStatus::Invalid;
    read.error = invalid_;
    return read;
  }
  if (held_) {
    read.status = DecodeReadStatus::Invalid;
    read.error = "take() on decode ring " + inQuotes(ring_.name()) + " while record from next() is held";
    return read;
  }
  if (ring_.retainedSlots() >= ring_.slotCount()) {
    ring_.heartbeat();
    const br::Poll poll = ring_.peek();
    switch (poll.status) {
      case br::PollStatus::Ended:
        read.status = DecodeReadStatus::Ended;
        return read;
      case br::PollStatus::Failed:
        read.status = DecodeReadStatus::Failed;
        read.failed = ring_.failedRecord();
        return read;
      case br::PollStatus::Invalid:
        invalid_ = poll.error;
        read.status = DecodeReadStatus::Invalid;
        read.error = invalid_;
        return read;
      case br::PollStatus::Empty:
      case br::PollStatus::Chunk:
        read.status = DecodeReadStatus::WindowFull;
        return read;
    }
  }
  read = fetch(deadline_ms);
  if (read.status == DecodeReadStatus::Record) {
    if (auto moved = ring_.advance(); !moved) {
      invalid_ = moved.error();
      read = DecodeRead{};
      read.status = DecodeReadStatus::Invalid;
      read.error = invalid_;
      return read;
    }
    next_sample_ = held_record_.first_sample + held_record_.num_samples;
  }
  return read;
}

std::expected<void, std::string> DecodeRingReader::releaseSlotsBefore(uint64_t slot) {
  return ring_.releaseSlotsBefore(slot);
}

DecodeRead DecodeRingReader::fetch(uint64_t deadline_ms) {
  DecodeRead read;
  ring_.heartbeat();
  const br::Poll poll = ring_.wait(deadline_ms);
  switch (poll.status) {
    case br::PollStatus::Empty:
      read.status = DecodeReadStatus::Pending;
      return read;
    case br::PollStatus::Ended:
      read.status = DecodeReadStatus::Ended;
      return read;
    case br::PollStatus::Failed:
      read.status = DecodeReadStatus::Failed;
      read.failed = ring_.failedRecord();
      return read;
    case br::PollStatus::Invalid:
      invalid_ = poll.error;
      read.status = DecodeReadStatus::Invalid;
      read.error = invalid_;
      return read;
    case br::PollStatus::Chunk:
      break;
  }

  const br::ChunkHeader& h = poll.chunk.header;
  const std::string where = "decode ring " + inQuotes(ring_.name()) + ": record at sample " +
                            std::to_string(h.first_sample_index) + " ";
  const auto refuse = [&](std::string what) {
    invalid_ = where + std::move(what) + "; nothing past it is read";
    read.status = DecodeReadStatus::Invalid;
    read.error = invalid_;
    return read;
  };
  if (h.capture_id != ring_.captureId()) {
    return refuse("belongs to job " + hexId(h.capture_id) + ", not this ring's job " + hexId(ring_.captureId()));
  }
  if (h.num_channels != num_channels_) {
    return refuse("carries " + std::to_string(h.num_channels) + " channels; job binds " +
                  std::to_string(num_channels_));
  }
  if (h.encoding != kRingEncodingChannelPlanes && h.encoding != kRingEncodingSkip) {
    return refuse("has encoding " + std::to_string(h.encoding) + "; decode job's ring carries channel planes (" +
                  std::to_string(kRingEncodingChannelPlanes) + ") and skip records (" +
                  std::to_string(kRingEncodingSkip) + ") only");
  }
  if (h.num_samples == 0) {
    return refuse("has no samples");
  }
  const auto bytes = chunkPayloadBytes(h.encoding, h.num_channels, h.num_samples);
  if (!bytes || *bytes != h.payload_bytes) {
    return refuse("carries " + std::to_string(h.payload_bytes) + " payload bytes; " +
                  std::to_string(h.num_samples) + " samples of " + std::to_string(h.num_channels) +
                  (h.encoding == kRingEncodingSkip ? " channels as skip record take " : " channels as planes take ") +
                  (bytes ? std::to_string(*bytes) : bytes.error()));
  }
  if (h.first_sample_index > next_sample_) {
    return refuse("comes after sample " + std::to_string(next_sample_) + ": samples " + std::to_string(next_sample_) +
                  " to " + std::to_string(h.first_sample_index - 1) + " are missing");
  }
  if (h.first_sample_index < next_sample_) {
    return refuse("repeats samples already read: expected sample " + std::to_string(next_sample_));
  }

  DecodeRecord& r = held_record_;
  r.kind = h.encoding == kRingEncodingSkip ? DecodeRecordKind::Skip : DecodeRecordKind::Planes;
  r.first_sample = h.first_sample_index;
  r.num_samples = h.num_samples;
  r.num_channels = h.num_channels;
  r.plane_stride = r.kind == DecodeRecordKind::Planes ? (h.num_samples + 63) / 64 * 8 : 0;
  r.payload = poll.chunk.payload;
  r.slot = ring_.readingSlot();
  read.status = DecodeReadStatus::Record;
  read.record = r;
  return read;
}

std::expected<void, std::string> DecodeRingReader::release() {
  if (!held_) {
    return std::unexpected("release on decode ring " + inQuotes(ring_.name()) + " without record from next()");
  }
  if (auto released = ring_.release(); !released) {
    return released;
  }
  held_ = false;
  next_sample_ = held_record_.first_sample + held_record_.num_samples;
  return {};
}

}

/*
 * SPDX-License-Identifier: MIT
 */

#include "ring_pump.h"

#include <format>
#include <limits>
#include <new>
#include <system_error>
#include <utility>

#include "chunk_view.h"
#include "display_text.h"
#include "start_messages.h"
#include "thread_name.h"

namespace bitoscope::acquisition {

namespace br = bitoscope::bulk_ring;
using bitoscope::capture::CaptureStatus;

std::expected<std::unique_ptr<RingPump>, std::string> RingPump::start(
    br::BulkRing& ring, std::shared_ptr<bitoscope::capture::CaptureSession> session,
    RingPumpConfig config) {
  if (!session) return std::unexpected(std::string("ring pump needs capture session"));
  std::unique_ptr<RingPump> pump;
  try {
    pump.reset(new RingPump(ring, std::move(session), std::move(config)));
    pump->thread_ = std::thread([p = pump.get()] { p->run(); });
  } catch (const std::system_error& e) {
    return std::unexpected(std::format("cannot start thread that reads ring '{}': {}",
                                       ring.name(), e.what()));
  } catch (const std::bad_alloc&) {
    return std::unexpected(
        std::format("out of memory starting thread that reads ring '{}'", ring.name()));
  }
  return pump;
}

RingPump::RingPump(br::BulkRing& ring, std::shared_ptr<bitoscope::capture::CaptureSession> session,
                   RingPumpConfig config)
    : ring_(ring), session_(std::move(session)), config_(std::move(config)) {}

RingPump::~RingPump() { stop(); }

void RingPump::stop() {
  stop_.store(true, std::memory_order_release);
  if (thread_.joinable()) thread_.join();
}

void RingPump::run() {
  nameCurrentThread("RingPump");
  const auto finish = [this](RingPumpEnd why) { end_.store(why, std::memory_order_release); };
  while (!stop_.load(std::memory_order_acquire)) {
    if (session_->status() != CaptureStatus::Running) {
      finish(RingPumpEnd::SessionOver);
      return;
    }
    // WHY: Heartbeat is refreshed here too: wait() does it only while the
    // ring is empty, and a busy host must not look dead.
    ring_.heartbeat();
    br::Poll poll = ring_.wait(ring_.nowMs() + config_.wait_slice_ms);
    switch (poll.status) {
      case br::PollStatus::Empty:
        break;
      case br::PollStatus::Chunk: {
        const uint64_t in_use = ring_.produced() - ring_.consumed();
        if (in_use > peak_.load(std::memory_order_relaxed)) {
          peak_.store(in_use, std::memory_order_release);
        }
        // WHY: Release a chunk only AFTER submit(): the view reads it in place
        // and the producer may overwrite it once released.
        deliver(poll.chunk);
        if (config_.before_release_for_test) config_.before_release_for_test(poll.chunk.payload);
        (void)ring_.release();
        chunks_read_.fetch_add(1, std::memory_order_acq_rel);
        break;
      }
      case br::PollStatus::Ended:
        session_->sourceEnded();
        finish(RingPumpEnd::Ended);
        return;
      case br::PollStatus::Failed: {
        const br::FailedRecord record = ring_.failedRecord();
        session_->failSource(record.at_sample, record.lost_samples,
                             bitoscope::display_text::displayText(record.reason));
        finish(RingPumpEnd::Failed);
        return;
      }
      case br::PollStatus::Invalid:
        session_->sourceInvalid(poll.error);
        finish(RingPumpEnd::Invalid);
        return;
    }
  }
  finish(RingPumpEnd::Stopped);
}

void RingPump::deliver(const br::ReadChunk& chunk) {
  const br::ChunkHeader& header = chunk.header;
  if (header.num_samples > std::numeric_limits<uint32_t>::max()) {
    session_->sourceInvalid(std::format(
        "chunk says it holds {} samples, more than one chunk can carry (at most {})",
        header.num_samples, std::numeric_limits<uint32_t>::max()));
    return;
  }
  if (header.encoding > static_cast<uint32_t>(std::numeric_limits<int32_t>::max())) {
    session_->sourceInvalid(
        std::format("chunk names sample encoding {}, which no encoding is", header.encoding));
    return;
  }
  bitoscope::host::ChunkView view;
  view.first_sample_index = header.first_sample_index;
  view.sample_rate_milli_hz = header.sample_rate_milli_hz;
  view.num_channels = header.num_channels;
  view.num_samples = static_cast<uint32_t>(header.num_samples);
  view.encoding = static_cast<bitoscope::host::SampleEncoding>(header.encoding);
  view.capture_id = header.capture_id;
  view.payload = chunk.payload;
  // WHY: Submit's error is ignored: a refusing session already failed or
  // counted the chunk, and status() stops the pump.
  (void)session_->submit(view);
}

std::expected<std::unique_ptr<AttemptRing>, std::string> AttemptRing::start(
    const CreateRingFn& create, const RingGeometry& geometry, uint64_t capture_id,
    std::shared_ptr<bitoscope::capture::CaptureSession> session, RingPumpConfig config) {
  if (!create) return std::unexpected(std::string("nothing to create bulk ring with"));
  std::unique_ptr<AttemptRing> attempt(new AttemptRing());
  auto ring = create(geometry, capture_id);
  if (!ring) return std::unexpected(ring.error());
  if (!*ring) return std::unexpected(ringNotCreatedMessage());
  attempt->ring_ = std::move(*ring);
  attempt->name_ = attempt->ring_->name();
  attempt->slot_count_ = attempt->ring_->slotCount();
  auto pump = RingPump::start(*attempt->ring_, std::move(session), std::move(config));
  if (!pump) return std::unexpected(pump.error());
  attempt->pump_ = std::move(*pump);
  return attempt;
}

uint64_t AttemptRing::slotsInUse() const {
  if (!ring_) return 0;
  // WHY: Read consumed BEFORE produced: produced only grows, so the difference cannot wrap below zero.
  const uint64_t consumed = ring_->consumed();
  const uint64_t produced = ring_->produced();
  return produced > consumed ? produced - consumed : 0;
}

void AttemptRing::end() {
  if (pump_) {
    pump_->stop();
    peak_ = pump_->peakSlotsInUse();
    chunks_read_ = pump_->chunksRead();
    pump_.reset();
  }
  ring_.reset();
}

}

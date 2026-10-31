/*
 * SPDX-License-Identifier: MIT
 */

#include "ring_summary.h"

#include <format>
#include <new>
#include <system_error>
#include <utility>

#include "display_text.h"

namespace bitoscope::cli {

namespace br = bitoscope::bulk_ring;

std::expected<std::unique_ptr<RingSummary>, std::string> RingSummary::start(br::BulkRing& ring,
                                                                             std::string driver_device_id, LineFn emit,
                                                                             RingSummaryConfig config) {
  std::unique_ptr<RingSummary> summary;
  try {
    summary.reset(new RingSummary(ring, std::move(driver_device_id), std::move(emit), std::move(config)));
    summary->thread_ = std::thread([s = summary.get()] { s->run(); });
  } catch (const std::system_error& e) {
    return std::unexpected(
        std::format("cannot start thread that reads ring '{}' for live summary: {}", ring.name(), e.what()));
  } catch (const std::bad_alloc&) {
    return std::unexpected(
        std::format("out of memory starting thread that reads ring '{}' for live summary", ring.name()));
  }
  return summary;
}

RingSummary::RingSummary(br::BulkRing& ring, std::string driver_device_id, LineFn emit, RingSummaryConfig config)
    : ring_(ring), driver_device_id_(std::move(driver_device_id)), emit_(std::move(emit)), config_(std::move(config)) {
  gaps_.expectFrom(0);
}

RingSummary::~RingSummary() { stop(); }

void RingSummary::stop() {
  stop_.store(true, std::memory_order_release);
  if (thread_.joinable()) thread_.join();
}

std::chrono::steady_clock::time_point RingSummary::now() const {
  return config_.now ? config_.now() : std::chrono::steady_clock::now();
}

RingSummaryTotals RingSummary::totals() const {
  std::lock_guard lock(mutex_);
  RingSummaryTotals t = totals_;
  t.end = end_.load(std::memory_order_acquire);
  return t;
}

std::string RingSummary::countLineLocked() {
  const std::string line = bitoscope::host::formatChunkSummary(
      driver_device_id_, bitoscope::host::ChunkSummary{.chunks = chunks_since_line_,
                                                .samples = samples_since_line_,
                                                .sample_rate_milli_hz = last_rate_milli_hz_,
                                                .last_first_sample_index = last_first_sample_,
                                                .missing_samples = missing_since_line_,
                                                .total_missing_samples = totals_.missing_samples});
  chunks_since_line_ = 0;
  samples_since_line_ = 0;
  missing_since_line_ = 0;
  return line;
}

void RingSummary::count(const br::ChunkHeader& header) {
  std::string line;
  {
    std::lock_guard lock(mutex_);
    const uint64_t missing = gaps_.observe(header.first_sample_index, header.num_samples);
    totals_.chunks += 1;
    totals_.samples += header.num_samples;
    totals_.missing_samples += missing;
    totals_.next_sample = header.first_sample_index + header.num_samples;
    chunks_since_line_ += 1;
    samples_since_line_ += header.num_samples;
    missing_since_line_ += missing;
    last_rate_milli_hz_ = header.sample_rate_milli_hz;
    last_first_sample_ = header.first_sample_index;
    const auto t = now();
    if (last_line_.has_value() &&
        !bitoscope::host::shouldEmitChunkSummary(chunks_since_line_, t - *last_line_, config_.chunk_interval,
                                                 config_.time_interval)) {
      return;
    }
    line = countLineLocked();
    last_line_ = t;
  }
  // WHY: Emit outside lock: slow console must not hold up totals() on another thread.
  if (emit_) emit_(line);
}

void RingSummary::run() {
  const auto finish = [this](RingSummaryEnd why) {
    {
      std::lock_guard lock(mutex_);
      end_.store(why, std::memory_order_release);
    }
    ended_cv_.notify_all();
  };
  while (!stop_.load(std::memory_order_acquire)) {
    // WHY: Busy reader must keep heartbeat fresh too: wait() refreshes it only while ring is empty.
    ring_.heartbeat();
    br::Poll poll = ring_.wait(ring_.nowMs() + config_.wait_slice_ms);
    switch (poll.status) {
      case br::PollStatus::Empty:
        break;
      case br::PollStatus::Chunk:
        count(poll.chunk.header);
        (void)ring_.release();
        break;
      case br::PollStatus::Ended:
        finish(RingSummaryEnd::Ended);
        return;
      case br::PollStatus::Failed: {
        const br::FailedRecord record = ring_.failedRecord();
        if (emit_) {
          emit_(std::format("[{}] la.acquisition: driver could not deliver from sample {} on ({} samples lost): {}",
                            driver_device_id_, record.at_sample, record.lost_samples,
                            bitoscope::display_text::displayText(record.reason)));
        }
        finish(RingSummaryEnd::Failed);
        return;
      }
      case br::PollStatus::Invalid:
        if (emit_) emit_(std::format("[{}] la.acquisition: bulk ring broke its protocol: {}", driver_device_id_, poll.error));
        finish(RingSummaryEnd::Invalid);
        return;
    }
  }
  finish(RingSummaryEnd::Stopped);
}

RingSummaryTotals RingSummary::finish(std::chrono::milliseconds limit) {
  {
    std::lock_guard lock(mutex_);
    if (finished_) {
      RingSummaryTotals t = totals_;
      t.end = end_.load(std::memory_order_acquire);
      return t;
    }
  }
  {
    std::unique_lock lock(mutex_);
    ended_cv_.wait_until(lock, std::chrono::steady_clock::now() + limit, [this] {
      return end_.load(std::memory_order_acquire) != RingSummaryEnd::Running;
    });
  }
  stop();

  std::string count_line;
  std::string totals_line;
  RingSummaryTotals t;
  {
    std::lock_guard lock(mutex_);
    finished_ = true;
    if (chunks_since_line_ != 0) count_line = countLineLocked();
    t = totals_;
    t.end = end_.load(std::memory_order_acquire);
    std::string how;
    switch (t.end) {
      case RingSummaryEnd::Ended:
        how = "driver ended stream";
        break;
      case RingSummaryEnd::Failed:
        how = "driver recorded failure";
        break;
      case RingSummaryEnd::Invalid:
        how = "bulk ring broke its protocol";
        break;
      case RingSummaryEnd::Stopped:
      case RingSummaryEnd::Running:
        how = std::format("stream had not ended {} ms after STOP", limit.count());
        break;
    }
    totals_line = std::format("[{}] la.acquisition: in all {} chunk(s), {} samples, next index {}", driver_device_id_,
                              t.chunks, t.samples, t.next_sample);
    if (t.missing_samples != 0) totals_line += std::format(", {} samples MISSING", t.missing_samples);
    totals_line += "; " + how;
  }
  if (emit_) {
    if (!count_line.empty()) emit_(count_line);
    emit_(totals_line);
  }
  return t;
}

}

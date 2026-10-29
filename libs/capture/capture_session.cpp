/*
 * SPDX-License-Identifier: MIT
 */

#include "capture_session.h"

#include <algorithm>
#include <chrono>
#include <format>
#include <limits>
#include <new>
#include <stdexcept>
#include <system_error>

#include "arena_growth.h"

namespace bitoscope::capture {

std::expected<std::unique_ptr<CaptureSession>, std::string> CaptureSession::create(RawCaptureBuffer&& buffer,
    SessionConfig config) {
  if (!config.now_ms) return std::unexpected("session needs clock");
  if (buffer.finalized()) return std::unexpected("capture buffer is already finalized");
  if (buffer.windowCount() != 0) return std::unexpected("capture buffer is not empty");

  const CapturePlan& plan = buffer.plan();
  std::optional<bitoscope::trigger::Evaluator> evaluator;
  const uint64_t num_buffers = windowPoolBuffers(config.pack_workers);

  if (config.trigger) {
    const bitoscope::trigger::TriggerSpec& spec = *config.trigger;
    if (auto ok = spec.validate(plan.channels()); !ok) return std::unexpected(ok.error());
    if (spec.pre_trigger_samples >
        std::numeric_limits<uint64_t>::max() - spec.post_trigger_samples) {
      return std::unexpected(std::format(
          "trigger asks for {} samples before and {} after, which overflows 64-bit "
          "total",
          spec.pre_trigger_samples, spec.post_trigger_samples));
    }
    const uint64_t range = spec.pre_trigger_samples + spec.post_trigger_samples;
    const uint64_t window = plan.samplesPerWindow();
    if (range > std::numeric_limits<uint64_t>::max() - window) {
      return std::unexpected(std::format(
          "trigger asks for {} samples, which leaves no room in 64-bit total for "
          "one-window ({} samples) front-offset margin store needs",
          range, window));
    }
    const uint64_t minimum = range + window;
    if (plan.totalSamples() < minimum) {
      return std::unexpected(std::format(
          "capture plan holds {} samples, but trigger asking for {} ({} before "
          "trigger and {} after) needs at least {}: plan must reserve one whole window of "
          "{} samples beyond pre + post as margin, because store keeps whole windows and "
          "so starts up to window before T - pre, and without room last "
          "post-trigger samples that were asked for would not fit",
          plan.totalSamples(), range, spec.pre_trigger_samples, spec.post_trigger_samples,
          minimum, window));
    }

    auto made = bitoscope::trigger::Evaluator::create(spec, plan.channels());
    if (!made) return std::unexpected(made.error());
    evaluator = std::move(*made);

    // WHY: Retain pre + one window: a trigger is reported up to a window late, so pre alone would leave history short.
    if (auto armed = buffer.armHistory(spec.pre_trigger_samples + window); !armed) {
      return std::unexpected(armed.error());
    }
  }
  if (num_buffers > std::numeric_limits<uint32_t>::max()) {
    return std::unexpected(std::format(
        "{} windows in flight to packers need {} window buffers, more than pool can hold",
        packPoolDepth(config.pack_workers), num_buffers));
  }
  if (config.window_pool_buffers_for_test != 0) {
    if (config.window_pool_buffers_for_test > num_buffers) {
      return std::unexpected(std::format(
          "window_pool_buffers_for_test is {}, more than {} windowPoolBuffers() computed "
          "for this capture: test seam may only shrink pool, not grow it past what "
          "CLI held back memory for",
          config.window_pool_buffers_for_test, num_buffers));
    }
    if (config.window_pool_buffers_for_test < num_buffers) {
      return std::unexpected(std::format(
          "window pool of {} buffers is smaller than {} this capture needs ({} windows "
          "in flight to packers, one being filled and one before it, still "
          "leased): assembler waits for free buffer rather than drop window, and "
          "smaller pool can leave it waiting for one that is never returned",
          config.window_pool_buffers_for_test, num_buffers,
          packPoolDepth(config.pack_workers)));
    }
  }

  bitoscope::host::AssemblerConfig assembler_config;
  assembler_config.samples_per_window = plan.samplesPerWindow();
  assembler_config.num_buffers = static_cast<uint32_t>(num_buffers);
  auto assembler = [&]() -> std::expected<bitoscope::host::WindowAssembler, std::string> {
    try {
      return bitoscope::host::WindowAssembler::create(assembler_config, plan.channels());
    } catch (const std::bad_alloc&) {
      return std::unexpected(std::format(
          "out of memory reserving {} window buffers of {} samples across {} channels for "
          "capture's {} samples",
          assembler_config.num_buffers, assembler_config.samples_per_window, plan.channels(),
          plan.totalSamples()));
    } catch (const std::length_error&) {
      return std::unexpected(std::format(
          "cannot reserve {} window buffers: more memory than this platform can address",
          assembler_config.num_buffers));
    }
  }();
  if (!assembler) return std::unexpected(assembler.error());

  const uint32_t pack_workers = config.pack_workers;
  PackPoolHooks hooks = config.pack_hooks_for_test;
  std::unique_ptr<CaptureSession> session(new CaptureSession(
      std::move(buffer), std::move(*assembler), std::move(config), std::move(evaluator)));
  if (session->config_.live_tail && windowOutlastsTailRefresh(session->buffer_.plan())) {
    const CapturePlan& own = session->buffer_.plan();
    if (auto reserved = session->published_.reserveTail(own.channels(), own.samplesPerWindow()); !reserved) {
      return std::unexpected(reserved.error());
    }
    session->tail_enabled_ = true;
  }
  if (pack_workers == 0 && hooks.alter_packed) {
    const CapturePlan& own = session->buffer_.plan();
    auto scratch = PackScratch::create(own.channels(), own.samplesPerWindow(), session->buffer_.hasPyramid());
    if (!scratch) return std::unexpected(scratch.error());
    session->inline_test_scratch_ = std::move(*scratch);
  }
  // WHY: Create the pool after the session sits at its final address: the pool keeps a pointer to it.
  if (pack_workers != 0) {
    PackPoolConfig pool_config;
    pool_config.workers = pack_workers;
    pool_config.depth = session->config_.pack_depth_for_test;
    pool_config.hooks = std::move(hooks);
    CaptureSession* self = session.get();
    auto pool = PackPool::create(
        session->buffer_, std::move(pool_config),
        [self](const bitoscope::host::WindowInfo& info,
               const std::expected<PackedWindow, std::string>& packed) {
          self->commitPacked(info, packed);
        });
    if (!pool) return std::unexpected(pool.error());
    session->pack_pool_ = std::move(*pool);
  }
  return session;
}

CaptureSession::~CaptureSession() {
  history_stop_.store(true, std::memory_order_relaxed);
  joinHistoryPass();
  if (config_.block_supply) config_.block_supply->abandon();
  pack_pool_.reset();
}

std::expected<void, std::string> CaptureSession::submit(const bitoscope::host::ChunkView& chunk) {
  std::lock_guard lock(mutex_);
  if (finished_) return std::unexpected("capture session is finished");
  if (config_.capture_id != 0 && chunk.capture_id != config_.capture_id) {
    if (chunk.capture_id == 0) {
      if (failure_ || reached_) return {};
      return fail(FailureKind::PluginIgnoresId, 0, 0,
                  std::format("driver '{}' sent chunk without capture id ({:#x}) that START "
                              "carried, so it does not echo it and its chunks cannot be told from "
                              "earlier capture's",
                              config_.plugin_id, config_.capture_id));
    }
    ++foreign_chunks_;
    return {};
  }
  ++chunks_received_;
  if (auto store_error = takeCommitErrorLocked()) return std::unexpected(std::move(*store_error));
  if (failure_ || reached_) return {};
  if (keep_stop_requested_.load(std::memory_order_acquire)) return {};
  if (session_stop_requested_.load(std::memory_order_acquire)) return {};

  const CapturePlan& plan = buffer_.plan();
  if (chunk.sample_rate_milli_hz != plan.sampleRateMilliHz()) {
    return reject(chunk, std::format("chunk sample rate {} mHz differs from plan's {} mHz",
                                     chunk.sample_rate_milli_hz, plan.sampleRateMilliHz()));
  }

  if (chunk.first_sample_index > next_expected_) {
    const uint64_t first_lost = next_expected_;
    const uint64_t lost = chunk.first_sample_index - next_expected_;
    if (seen_chunk_) {
      return fail(FailureKind::Gap, first_lost, lost,
                  std::format("gap: samples {} to {} ({} samples) never arrived; next chunk "
                              "begins at sample {}",
                              first_lost, first_lost + lost - 1, lost,
                              chunk.first_sample_index));
    }
    return fail(FailureKind::StartUpLoss, 0, lost,
                std::format("start-up loss: first chunk begins at sample {}, so samples 0 "
                            "to {} ({} samples) were lost",
                            chunk.first_sample_index, lost - 1, lost));
  }

  window_failure_.clear();
  auto accepted = assembler_.submit(chunk, on_window_);
  if (!accepted) {
    if (const auto ended = assembler_.waitEnded()) {
      if (*ended == bitoscope::host::WaitEnd::TimedOut && !failure_) {
        return recordStall(next_expected_, accepted.error());
      }
      return std::unexpected(failure_ ? failure_->message : accepted.error());
    }
    return reject(chunk, accepted.error());
  }

  seen_chunk_ = true;
  last_activity_ms_ = config_.now_ms();
  const uint64_t end = chunk.first_sample_index + chunk.num_samples;
  next_expected_ = end;
  if (end > covered_) covered_ = end;
  // WHY: While armed never set reached_ from the raw stream's length: it would switch off every hole check.
  // End is T + post, not origin + plan total: the plan carries a window of margin.
  const bool reached_end = triggered()
                               ? (absolute_end_.has_value() && covered_ >= *absolute_end_)
                               : covered_ >= plan.totalSamples();
  if (reached_end) {
    reached_ = true;
    assembler_.flush(on_window_);
  }
  if (!window_failure_.empty()) {
    return std::unexpected(std::exchange(window_failure_, std::string{}));
  }
  if (auto store_error = takeCommitErrorLocked()) return std::unexpected(std::move(*store_error));
  refreshTailLocked();
  return {};
}

void CaptureSession::failSource(uint64_t at_sample, uint64_t lost_samples,
    std::string_view reason) {
  std::lock_guard lock(mutex_);
  adoptCommitFailureLocked();
  if (finished_ || failure_ || reached_) return;
  const std::string lost =
      lost_samples == 0
          ? std::format("delivered nothing from sample {} on", at_sample)
          : std::format("lost samples {} to {} ({} samples) and delivered nothing after them",
                        at_sample, at_sample + lost_samples - 1, lost_samples);
  (void)fail(FailureKind::Dropped, at_sample, lost_samples,
             std::format("driver '{}' {}: {}", config_.plugin_id, lost, reason));
}

void CaptureSession::sourceEnded() {
  std::lock_guard lock(mutex_);
  adoptCommitFailureLocked();
  if (finished_ || failure_ || reached_) return;
  if (observeLocked() != CaptureStatus::Running) return;
  const uint64_t got = next_expected_;
  const std::string arrived = std::format("{} samples in {} chunk(s)", got, chunks_received_);
  if (triggered() && !absolute_end_) {
    (void)fail(FailureKind::EndedEarly, got, 0,
               std::format("stream ended early: driver '{}' ended it after {}, while "
                           "trigger was still waiting to fire",
                           config_.plugin_id, arrived));
    return;
  }
  const uint64_t end = triggered() ? *absolute_end_ : buffer_.plan().totalSamples();
  const uint64_t lost = end > got ? end - got : 0;
  (void)fail(FailureKind::EndedEarly, got, lost,
             std::format("stream ended early: driver '{}' ended it after {}, but capture "
                         "runs to sample {}; samples {} to {} ({} samples) never arrived",
                         config_.plugin_id, arrived, end - 1, got, end - 1, lost));
}

void CaptureSession::sourceInvalid(std::string_view reason) {
  std::lock_guard lock(mutex_);
  adoptCommitFailureLocked();
  if (finished_ || failure_ || reached_) return;
  (void)fail(FailureKind::Rejected, next_expected_, 0,
             std::format("bulk ring from driver '{}' is not valid: {}", config_.plugin_id,
                         reason));
}

// WHY: finish() caches its result: setting finished_ before checking the append error once turned a failed
// capture into a reported success on the second call.
std::expected<CaptureSummary, std::string> CaptureSession::finish() {
  std::lock_guard lock(mutex_);
  if (finished_) return finish_result_;
  {
    const bool armed_unfired = triggered() && !trigger_fired_;
    if (!reached_ && !armed_unfired) assembler_.flush(on_window_);
    if (pack_pool_) {
      if (auto drained = pack_pool_->drain(wait_limit_); !drained) {
        if (drained.error() == bitoscope::host::WaitEnd::TimedOut) {
          (void)recordStall(next_expected_,
                            std::format("packers did not commit windows in flight within "
                                        "{} ms of end of capture",
                                        config_.inactivity_timeout_ms));
        }
        pack_pool_->abandon();
      }
    }
    joinHistoryPass();
    {
      auto commits = commitLock();
      publishSnapshotLocked();
      auto done = buffer_.finalize();
      if (!done) return std::unexpected(done.error());
      if (config_.block_supply) config_.block_supply->close();
      arena_closed_.store(true, std::memory_order_release);
    }
    end_ = statusLocked();
    if (end_ == CaptureStatus::Running) end_ = CaptureStatus::Interrupted;
    if (!latched_) {
      timed_out_before_first_chunk_ = end_ == CaptureStatus::TimedOut && !seen_chunk_;
    }
    finished_ = true;
    if (pack_pool_) pack_pool_->release();
    assembler_.releaseWindows();
  }
  auto commits = commitLock();
  if (commit_failed_ && commit_failure_kind_ == FailureKind::StoreError) {
    finish_result_ = std::unexpected(commitErrorText());
    return finish_result_;
  }
  CaptureSummary summary;
  summary.end = end_;
  summary.requested_samples =
      config_.trigger ? config_.trigger->pre_trigger_samples +
                            config_.trigger->post_trigger_samples
                      : buffer_.plan().totalSamples();
  summary.stored_samples = captureExtentLocked().count;
  summary.not_stored_samples = buffer_.overflowSamples() + skipped_samples_;
  summary.shortfall_samples = summary.requested_samples > summary.stored_samples
                                  ? summary.requested_samples - summary.stored_samples
                                  : 0;
  summary.capture_id = config_.capture_id;
  summary.foreign_chunks = foreign_chunks_;
  finish_result_ = summary;
  return finish_result_;
}

uint64_t CaptureSession::memoryStillNeeded(uint64_t free_now) const {
  if (!config_.arena_memory || arena_closed_.load(std::memory_order_acquire)) return 0;
  return config_.arena_memory->stillNeededBytes(free_now);
}

CaptureProgress CaptureSession::progress() const {
  std::lock_guard lock(mutex_);
  CaptureProgress p;
  p.requested_samples = config_.trigger ? config_.trigger->pre_trigger_samples +
                                              config_.trigger->post_trigger_samples
                                        : buffer_.plan().totalSamples();
  {
    auto commits = commitLock();
    p.stored_samples = captureExtentLocked().count;
    p.memory_used_bytes = buffer_.arenaUsedBytes();
    p.memory_bytes = buffer_.arenaBytes();
  }
  p.armed = triggered() && !trigger_fired_;
  if (p.armed) {
    const uint64_t now = config_.now_ms();
    p.armed_ms = now > armed_at_ms_ ? now - armed_at_ms_ : 0;
    p.history_complete = covered_ >= config_.trigger->pre_trigger_samples;
  }
  return p;
}

CaptureSession::CaptureSession(RawCaptureBuffer&& buffer,
    bitoscope::host::WindowAssembler&& assembler, SessionConfig config,
    std::optional<bitoscope::trigger::Evaluator> evaluator) : buffer_(std::move(buffer)), assembler_(std::move(assembler)), config_(std::move(config)), evaluator_(std::move(evaluator)), last_activity_ms_(config_.now_ms()), armed_at_ms_(last_activity_ms_) {
  wait_limit_.abandon = &abandon_waits_;
  wait_limit_.now_ms = config_.now_ms;
  wait_limit_.timeout_ms = config_.inactivity_timeout_ms;
  wait_limit_.poll = std::chrono::milliseconds(config_.buffer_wait_poll_ms);
  assembler_.setWaitLimit(wait_limit_);
  on_window_ = [this](bitoscope::host::WindowLease lease) {
    if (triggered()) {
      onTriggeredWindow(std::move(lease));
    } else {
      clipAndAppend(lease, buffer_.plan().totalSamples());
    }
  };
}

void CaptureSession::clipAndAppend(bitoscope::host::WindowLease& lease, uint64_t end) {
  bitoscope::host::WindowInfo& info = lease.info();
  if (info.first_sample_index >= end) return;
  const uint64_t room = end - info.first_sample_index;
  if (info.num_samples > room) info.num_samples = static_cast<uint32_t>(room);
  store(lease);
}

void CaptureSession::store(bitoscope::host::WindowLease& lease) {
  const bitoscope::host::WindowInfo& info = lease.info();
  if (pack_pool_) {
    if (abandon_waits_.load(std::memory_order_acquire)) return;
    const uint64_t first = info.first_sample_index + store_origin_.value_or(0);
    auto dispatched = pack_pool_->dispatch(std::move(lease), wait_limit_);
    if (!dispatched && dispatched.error() == bitoscope::host::WaitEnd::TimedOut) {
      (void)recordStall(first, std::format("no pack slot was freed within {} ms: window at "
                                           "sample {} could not be handed to packers",
                                           config_.inactivity_timeout_ms, first));
    }
    return;
  }

  const uint64_t seq = inline_seq_++;

  // WHY: After a refusal append nothing: a later, smaller window could fit behind it and leave a gap mid-capture.
  if (buffer_.overflowSamples() != 0) {
    skipped_samples_ += info.num_samples;
    return;
  }

  if (commit_failed_) return;
  recordOutcome(info, appendInline(lease, seq));
  publishSnapshotLocked();
  if (commit_failed_) adoptCommitFailureLocked();
}

std::expected<AppendOutcome, std::string> CaptureSession::appendInline(bitoscope::host::WindowLease& lease,
    uint64_t seq) {
  const bool armed = buffer_.storeMode() == StoreMode::Armed;
  if (!inline_test_scratch_) {
    if (armed) return buffer_.appendWindowArmed(lease);
    return config_.block_supply ? buffer_.appendWindow(lease, *config_.block_supply) : buffer_.appendWindow(lease);
  }
  auto packed = buffer_.packWindow(lease, *inline_test_scratch_);
  config_.pack_hooks_for_test.alter_packed(seq, packed);
  if (!packed) return std::unexpected(packed.error());
  return armed ? buffer_.appendArmed(*packed) : buffer_.appendPacked(*packed, config_.block_supply.get());
}

void CaptureSession::commitPacked(const bitoscope::host::WindowInfo& info,
    const std::expected<PackedWindow, std::string>& packed) {
  if (buffer_.overflowSamples() != 0) {
    skipped_samples_ += info.num_samples;
    return;
  }
  if (commit_failed_) return;
  if (!packed) {
    recordCommitFailure(FailureKind::StoreError, info, packed.error());
    return;
  }
  try {
    recordOutcome(info, buffer_.storeMode() == StoreMode::Armed
                            ? buffer_.appendArmed(*packed)
                            : buffer_.appendPacked(*packed, config_.block_supply.get()));
  } catch (...) {
    recordCommitFailure(FailureKind::StoreError, info, {});
  }
  publishSnapshotLocked();
}

void CaptureSession::recordOutcome(const bitoscope::host::WindowInfo& info,
    const std::expected<AppendOutcome, std::string>& stored) {
  if (!stored) {
    recordCommitFailure(FailureKind::StoreError, info, stored.error());
  } else if (*stored == AppendOutcome::NoRoomWhileArmed) {
    recordCommitFailure(FailureKind::TriggerShortfall, info, {});
  }
}

void CaptureSession::recordCommitFailure(FailureKind kind, const bitoscope::host::WindowInfo& info,
    std::string_view why) {
  commit_failed_ = true;
  commit_failure_kind_ = kind;
  const uint64_t first = info.first_sample_index + commit_origin_;
  try {
    commit_failure_ = CaptureFailure{kind, first, info.num_samples,
                                     commitFailureText(kind, first, info.num_samples, why)};
    commit_error_ = commit_failure_->message;
  } catch (...) {
    if (!commit_failure_) commit_failure_.emplace();
    commit_failure_->kind = kind;
    commit_failure_->sample_index = first;
    commit_failure_->lost_samples = info.num_samples;
  }
}

std::string CaptureSession::commitFailureText(FailureKind kind, uint64_t sample, uint64_t samples,
    std::string_view why) const {
  if (kind == FailureKind::TriggerShortfall) {
    return std::format(
        "memory for capture cannot hold pre-trigger history: window at sample "
        "{} ({} samples) found no room in it while trigger was awaited, so {} samples "
        "before trigger cannot be kept whatever data; nothing is saved - use shorter "
        "pre-trigger, lower rate, fewer channels, or larger memory budget",
        sample, samples, config_.trigger ? config_.trigger->pre_trigger_samples : 0);
  }
  return std::format(
      "window at sample {} ({} samples) could not be stored, so nothing from it on is: {}",
      sample, samples, why.empty() ? std::string_view("no memory was left to say why") : why);
}

void CaptureSession::adoptCommitFailureLocked() const {
  auto commits = commitLock();
  if (!commit_failure_) return;
  if (!failure_) {
    failure_ = std::move(*commit_failure_);
    if (failure_->message.empty()) {
      failure_->message =
          failure_->kind == FailureKind::TriggerShortfall
              ? std::format("memory for capture cannot hold pre-trigger history: "
                            "window at sample {} ({} samples) found no room in it",
                            failure_->sample_index, failure_->lost_samples)
              : std::format("window at sample {} ({} samples) could not be stored, so "
                            "nothing from it on is",
                            failure_->sample_index, failure_->lost_samples);
    }
  }
  commit_failure_.reset();
}

std::optional<std::string> CaptureSession::takeCommitErrorLocked() {
  adoptCommitFailureLocked();
  auto commits = commitLock();
  if (!commit_failed_ || commit_error_reported_) return std::nullopt;
  commit_error_reported_ = true;
  return commitErrorText();
}

void CaptureSession::onTriggeredWindow(bitoscope::host::WindowLease lease) {
  if (failure_ || abandon_waits_.load(std::memory_order_acquire)) return;

  if (store_origin_) {
    lease.info().first_sample_index -= *store_origin_;
    clipAndAppend(lease, store_end_);
    return;
  }

  // WHY: Scan the raw window BEFORE storing it: packing zeroes pad bits in the lease's buffer, and with packers
  // the lease is handed on and can no longer be read here.
  const std::optional<uint64_t> hit = evaluator_->scan(lease);

  store(lease);
  if (!hit) return;
  trigger_fired_ = true;
  settleOnHit(*hit);
}

void CaptureSession::settleOnHit(uint64_t trigger) {
  if (pack_pool_) {
    if (auto drained = pack_pool_->drain(wait_limit_); !drained) {
      if (drained.error() == bitoscope::host::WaitEnd::TimedOut) {
        (void)recordStall(trigger, std::format("packers did not commit pre-trigger "
                                               "history within {} ms of trigger at sample {}",
                                               config_.inactivity_timeout_ms, trigger));
      }
      return;
    }
  }
  adoptCommitFailureLocked();
  if (failure_) return;

  const uint64_t window = buffer_.plan().samplesPerWindow();
  const uint64_t pre = config_.trigger->pre_trigger_samples;
  const uint64_t post = config_.trigger->post_trigger_samples;

  LiveHistory live;
  {
    auto commits = commitLock();
    live = buffer_.liveHistory();
  }
  if (trigger < pre) {
    failInWindow(FailureKind::TriggerShortfall, trigger, pre - trigger,
                 std::format("trigger at sample {} asked for {} samples before it, but "
                             "stream began only {} samples before it",
                             trigger, pre, trigger));
    return;
  }
  const uint64_t begin = trigger - pre;
  if (live.windows == 0 || live.first_sample > begin) {
    const uint64_t held_from = live.windows == 0 ? trigger : live.first_sample;
    failInWindow(FailureKind::TriggerShortfall, begin, held_from - begin,
                 std::format("trigger at sample {} asked for {} samples back to sample "
                             "{} before it, but pre-trigger history holds only from sample "
                             "{}: {} samples of it are missing",
                             trigger, pre, begin, held_from, held_from - begin));
    return;
  }
  if (live.end_sample <= trigger) {
    failInWindow(FailureKind::TriggerShortfall, live.end_sample, trigger + 1 - live.end_sample,
                 std::format("trigger at sample {} is not in pre-trigger history, which "
                             "ends at sample {}",
                             trigger, live.end_sample));
    return;
  }

  std::expected<uint64_t, std::string> settled;
  {
    auto commits = commitLock();
    settled = buffer_.settleHistory(begin);
    if (settled) commit_origin_ = *settled;
  }
  if (!settled) {
    failInWindow(FailureKind::TriggerShortfall, begin, 0,
                 std::format("pre-trigger history for trigger at sample {} could not be "
                             "settled: {}",
                             trigger, settled.error()));
    return;
  }
  const uint64_t origin = *settled;

  if (origin % window != 0) {
    failInWindow(FailureKind::TriggerShortfall, origin, 0,
                 std::format("pre-trigger window begins at sample {}, which is not "
                             "whole window of {} samples from start of capture",
                             origin, window));
    return;
  }

  if (begin < origin || begin - origin >= window) {
    failInWindow(FailureKind::TriggerShortfall, origin, 0,
                 std::format("pre-trigger history kept for trigger at sample {} begins "
                             "at sample {}, which is not whole window of {} samples "
                             "holding sample {} (T - pre)",
                             trigger, origin, window, begin));
    return;
  }
  const uint64_t d = begin - origin;

  const uint64_t store_end = d + pre + post;
  const uint64_t plan_total = buffer_.plan().totalSamples();
  if (store_end > plan_total) {
    const uint64_t missing = store_end - plan_total;
    failInWindow(FailureKind::TriggerShortfall, origin + plan_total, missing,
                 std::format("capture would end at sample {}, but {} samples asked "
                             "for after trigger at sample {} run to sample {}: last "
                             "{} post-trigger samples would be missing",
                             origin + plan_total, post, trigger, trigger + post, missing));
    return;
  }

  {
    auto commits = commitLock();
    store_origin_ = origin;
    front_offset_ = d;
    store_end_ = store_end;
    absolute_end_ = trigger + post;
    trigger_origin_ = begin;
    trigger_sample_index_ = trigger;
    publishSnapshotLocked();
  }

  if (buffer_.hasPyramid()) {
    try {
      history_pass_ = std::thread([this] { (void)buffer_.summarizeHistory(history_stop_); });
    } catch (const std::system_error& e) {
      (void)buffer_.failHistorySummary(std::string("its thread could not start: ") + e.what());
    }
  }
}

void CaptureSession::refreshTailLocked() {
  if (!tail_enabled_) return;
  if (tail_refreshed_ms_ && last_activity_ms_ - *tail_refreshed_ms_ < kTailRefreshMs) return;
  if (triggered() && !store_origin_) return;
  auto commits = commitLock();
  const CaptureExtent extent = captureExtentLocked();
  const uint64_t extent_end = extent.first_sample + extent.count;
  const uint64_t filling_first = assembler_.fillingFirstSample() - store_origin_.value_or(0);
  if (filling_first != extent_end) return;
  if (published_.publishTail(extent, trigger_origin_, trigger_sample_index_, assembler_.fillingWindow(),
                             assembler_.filledSamples())) {
    tail_refreshed_ms_ = last_activity_ms_;
  }
}

CaptureExtent CaptureSession::captureExtentLocked() const {
  const uint64_t stored = buffer_.storedSamples();
  if (!triggered()) return CaptureExtent{0, stored};
  if (!store_origin_) return CaptureExtent{0, 0};
  const uint64_t held = stored > front_offset_ ? stored - front_offset_ : 0;
  const uint64_t requested =
      config_.trigger->pre_trigger_samples + config_.trigger->post_trigger_samples;
  return CaptureExtent{front_offset_, (std::min)(held, requested)};
}

void CaptureSession::failInWindow(FailureKind kind, uint64_t sample_index, uint64_t lost,
    std::string message) {
  if (!failure_) failure_ = CaptureFailure{kind, sample_index, lost, message};
  if (window_failure_.empty()) window_failure_ = std::move(message);
}

void CaptureSession::abandonWaits() {
  abandon_waits_.store(true, std::memory_order_release);
  history_stop_.store(true, std::memory_order_relaxed);
  assembler_.wakeWaiters();
  if (pack_pool_) pack_pool_->wakeWaiters();
  if (config_.block_supply) config_.block_supply->abandon();
}

std::unexpected<std::string> CaptureSession::recordStall(uint64_t sample,
    const std::string& what) {
  abandonWaits();
  failInWindow(FailureKind::Stalled, sample, 0,
               "internal stall: " + what + "; nothing more can be stored");
  return std::unexpected(failure_->message);
}

std::unexpected<std::string> CaptureSession::fail(FailureKind kind, uint64_t sample_index,
    uint64_t lost, std::string message) {
  if (!failure_) failure_ = CaptureFailure{kind, sample_index, lost, message};
  return std::unexpected(std::move(message));
}

std::unexpected<std::string> CaptureSession::reject(const bitoscope::host::ChunkView& chunk,
    const std::string& why) {
  return fail(FailureKind::Rejected, chunk.first_sample_index, 0,
              std::format("chunk at sample {} rejected: {}", chunk.first_sample_index, why));
}

// WHY: Precedence is load-bearing: stop is tested before overflow and length; overflow before reached_, else a
// capture that stored a fraction of the stream would report Complete.
CaptureStatus CaptureSession::statusLocked() const {
  if (finished_) return end_;
  adoptCommitFailureLocked();
  if (failure_) return CaptureStatus::Failed;
  if (stop_requested_) return CaptureStatus::Interrupted;
  if (keep_stop_requested_.load(std::memory_order_acquire)) return CaptureStatus::Stopped;
  if (latched_) return *latched_;
  if (triggerTimedOutLocked()) return CaptureStatus::Interrupted;
  {
    auto commits = commitLock();
    if (buffer_.overflowSamples() != 0) return CaptureStatus::Full;
    if (reached_) {
      if (!allCommitted()) return CaptureStatus::Running;
      return session_stop_requested_.load(std::memory_order_acquire) ? CaptureStatus::Stopped
                                                                      : CaptureStatus::Complete;
    }
  }
  if (config_.now_ms() - last_activity_ms_ >= config_.inactivity_timeout_ms) {
    return CaptureStatus::TimedOut;
  }
  if (session_stop_requested_.load(std::memory_order_acquire)) return CaptureStatus::Stopped;
  return CaptureStatus::Running;
}

CaptureStatus CaptureSession::observeLocked() const {
  const CaptureStatus now = statusLocked();
  if (!latched_ && now != CaptureStatus::Running && now != CaptureStatus::Failed &&
      !finished_) {
    latched_ = now;
    latched_timeout_ = now == CaptureStatus::Interrupted && !stop_requested_ &&
                       triggerTimedOutLocked();
    timed_out_before_first_chunk_ = now == CaptureStatus::TimedOut && !seen_chunk_;
  }
  return now;
}

bool CaptureSession::triggerTimedOutLocked() const {
  if (!config_.trigger || trigger_fired_) return false;
  const uint64_t timeout_s = config_.trigger->timeout_s;
  if (timeout_s == 0) return false;
  if (timeout_s > std::numeric_limits<uint64_t>::max() / 1000) return false;
  return config_.now_ms() - armed_at_ms_ >= timeout_s * 1000;
}

}

/*
 * SPDX-License-Identifier: MIT
 */

#include "acquisition.h"

#include <algorithm>
#include <bit>
#include <chrono>
#include <cstdio>
#include <format>
#include <functional>
#include <string_view>
#include <utility>

#include "arena_growth.h"
#include "block_supply_ahead.h"
#include "capture_planning.h"
#include "heap_trim.h"
#include "la_pipe_client.h"
#include "raw_capture_buffer.h"
#include "ring_pump.h"
#include "start_messages.h"

namespace bitoscope::acquisition {

using bitoscope::capture::CaptureStatus;

std::string formatCaptureId(uint64_t id) {
  char text[32];
  std::snprintf(text, sizeof text, "0x%016llx", static_cast<unsigned long long>(id));
  return text;
}

namespace detail {

std::string describeEnd(CaptureStatus end, uint64_t inactivity_timeout_ms, std::optional<uint64_t> trigger_timeout_s) {
  switch (end) {
    case CaptureStatus::TimedOut:
      return "no data for " + std::to_string(inactivity_timeout_ms) + " ms";
    case CaptureStatus::Interrupted:
      if (trigger_timeout_s.has_value()) {
        return "trigger did not fire within " + std::to_string(*trigger_timeout_s) + " s (--trigger-timeout)";
      }
      return "interrupted";
    case CaptureStatus::Full:
      return "memory reserved for capture is full";
    case CaptureStatus::Stopped:
      return "stopped by user";
    case CaptureStatus::Complete:
    case CaptureStatus::Failed:
    case CaptureStatus::Running:
      break;
  }
  return "capture ended early";
}

std::string describeEarlyEnd(const bitoscope::capture::CaptureSummary& summary,
                             const bitoscope::capture::CapturePlan& plan, uint64_t inactivity_timeout_ms,
                             std::optional<uint64_t> trigger_timeout_s) {
  namespace pd = bitoscope::capture::plan_detail;
  return "captured " + pd::withThousands(summary.stored_samples) + " of " +
         pd::withThousands(summary.requested_samples) + " samples (" +
         pd::formatDuration(summary.stored_samples, plan.sampleRateMilliHz()) + " of " +
         pd::formatDuration(summary.requested_samples, plan.sampleRateMilliHz()) + "), then " +
         describeEnd(summary.end, inactivity_timeout_ms, trigger_timeout_s) + "; nothing was saved";
}

std::optional<std::string> describeUntriggeredMemory(uint64_t samples_floor,
                                                     const bitoscope::capture::CapturePlan& plan,
                                                     bool taken_as_it_grows) {
  namespace pd = bitoscope::capture::plan_detail;
  if (samples_floor >= plan.totalSamples()) return std::nullopt;
  return std::format(
      "possibly not all data will be acquired: at least {} samples ({}) fit even if nothing packs{}; more "
      "will fit as data compresses ({} samples, {} requested)",
      pd::withThousands(samples_floor), pd::formatDuration(samples_floor, plan.sampleRateMilliHz()),
      taken_as_it_grows ? ", unless memory is taken meanwhile" : "", pd::withThousands(plan.totalSamples()),
      pd::formatDuration(plan.totalSamples(), plan.sampleRateMilliHz()));
}

namespace {

std::string countOf(uint64_t n, std::string_view what) {
  return bitoscope::capture::plan_detail::withThousands(n) + " " + std::string(what) + (n == 1 ? " sample" : " samples");
}

}

std::expected<std::optional<std::string>, std::string> describeTriggeredMemory(
    uint64_t samples_floor, const bitoscope::trigger::TriggerSpec& spec, const bitoscope::capture::CapturePlan& plan) {
  namespace pd = bitoscope::capture::plan_detail;
  using bitoscope::capture::TriggeredMemoryAnswer;
  const uint64_t pre = spec.pre_trigger_samples;
  const uint64_t post = spec.post_trigger_samples;
  const uint64_t rate = plan.sampleRateMilliHz();
  const bitoscope::capture::TriggeredMemoryFit fit =
      bitoscope::capture::triggeredMemoryFit(samples_floor, pre, post, plan.samplesPerWindow());
  switch (fit.answer) {
    case TriggeredMemoryAnswer::kRun:
      return std::optional<std::string>{};
    case TriggeredMemoryAnswer::kRefuse:
      return std::unexpected(std::format(
          "memory for capture cannot hold pre-trigger history and trigger sample "
          "even if nothing packs: while trigger is awaited history is kept as whole raw "
          "windows, and with {} ({}) it can need up to {} per channel ({}), depending on where "
          "trigger falls in its {}-sample window, and {} ({}) fit; use shorter pre-trigger "
          "(--pre-samples, --pre-time or --capture-ratio), lower rate, fewer channels, or "
          "larger --memory-budget",
          countOf(pre, "pre-trigger"), pd::formatDuration(pre, rate),
          pd::withThousands(fit.needed_samples) + " samples", pd::formatDuration(fit.needed_samples, rate),
          pd::withThousands(plan.samplesPerWindow()), pd::withThousands(fit.usable_samples) + " samples",
          pd::formatDuration(fit.usable_samples, rate)));
    case TriggeredMemoryAnswer::kWarn:
      break;
  }
  const std::string floor =
      countOf(fit.post_floor, "post-trigger") + " (" + pd::formatDuration(fit.post_floor, rate) + ")";
  const std::string held = pre == 0 ? "at least " + floor
                                    : "all " + countOf(pre, "pre-trigger") + " (" + pd::formatDuration(pre, rate) +
                                          ") and at least " + floor;
  return std::optional<std::string>{std::format(
      "possibly not all data will be acquired: {} fit even if nothing packs; more will fit as "
      "data compresses ({}, {} requested)",
      held, countOf(post, "post-trigger"), pd::formatDuration(post, rate))};
}

std::expected<bitoscope::capture::MemoryShortfall, std::string> describeShortfall(
    const bitoscope::capture::CaptureSummary& summary, const bitoscope::capture::CaptureSession& capture,
    const std::optional<bitoscope::trigger::TriggerSpec>& trigger) {
  bitoscope::capture::MemoryShortfall shortfall;
  shortfall.reason = summary.end == CaptureStatus::Stopped ? bitoscope::capture::ShortReason::StoppedByUser
                                                           : bitoscope::capture::ShortReason::Memory;
  shortfall.requested_samples = summary.requested_samples;
  shortfall.stored_samples = summary.stored_samples;
  if (trigger.has_value()) {
    const uint64_t pre = trigger->pre_trigger_samples;
    const uint64_t count = capture.captureExtent().count;
    if (count < pre) {
      return std::unexpected(std::format(
          "kept capture does not hold its whole pre-trigger range: it holds {} samples, "
          "less than {} samples asked for before trigger",
          count, pre));
    }
    shortfall.requested_post_trigger_samples = trigger->post_trigger_samples;
    shortfall.stored_post_trigger_samples = count - pre;
  }
  return shortfall;
}

namespace {

std::string describeTally(const bitoscope::capture::CaptureSession& capture, uint64_t capture_id) {
  return " (capture id " + formatCaptureId(capture_id) + "; " + std::to_string(capture.chunksReceived()) +
         " chunk(s) of this capture arrived, " + std::to_string(capture.foreignChunks()) +
         " of another capture ignored)";
}

// WHY: Retry decision reads these flags, never the message text; only start-up loss is retried.
struct AttemptError {
  std::optional<bitoscope::capture::FailureKind> kind;
  uint64_t lost_samples = 0;
  std::string message;
  bool no_data = false;
  bool source_before_first_chunk = false;
  std::string source_message{};
  bool device_lost = false;

  bool isStartUpLoss() const { return kind == bitoscope::capture::FailureKind::StartUpLoss; }
  bool isRetryable() const { return !device_lost && (isStartUpLoss() || no_data || source_before_first_chunk); }
};

std::string describeLostStart(const AttemptError& error, uint64_t inactivity_timeout_ms) {
  if (error.source_before_first_chunk) {
    const bool ended = error.kind == bitoscope::capture::FailureKind::EndedEarly;
    return std::string(ended ? "driver ended stream before its first chunk - "
                             : "driver failed capture before its first chunk - ") +
           error.source_message;
  }
  if (error.no_data) {
    return "no data arrived within " + std::to_string(inactivity_timeout_ms) + " ms of start";
  }
  return std::to_string(error.lost_samples) + " samples missing at start";
}

Progress progressOf(const bitoscope::capture::CaptureSession& capture, const AttemptRing& ring,
                    const std::optional<bitoscope::trigger::TriggerSpec>& trigger) {
  const bitoscope::capture::CaptureProgress progress = capture.progress();
  Progress figures;
  figures.stored_samples = progress.stored_samples;
  figures.requested_samples = progress.requested_samples;
  figures.memory_used_bytes = progress.memory_used_bytes;
  figures.memory_bytes = progress.memory_bytes;
  figures.ring_slots_in_use = ring.slotsInUse();
  figures.ring_slots = ring.slotCount();
  figures.triggered = trigger.has_value();
  if (progress.armed) {
    figures.armed = Progress::Armed{progress.armed_ms, progress.history_complete,
                                    trigger.has_value() ? trigger->timeout_s : 0};
  }
  return figures;
}

// WHY: mulDivFloor is hand-written because MSVC has no __int128.
uint64_t mulDivFloor(uint64_t a, uint64_t b, uint64_t d) {
  const uint64_t a_lo = a & 0xFFFF'FFFFu, a_hi = a >> 32, b_lo = b & 0xFFFF'FFFFu, b_hi = b >> 32;
  const uint64_t lo_lo = a_lo * b_lo, hi_lo = a_hi * b_lo, lo_hi = a_lo * b_hi, hi_hi = a_hi * b_hi;
  const uint64_t cross = (lo_lo >> 32) + (hi_lo & 0xFFFF'FFFFu) + lo_hi;
  uint64_t hi = hi_hi + (hi_lo >> 32) + (cross >> 32);
  uint64_t lo = (cross << 32) | (lo_lo & 0xFFFF'FFFFu);
  uint64_t quotient = 0;
  uint64_t rem = 0;
  for (int bit = 127; bit >= 0; --bit) {
    const uint64_t next = bit >= 64 ? (hi >> (bit - 64)) & 1 : (lo >> bit) & 1;
    const bool carry = (rem >> 63) != 0;
    rem = (rem << 1) | next;
    if (carry || rem >= d) {
      rem -= d;
      if (bit < 64) quotient |= uint64_t{1} << bit;
    }
  }
  return quotient;
}

struct DeviceRun {
  const SessionDevice* dev = nullptr;
  uint32_t index = 0;
  std::optional<bitoscope::capture::CapturePlan> plan;
  std::optional<bitoscope::host::pipe::LaAcquisitionClient> la;
  RingGeometry ring_geometry;
  bitoscope::host::pipe::CaptureRequest request;
  HeldBack held_back;
  std::optional<bitoscope::capture::Reservation> reservation;
  std::optional<bitoscope::capture::ArenaGrowth> growth;
  std::vector<AcquireWarning> warnings;
  std::shared_ptr<bitoscope::capture::CaptureSession> capture;
  std::unique_ptr<AttemptRing> ring;
  bool running = false;
  bool progress_given = false;
  uint64_t last_progress_ms = 0;
  std::optional<uint32_t> stopped_for;
  std::string stopped_for_id;
  std::optional<AcquireResult> result;

  const std::string& id() const { return dev->device.plugin_id; }
  const std::optional<bitoscope::trigger::TriggerSpec>& trigger() const { return dev->request.trigger; }
};

struct SessionContext {
  const AcquisitionSeams& seams;
  AcquisitionEvents& events;
  bool keep_on_memory_full = true;
};

void attach(const AcquisitionSeams& seams, std::shared_ptr<bitoscope::capture::CaptureSession> target) {
  if (seams.on_session) seams.on_session(std::move(target));
}

void endRing(SessionContext& ctx, DeviceRun& run) {
  if (!run.ring) return;
  run.ring->end();
  ctx.events.ringPeak(run.index, run.ring->peakSlotsInUse(), run.ring->slotCount());
  run.ring.reset();
}

std::optional<std::string> pluginGone(const DeviceRun& run) {
  return run.dev->plugin_closed ? run.dev->plugin_closed() : std::nullopt;
}

std::expected<void, std::string> prepareDevice(DeviceRun& run, const AcquisitionSeams& seams) {
  const AcquireRequest& req = run.dev->request;
  auto plan = planCapture(req, seams);
  if (!plan) return std::unexpected(plan.error());
  run.plan = *plan;

  auto la = makeLaClient(run.dev->device, run.dev->transport, seams.min_la_version);
  if (!la) return std::unexpected(la.error());
  run.la.emplace(std::move(*la));

  if (auto ring_ok = requireBulkRing(run.dev->device, *run.la); !ring_ok) return std::unexpected(ring_ok.error());
  auto geometry = ringGeometryFor(req, seams);
  if (!geometry) return std::unexpected(geometry.error());
  run.ring_geometry = *geometry;
  const uint64_t first_id = seams.new_capture_id ? seams.new_capture_id() : uint64_t{0};
  if (first_id == 0) return std::unexpected(noCaptureIdMessage());
  // WHY: A triggered capture is sent unbounded: the plan total must not become
  // the device's stop point; untriggered sends its length.
  run.request = bitoscope::host::pipe::CaptureRequest{
      .sample_rate_milli_hz = req.sample_rate_milli_hz,
      .channel_mask = req.channel_mask,
      .sample_limit = req.trigger.has_value() ? uint64_t{0} : req.total_samples,
      .mode = ipc::la::AcquisitionMode::streaming,
      .capture_id = first_id};
  if (auto checked = run.la->check(run.request); !checked) {
    return std::unexpected(couldNotStartCapture(run.id(), checked.error()));
  }

  return {};
}

std::expected<void, std::string> sizeHeldBack(DeviceRun& run) {
  auto held_back = heldBackFor(*run.plan, run.dev->request, run.ring_geometry);
  if (!held_back) return std::unexpected(held_back.error());
  run.held_back = std::move(*held_back);
  return {};
}

// WHY: Triggered floor is the ring floor in whole RAW windows, so no answer promises more than the ring holds.
std::expected<void, std::string> answerMemory(DeviceRun& run) {
  const bitoscope::capture::Reservation& reservation = *run.reservation;
  std::optional<AcquireWarning> trigger_warning;
  if (const auto& trigger = run.trigger(); trigger.has_value()) {
    const uint64_t ring_floor = bitoscope::capture::historyRingSamples(reservation);
    auto answer = describeTriggeredMemory(ring_floor, *trigger, *run.plan);
    if (!answer) {
      return std::unexpected("could not reserve memory for triggered capture - nothing was started: " +
                             answer.error());
    }
    if (answer->has_value()) {
      const auto fit = bitoscope::capture::triggeredMemoryFit(ring_floor, trigger->pre_trigger_samples,
                                                              trigger->post_trigger_samples,
                                                              run.plan->samplesPerWindow());
      trigger_warning = AcquireWarning{AcquireWarning::Kind::Floor, std::move(**answer), fit.post_floor};
    }
  }
  for (const std::string& warning : reservation.warnings) {
    run.warnings.push_back(AcquireWarning{AcquireWarning::Kind::Memory, warning, std::nullopt});
  }
  if (!run.trigger().has_value()) {
    if (auto floor = describeUntriggeredMemory(reservation.samplesFloor, *run.plan, run.growth.has_value())) {
      run.warnings.push_back(AcquireWarning{AcquireWarning::Kind::Floor, std::move(*floor), reservation.samplesFloor});
    }
  }
  if (trigger_warning.has_value()) run.warnings.push_back(std::move(*trigger_warning));
  return {};
}

std::expected<void, std::string> reserveGrowing(DeviceRun& run, bitoscope::capture::AllocatorSettings settings,
                                                const AcquisitionSeams& seams) {
  settings = growingArenaSettings(std::move(settings));
  auto budget = bitoscope::capture::computeBudget(seams.probe, settings.budget);
  if (!budget) return std::unexpected(couldNotReserve(budget.error()));
  auto grown = bitoscope::capture::planGrowingArena(*run.plan, *budget, seams.probe, seams.blocks, settings);
  if (!grown) return std::unexpected(couldNotReserve(grown.error()));
  bitoscope::capture::Reservation& reservation = grown->reservation;
  if (reservation.samplesFloor == 0) {
    return std::unexpected(couldNotReserve(bitoscope::capture::noWindowRefusal(*run.plan, budget->bytes)));
  }
  auto first = grown->growth.supply(0, reservation.blockSize(0));
  if (!first) return std::unexpected(couldNotReserve(first.error().why));
  reservation.blocks[0] = std::move(*first);
  run.reservation.emplace(std::move(reservation));
  run.growth.emplace(std::move(grown->growth));
  return answerMemory(run);
}

std::chrono::milliseconds blockWait(const DeviceRun& run, const AcquisitionSeams& seams) {
  constexpr uint64_t kBitsPerByte = 8;
  constexpr uint64_t kMilli = 1000;
  const uint64_t ring_bytes = uint64_t{run.ring_geometry.slot_count} * run.ring_geometry.slot_payload_bytes;
  const uint64_t channel_milli_hz = uint64_t{run.plan->channels()} * run.plan->sampleRateMilliHz();
  const uint64_t slack_ms = channel_milli_hz == 0
                                ? seams.inactivity_timeout_ms
                                : mulDivFloor(ring_bytes, kBitsPerByte * kMilli * kMilli, channel_milli_hz);
  // WHY: Not under one poll: a source that waits (test, unpaced demo) loses nothing, yet a tiny ring fails it.
  const uint64_t waited = (std::min)((std::max)(slack_ms, seams.poll_ms), seams.inactivity_timeout_ms);
  return std::chrono::milliseconds(waited);
}

template <typename Named>
std::expected<void, std::string> reserveMemory(std::vector<DeviceRun>& runs, const SessionRequest& session,
                                               const AcquisitionSeams& seams, const Named& named) {
  for (DeviceRun& run : runs) {
    if (auto sized = sizeHeldBack(run); !sized) return std::unexpected(named(run, sized.error()));
  }
  if (runs.size() == 1) {
    DeviceRun& run = runs[0];
    const bitoscope::capture::AllocatorSettings settings =
        singleDeviceSettings(run.dev->request, session, seams, run.held_back);
    if (!run.trigger().has_value()) return reserveGrowing(run, settings, seams);
    auto reservation = bitoscope::capture::allocateCapture(*run.plan, seams.probe, seams.blocks, settings);
    if (!reservation) return std::unexpected(couldNotReserve(reservation.error()));
    run.reservation.emplace(std::move(*reservation));
    return answerMemory(run);
  }

  uint64_t held_back = 0;
  uint64_t total_rate = 0;
  for (const DeviceRun& run : runs) {
    held_back = held_back > bitoscope::capture::plan_detail::kMaxU64 - run.held_back.outside_arena
                    ? bitoscope::capture::plan_detail::kMaxU64
                    : held_back + run.held_back.outside_arena;
    total_rate += uint64_t{run.plan->channels()} * run.plan->sampleRateMilliHz();
  }
  bitoscope::capture::BudgetSettings budget_settings;
  budget_settings.explicit_budget = session.memory_budget_bytes;
  budget_settings.reserve = session.reserve_bytes;
  budget_settings.held_back = held_back;
  budget_settings.held_back_for_arena = [&runs, total_rate](uint64_t budget_bytes) {
    uint64_t sum = 0;
    for (const DeviceRun& run : runs) {
      if (!run.held_back.for_arena) continue;
      const uint64_t rate = uint64_t{run.plan->channels()} * run.plan->sampleRateMilliHz();
      const uint64_t planes = run.held_back.for_arena(mulDivFloor(budget_bytes, rate, total_rate));
      sum = planes > bitoscope::capture::plan_detail::kMaxU64 - sum ? bitoscope::capture::plan_detail::kMaxU64
                                                                     : sum + planes;
    }
    return sum;
  };
  auto budget = bitoscope::capture::computeBudget(seams.probe, budget_settings);
  if (!budget) {
    return std::unexpected("could not reserve memory for session - nothing was started: " + budget.error());
  }
  for (DeviceRun& run : runs) {
    const uint64_t rate = uint64_t{run.plan->channels()} * run.plan->sampleRateMilliHz();
    const uint64_t share = mulDivFloor(budget->bytes, rate, total_rate);
    bitoscope::capture::AllocatorSettings settings = arenaSettings(run.dev->request, session, seams);
    settings.budget.explicit_budget = share;
    settings.budget.reserve = budget->reserve;
    settings.budget.held_back = budget->held_back;
    settings.budget.fit_explicit_to_ceiling = true;
    auto reservation = bitoscope::capture::allocateCapture(*run.plan, seams.probe, seams.blocks, settings);
    if (!reservation) {
      return std::unexpected(named(run, "could not reserve memory for capture (share of " + std::to_string(share) +
                                            " bytes of session's " + std::to_string(budget->bytes) +
                                            ") - nothing was started: " + reservation.error()));
    }
    run.reservation.emplace(std::move(*reservation));
    if (auto answered = answerMemory(run); !answered) return std::unexpected(named(run, answered.error()));
  }
  return {};
}

std::expected<void, AttemptError> startAttempt(SessionContext& ctx, DeviceRun& run) {
  const AcquisitionSeams& seams = ctx.seams;
  const auto fail = [](std::string message) {
    return std::unexpected(AttemptError{std::nullopt, 0, std::move(message)});
  };
  const std::string& id = run.id();
  auto buffer = bitoscope::capture::RawCaptureBuffer::create(std::move(*run.reservation), run.dev->request.pyramid);
  if (!buffer) return fail("could not set up capture buffer: " + buffer.error());
  bitoscope::capture::SessionConfig config;
  config.inactivity_timeout_ms = seams.inactivity_timeout_ms;
  config.now_ms = seams.now_ms;
  config.capture_id = run.request.capture_id;
  config.plugin_id = id;
  config.trigger = run.trigger();
  config.pack_workers = run.dev->request.pack_workers;
  config.live_tail = run.dev->request.live_tail;
  if (run.growth) {
    config.block_supply = std::make_shared<bitoscope::capture::BlockSupplyAhead>(*run.growth, blockWait(run, seams),
                                                                                 seams.clearing_threads_for_test);
    config.arena_memory = run.growth->memory();
  }
  auto made = bitoscope::capture::CaptureSession::create(std::move(*buffer), std::move(config));
  if (!made) return fail("could not set up capture session: " + made.error());
  std::shared_ptr<bitoscope::capture::CaptureSession> capture = std::move(*made);
  attach(seams, capture);

  // WHY: The pump reads the ring BEFORE START goes out, so the host is reading when the first chunk is written.
  bitoscope::host::pipe::CaptureRequest start_request = run.request;
  auto made_ring = AttemptRing::start(seams.create_ring, run.ring_geometry, run.request.capture_id, capture);
  if (!made_ring) {
    attach(seams, nullptr);
    return fail(couldNotSetUpRing(id, made_ring.error()));
  }
  std::unique_ptr<AttemptRing> ring = std::move(*made_ring);
  start_request.bulk_ring_name = ring->name();

  capture->deviceStarted();
  if (auto started = run.la->start(start_request); !started) {
    ring.reset();
    attach(seams, nullptr);
    return fail(couldNotStartCapture(id, started.error()));
  }
  ctx.events.started(run.index, id, run.request.capture_id);
  run.capture = std::move(capture);
  run.ring = std::move(ring);
  run.running = true;
  run.progress_given = false;
  run.last_progress_ms = 0;
  return {};
}

std::expected<AcquireResult, AttemptError> finishAttempt(SessionContext& ctx, DeviceRun& run,
                                                         std::optional<std::string> gone) {
  const AcquisitionSeams& seams = ctx.seams;
  AcquisitionEvents& events = ctx.events;
  const std::string& id = run.id();
  const auto fail = [](std::string message) {
    return std::unexpected(AttemptError{std::nullopt, 0, std::move(message)});
  };
  run.running = false;
  std::shared_ptr<bitoscope::capture::CaptureSession> capture = std::move(run.capture);
  const uint64_t capture_id = run.request.capture_id;
  const bitoscope::capture::CapturePlan& plan = *run.plan;
  const std::optional<bitoscope::trigger::TriggerSpec>& trigger = run.trigger();

  const auto driverEnded = [&](const std::string& reason) {
    attach(seams, nullptr);
    (void)capture->finish();
    return fail("capture failed and nothing was saved - driver ended during capture: " + reason +
                describeTally(*capture, capture_id));
  };
  if (!gone) gone = pluginGone(run);
  if (gone) {
    capture->requestStop();
    endRing(ctx, run);
    return driverEnded(*gone);
  }

  // WHY: STOP is always sent, even when the driver stopped itself: a failed STOP leaves hardware state unknown.
  auto stopped = run.la->stop();
  endRing(ctx, run);
  if (!stopped) {
    // WHY: Ring and pipe are not ordered: ring can complete before the reader sees the driver end on its pipe.
    if (const auto ended = pluginGone(run)) return driverEnded(*ended);
    attach(seams, nullptr);
    return fail(mayStillBeRunning(id, stopped.error()));
  }
  events.stopped(run.index);

  if (const auto failure = capture->failure()) {
    attach(seams, nullptr);
    (void)capture->finish();
    AttemptError error{failure->kind, failure->lost_samples,
                       "capture failed and nothing was saved - " + failure->message +
                           describeTally(*capture, capture_id)};
    using bitoscope::capture::FailureKind;
    if ((failure->kind == FailureKind::Dropped || failure->kind == FailureKind::EndedEarly) &&
        capture->chunksReceived() == 0) {
      error.source_before_first_chunk = true;
      error.source_message = failure->message;
    }
    if (const auto lost = run.dev->device_lost ? run.dev->device_lost() : std::nullopt) {
      error.device_lost = true;
      if (error.message.find(*lost) == std::string::npos) error.message += " (device is gone: " + *lost + ")";
    }
    if (error.isRetryable()) {
      auto returned = capture->releaseReservation();
      if (!returned) return fail("could not reuse capture memory: " + returned.error());
      run.reservation = std::move(*returned);
    }
    return std::unexpected(std::move(error));
  }

  const CaptureStatus ended_as = capture->status();
  const bool keep_short =
      (ended_as == CaptureStatus::Full && ctx.keep_on_memory_full) || ended_as == CaptureStatus::Stopped;

  if (ended_as != CaptureStatus::Complete && !keep_short) {
    const std::optional<uint64_t> trigger_timeout_s = (trigger.has_value() && capture->triggerTimedOut())
                                                          ? std::optional<uint64_t>(trigger->timeout_s)
                                                          : std::nullopt;
    attach(seams, nullptr);
    auto early = capture->finish();
    if (!early) return fail("could not finish capture: " + early.error());
    AttemptError error{std::nullopt, 0,
                       describeEarlyEnd(*early, plan, seams.inactivity_timeout_ms, trigger_timeout_s) +
                           describeTally(*capture, capture_id)};
    error.no_data = capture->timedOutBeforeFirstChunk();
    if (const auto lost = run.dev->device_lost ? run.dev->device_lost() : std::nullopt) {
      error.device_lost = true;
      error.message += " (device is gone: " + *lost + ")";
    }
    if (error.isRetryable()) {
      auto returned = capture->releaseReservation();
      if (!returned) return fail("could not reuse capture memory: " + returned.error());
      run.reservation = std::move(*returned);
    }
    return std::unexpected(std::move(error));
  }

  attach(seams, nullptr);
  auto summary = capture->finish();
  if (!summary) return fail("could not finish capture: " + summary.error());
  if (const auto failure = capture->failure()) {
    return std::unexpected(AttemptError{failure->kind, failure->lost_samples,
                                        "capture failed and nothing was saved - " + failure->message +
                                            describeTally(*capture, capture_id)});
  }

  AcquireResult result;
  if (keep_short) {
    if (summary->end == CaptureStatus::Stopped && summary->stored_samples == 0) {
      if (run.stopped_for) {
        const char* why = trigger.has_value()                ? "its trigger had not fired"
                          : capture->chunksReceived() == 0 ? "no data had arrived from it"
                                                             : "none of it had been stored yet";
        return fail("capture failed and nothing was saved - memory of device '" + run.stopped_for_id +
                    "' filled before this device stored anything (" + why + ")" + describeTally(*capture, capture_id));
      }
      return fail("stopped by user before anything was captured (trigger that had not fired has nothing to "
                  "keep); nothing was saved" + describeTally(*capture, capture_id));
    }
    if ((summary->end != CaptureStatus::Full && summary->end != CaptureStatus::Stopped) ||
        summary->stored_samples == 0) {
      return fail(describeEarlyEnd(*summary, plan, seams.inactivity_timeout_ms) + describeTally(*capture, capture_id));
    }
    // WHY: describeShortfall runs BEFORE the outcome is announced: a kept range short of its pre-trigger range must fail.
    auto shortfall = describeShortfall(*summary, *capture, trigger);
    if (!shortfall) return fail(shortfall.error() + describeTally(*capture, capture_id));
    if (run.stopped_for && summary->end == CaptureStatus::Stopped) {
      shortfall->reason = bitoscope::capture::ShortReason::StoppedWithSession;
      shortfall->memory_full_device = *run.stopped_for;
    }
    result.memory_shortfall = *shortfall;
  } else if (summary->end != CaptureStatus::Complete || summary->shortfall_samples != 0) {
    return fail(describeEarlyEnd(*summary, plan, seams.inactivity_timeout_ms) + describeTally(*capture, capture_id));
  }

  result.summary = *summary;
  result.session = std::move(capture);
  result.device = id;
  result.properties = run.dev->request.properties;
  result.other_facet_properties = run.dev->request.other_facet_properties;
  // WHY: Requested counts come from the plan and trigger spec, never from what was stored.
  if (trigger.has_value()) {
    result.requested_pre_trigger_samples = trigger->pre_trigger_samples;
    result.requested_post_trigger_samples = trigger->post_trigger_samples;
    result.requested_samples = trigger->pre_trigger_samples + trigger->post_trigger_samples;
  } else {
    result.requested_samples = plan.totalSamples();
  }
  return result;
}

std::string describeStored(const bitoscope::capture::CaptureProgress& progress) {
  return std::to_string(progress.stored_samples) + " of " + std::to_string(progress.requested_samples) +
         " samples stored";
}

std::unexpected<std::string> failSession(SessionContext& ctx, std::vector<DeviceRun>& runs, std::string message) {
  const bool several = runs.size() > 1;
  for (DeviceRun& run : runs) {
    if (run.result) {
      if (several) {
        using bitoscope::capture::ShortReason;
        const auto& shortfall = run.result->memory_shortfall;
        const char* how = !shortfall                                     ? "had completed"
                          : shortfall->reason == ShortReason::Memory     ? "had filled its memory"
                          : shortfall->reason == ShortReason::StoppedByUser ? "had been stopped by user"
                                                                            : "had stopped with session";
        message += "; device '" + run.id() + "' " + how + " and was discarded: " +
                   describeStored(run.result->session->progress());
      }
      continue;
    }
    if (!run.running) continue;
    run.running = false;
    std::shared_ptr<bitoscope::capture::CaptureSession> capture = std::move(run.capture);
    capture->requestStop();
    ctx.events.waitEnded(run.index);
    const auto stopped = run.la->stop();
    endRing(ctx, run);
    if (stopped) ctx.events.stopped(run.index);
    attach(ctx.seams, nullptr);
    (void)capture->finish();
    if (several) {
      message += "; device '" + run.id() + "' stopped and discarded: " + describeStored(capture->progress()) +
                 describeTally(*capture, run.request.capture_id);
    }
    if (!stopped) {
      if (const auto ended = pluginGone(run)) {
        message += "; driver ended during capture on '" + run.id() + "': " + *ended;
      } else {
        message += "; " + mayStillBeRunning(run.id(), stopped.error());
      }
    }
  }
  return std::unexpected(std::move(message));
}

std::expected<void, std::string> stopForRestart(SessionContext& ctx, DeviceRun& run) {
  run.running = false;
  std::shared_ptr<bitoscope::capture::CaptureSession> capture = std::move(run.capture);
  const uint64_t capture_id = run.request.capture_id;
  capture->requestStop();
  ctx.events.waitEnded(run.index);
  const auto driverEnded = [&](const std::string& reason) {
    (void)capture->finish();
    return std::unexpected("capture failed and nothing was saved - driver ended during capture: " + reason +
                           describeTally(*capture, capture_id));
  };
  if (const auto gone = pluginGone(run)) {
    endRing(ctx, run);
    attach(ctx.seams, nullptr);
    return driverEnded(*gone);
  }
  const auto stopped = run.la->stop();
  endRing(ctx, run);
  attach(ctx.seams, nullptr);
  if (!stopped) {
    // WHY: Ring and pipe are not ordered: driver's end can show on its pipe only once STOP is in flight.
    if (const auto ended = pluginGone(run)) return driverEnded(*ended);
    (void)capture->finish();
    return std::unexpected(mayStillBeRunning(run.id(), stopped.error()));
  }
  ctx.events.stopped(run.index);
  if (auto finished = capture->finish(); !finished) {
    return std::unexpected("could not finish capture: " + finished.error());
  }
  if (const auto failure = capture->failure()) {
    using bitoscope::capture::FailureKind;
    const bool before_first_chunk =
        failure->kind == FailureKind::StartUpLoss ||
        ((failure->kind == FailureKind::Dropped || failure->kind == FailureKind::EndedEarly) &&
         capture->chunksReceived() == 0);
    if (!before_first_chunk) {
      return std::unexpected("capture failed and nothing was saved - " + failure->message +
                             describeTally(*capture, capture_id));
    }
  }
  auto returned = capture->releaseReservation();
  if (!returned) return std::unexpected("could not reuse capture memory: " + returned.error());
  run.reservation = std::move(*returned);
  return {};
}

std::expected<void, std::string> reclaimForRestart(DeviceRun& run) {
  auto returned = run.result->session->releaseReservation();
  run.result.reset();
  if (!returned) return std::unexpected("could not reuse capture memory: " + returned.error());
  run.reservation = std::move(*returned);
  return {};
}

struct LostStart {
  DeviceRun* run = nullptr;
  AttemptError error;
  std::string lost;
};

struct UserRequests {
  bool abandon = false;
  bool stop = false;
};

UserRequests passUserRequests(const AcquisitionSeams& seams, std::vector<DeviceRun>& runs,
                              std::optional<uint32_t> memory_full) {
  const UserRequests asked{seams.abandon_requested && seams.abandon_requested(),
                           seams.stop_requested && seams.stop_requested()};
  for (DeviceRun& run : runs) {
    if (!run.running) continue;
    if (asked.abandon) {
      run.capture->requestStop();
    } else if (asked.stop) {
      run.capture->requestKeepStop();
    } else if (memory_full && !run.stopped_for) {
      run.stopped_for = *memory_full;
      run.stopped_for_id = runs[*memory_full].id();
      run.capture->requestSessionStop();
    }
  }
  return asked;
}

std::expected<SessionResult, std::string> runSessionBody(std::span<const SessionDevice> devices,
                                                         const SessionRequest& session, const AcquisitionSeams& seams,
                                                         AcquisitionEvents& events) {
  if (devices.empty()) return std::unexpected("session needs at least one device");
  const bool several = devices.size() > 1;
  const auto named = [several](const DeviceRun& run, std::string message) {
    return several ? "device '" + run.id() + "': " + message : message;
  };
  std::vector<DeviceRun> runs(devices.size());
  for (size_t i = 0; i < devices.size(); ++i) {
    runs[i].dev = &devices[i];
    runs[i].index = static_cast<uint32_t>(i);
  }
  SessionContext ctx{seams, events, session.keep_on_memory_full};

  for (DeviceRun& run : runs) {
    if (auto prepared = prepareDevice(run, seams); !prepared) return std::unexpected(named(run, prepared.error()));
  }
  for (const DeviceRun& run : runs) events.planned(run.index, *run.plan, run.trigger());
  if (auto reserved = reserveMemory(runs, session, seams, named); !reserved) return std::unexpected(reserved.error());
  for (DeviceRun& run : runs) {
    for (const AcquireWarning& warning : run.warnings) events.warning(run.index, warning);
  }
  for (DeviceRun& run : runs) {
    const auto& values = run.dev->request.properties;
    if (values.empty()) continue;
    auto set = run.dev->transport.setProperties(run.id(), bitoscope::host::pipe::kLaAcquisitionFacetName, values);
    if (!set) {
      return std::unexpected(named(run, propertiesNotSet(run.id(), set.error())));
    }
    events.propertiesSet(run.index, run.id(), values);
  }
  for (DeviceRun& run : runs) {
    for (const auto& [facet, values] : run.dev->request.other_facet_properties) {
      if (values.empty()) continue;
      auto set = run.dev->transport.setProperties(run.id(), facet, values);
      if (!set) {
        return std::unexpected(named(run, propertiesNotSet(facet, set.error())));
      }
      events.propertiesSet(run.index, run.id(), values);
    }
  }
  for (DeviceRun& run : runs) {
    if (auto started = startAttempt(ctx, run); !started) return failSession(ctx, runs, named(run, started.error().message));
  }

  uint64_t attempt = 1;
  const uint64_t attempts_allowed = uint64_t{session.start_retries} + 1;
  std::string lost_starts;

  std::optional<uint32_t> memory_full;
  for (;;) {
    UserRequests asked = passUserRequests(seams, runs, memory_full);
    std::vector<std::pair<DeviceRun*, std::optional<std::string>>> ended;
    std::optional<uint64_t> now;
    for (DeviceRun& run : runs) {
      if (!run.running) continue;
      // WHY: Driver death is checked ahead of an abandon request in the same
      // poll: integrity failure outranks the user's action.
      if (auto gone = pluginGone(run)) {
        ended.emplace_back(&run, std::move(gone));
        continue;
      }
      if (run.capture->status() != CaptureStatus::Running) {
        ended.emplace_back(&run, std::nullopt);
        continue;
      }
      if (events.wantsProgress()) {
        if (!now) now = seams.now_ms();
        if (!run.progress_given || *now - run.last_progress_ms >= seams.progress_interval_ms) {
          events.progress(run.index, *now, progressOf(*run.capture, *run.ring, run.trigger()));
          run.progress_given = true;
          run.last_progress_ms = *now;
        }
      }
    }

    // WHY: Read again once an end is seen: a request landing after the first read would be lost to that end.
    if (!ended.empty()) asked = passUserRequests(seams, runs, memory_full);

    const std::string where = attempt > 1 ? " (attempt " + std::to_string(attempt) + " of " +
                                                std::to_string(attempts_allowed) + ")"
                                          : std::string();
    std::vector<LostStart> lost;
    for (auto& [run, gone] : ended) {
      events.waitEnded(run->index);
      auto done = finishAttempt(ctx, *run, std::move(gone));
      if (done) {
        run->result = std::move(*done);
        const auto& shortfall = run->result->memory_shortfall;
        if (shortfall && shortfall->reason == bitoscope::capture::ShortReason::Memory && !asked.stop && !memory_full) {
          memory_full = run->index;
        }
        continue;
      }
      AttemptError error = std::move(done.error());
      // WHY: CaptureEnded is read after STOP: one pipe keeps order, so an event sent before STOP's reply is already in.
      if (run->dev->capture_ended) {
        if (const auto said = run->dev->capture_ended(run->request.capture_id);
            said && said->reason != ipc::la::EndReason::sampleLimitReached && !said->message.empty() &&
            error.message.find(said->message) == std::string::npos) {
          error.message += " (driver reported: " + said->message + ")";
        }
      }
      if (!error.isRetryable()) return failSession(ctx, runs, named(*run, error.message + where));
      std::string what = describeLostStart(error, seams.inactivity_timeout_ms);
      lost.push_back(LostStart{run, std::move(error), std::move(what)});
    }

    if (!lost.empty()) {
      const LostStart& first = lost.front();
      lost_starts += (attempt > 1 ? "; attempt " : "attempt ") + std::to_string(attempt) + ": ";
      for (size_t i = 0; i < lost.size(); ++i) {
        if (i > 0) lost_starts += ", and ";
        lost_starts += several ? "device '" + lost[i].run->id() + "': " + lost[i].lost : lost[i].lost;
      }
      if (attempt >= attempts_allowed) {
        if (attempts_allowed == 1) return failSession(ctx, runs, named(*first.run, first.error.message));
        return failSession(ctx, runs,
                           "capture failed and nothing was saved - all " + std::to_string(attempts_allowed) +
                               " attempts lost their start (" + lost_starts +
                               "); last: " + named(*first.run, first.error.message));
      }
      for (DeviceRun& run : runs) {
        if (run.running) {
          if (auto stopped = stopForRestart(ctx, run); !stopped) {
            return failSession(ctx, runs, named(run, stopped.error() + where));
          }
        } else if (run.result) {
          if (auto reclaimed = reclaimForRestart(run); !reclaimed) {
            return failSession(ctx, runs, named(run, reclaimed.error() + where));
          }
        }
      }
      // WHY: An abandon or Stop asked for starts no new attempt: what the
      // failed attempt holds is broken, so Stop keeps nothing.
      const std::string last_lost = std::string(first.error.no_data ? "attempt received no data: "
                                                                    : "attempt lost its start: ") +
                                    first.error.message;
      const std::string next = std::to_string(attempt + 1) + " of " + std::to_string(attempts_allowed);
      if (asked.abandon || (seams.abandon_requested && seams.abandon_requested())) {
        return failSession(ctx, runs,
                           named(*first.run, "interrupted before attempt " + next + "; nothing was saved - last " +
                                                 last_lost));
      }
      if (asked.stop || (seams.stop_requested && seams.stop_requested())) {
        return failSession(ctx, runs,
                           named(*first.run, "stopped by user before attempt " + next +
                                                 "; nothing was saved - last " + last_lost));
      }
      std::vector<uint64_t> old_ids;
      for (DeviceRun& run : runs) {
        old_ids.push_back(run.request.capture_id);
        run.request.capture_id = seams.new_capture_id ? seams.new_capture_id() : uint64_t{0};
        if (run.request.capture_id == 0) {
          return failSession(ctx, runs, named(run, noCaptureIdMessage()));
        }
      }
      for (const LostStart& one : lost) {
        const DeviceRun& run = *one.run;
        events.retrying(run.index, AcquireRetry{one.lost, old_ids[run.index], run.request.capture_id, attempt + 1,
                                                attempts_allowed, run.id()});
      }
      ++attempt;
      memory_full.reset();
      for (DeviceRun& run : runs) {
        run.stopped_for.reset();
        if (auto started = startAttempt(ctx, run); !started) {
          return failSession(ctx, runs,
                             named(run, started.error().message + " (attempt " + std::to_string(attempt) + " of " +
                                            std::to_string(attempts_allowed) + ")"));
        }
      }
    }

    bool any_running = false;
    for (const DeviceRun& run : runs) any_running = any_running || run.running;
    if (!any_running) break;
    if (ended.empty()) seams.sleep_ms(seams.poll_ms);
  }

  // WHY: Give unused memory back only once the session is kept: a later restart must reuse the reservation.
  SessionResult result;
  for (DeviceRun& run : runs) {
    AcquireResult& kept = *run.result;
    if (memory_full && run.index != *memory_full && !kept.memory_shortfall) {
      bitoscope::capture::MemoryShortfall shortfall;
      shortfall.requested_samples = kept.requested_samples;
      shortfall.stored_samples = kept.summary.stored_samples;
      shortfall.requested_post_trigger_samples = kept.requested_post_trigger_samples;
      shortfall.stored_post_trigger_samples = kept.requested_post_trigger_samples;
      shortfall.reason = bitoscope::capture::ShortReason::StoppedWithSession;
      shortfall.memory_full_device = *memory_full;
      kept.memory_shortfall = shortfall;
    }
    (void)kept.session->releaseUnusedMemory();
    events.completed(run.index, kept.summary, *run.plan, *kept.session, kept.memory_shortfall);
    result.devices.push_back(std::move(kept));
  }
  return result;
}

std::expected<SessionResult, std::string> runSession(std::span<const SessionDevice> devices,
                                                     const SessionRequest& session, const AcquisitionSeams& seams,
                                                     AcquisitionEvents& events) {
  auto result = runSessionBody(devices, session, seams, events);
  bitoscope::heap::giveFreeHeapBack();
  return result;
}

}

}

std::expected<SessionResult, std::string> acquireSession(std::span<const SessionDevice> devices,
                                                         const SessionRequest& session, const AcquisitionSeams& seams,
                                                         AcquisitionEvents& events) {
  return detail::runSession(devices, session, seams, events);
}

std::expected<MemoryCheck, std::string> checkMemoryBeforeStart(const AcquireRequest& req, const SessionRequest& session,
                                                               const AcquisitionSeams& seams) {
  auto planning = planSingleDevice(req, session, seams);
  if (!planning) return std::unexpected(planning.error());
  const bitoscope::capture::CapturePlan* plan = &planning->plan;
  auto counted_reservation = countedReservation(*planning);
  if (!counted_reservation) return std::unexpected(counted_reservation.error());
  const bitoscope::capture::Reservation& counted = *counted_reservation;

  MemoryCheck check;
  if (const auto& trigger = req.trigger; trigger.has_value()) {
    const uint64_t ring_floor = bitoscope::capture::historyRingSamples(counted);
    auto answer = detail::describeTriggeredMemory(ring_floor, *trigger, *plan);
    if (!answer) {
      return std::unexpected("could not reserve memory for triggered capture - nothing was started: " +
                             answer.error());
    }
    if (answer->has_value()) {
      const auto fit = bitoscope::capture::triggeredMemoryFit(ring_floor, trigger->pre_trigger_samples,
                                                              trigger->post_trigger_samples, plan->samplesPerWindow());
      check.answer = MemoryCheck::Answer::Warn;
      check.floor_samples = fit.post_floor;
      check.text = std::move(**answer);
    }
  } else if (auto floor = detail::describeUntriggeredMemory(counted.samplesFloor, *plan, true)) {
    check.answer = MemoryCheck::Answer::Warn;
    check.floor_samples = counted.samplesFloor;
    check.text = std::move(*floor);
  }
  return check;
}

std::expected<AcquireResult, std::string> acquire(const AcquireRequest& req,
                                                  const bitoscope::host::pipe::PluginDevice& device,
                                                  bitoscope::host::FacetTransport& transport,
                                                  const AcquisitionSeams& seams, AcquisitionEvents& events) {
  const SessionDevice one{device, transport, req, seams.plugin_closed, seams.device_lost, seams.capture_ended};
  const SessionRequest session{req.memory_policy, req.memory_budget_bytes, req.reserve_bytes, req.keep_on_memory_full,
                               req.start_retries};
  auto result = detail::runSession(std::span<const SessionDevice>(&one, 1), session, seams, events);
  if (!result) return std::unexpected(std::move(result.error()));
  return std::move(result->devices.front());
}

}

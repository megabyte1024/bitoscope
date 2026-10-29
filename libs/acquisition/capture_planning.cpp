/*
 * SPDX-License-Identifier: MIT
 */

#include "capture_planning.h"

#include <algorithm>
#include <bit>
#include <new>
#include <utility>

#include "history_ring_floor.h"

namespace bitoscope::acquisition {

std::expected<bitoscope::capture::CapturePlan, std::string> planCapture(const AcquireRequest& request,
                                                                        const AcquisitionSeams& seams) {
  const uint32_t channels = static_cast<uint32_t>(std::popcount(request.channel_mask));
  // WHY: Triggered plan is pre + post + one whole window (store front
  // offset, never saved); the spec keeps the TRUE counts.
  uint64_t plan_total_samples = request.total_samples;
  if (request.trigger.has_value()) {
    if (auto validated = request.trigger->validate(channels); !validated) {
      return std::unexpected("cannot arm trigger: " + validated.error());
    }
    const uint64_t pre = request.trigger->pre_trigger_samples;
    const uint64_t post = request.trigger->post_trigger_samples;
    if (pre > bitoscope::capture::plan_detail::kMaxU64 - post ||
        pre + post > bitoscope::capture::plan_detail::kMaxU64 - seams.samples_per_window) {
      return std::unexpected("cannot arm trigger: requested capture length is too long to plan");
    }
    plan_total_samples = pre + post + seams.samples_per_window;
  }
  auto plan = bitoscope::capture::makeCapturePlan(channels, request.sample_rate_milli_hz, plan_total_samples,
                                                   seams.samples_per_window);
  if (!plan) return std::unexpected("cannot plan capture: " + plan.error());
  return *plan;
}

std::expected<RingGeometry, std::string> ringGeometryFor(const AcquireRequest& request,
                                                         const AcquisitionSeams& seams) {
  if (seams.ring_geometry_for_test) return *seams.ring_geometry_for_test;
  auto geometry = ringGeometryForMiB(request.ring_mib);
  if (!geometry) {
    return std::unexpected("cannot size bulk ring (--ring-mib) - nothing was started: " + geometry.error());
  }
  return *geometry;
}

std::expected<HeldBack, std::string> heldBackFor(const bitoscope::capture::CapturePlan& plan,
                                                 const AcquireRequest& request, const RingGeometry& geometry) {
  auto held_back = bitoscope::capture::captureHeldBackBytes(plan, request.pack_workers, request.pyramid);
  if (!held_back) {
    return std::unexpected("could not size capture's compression scratch - nothing was started: " +
                           held_back.error());
  }
  auto acquisition_held_back = acquisitionHeldBackBytes(*held_back, geometry);
  if (!acquisition_held_back) {
    return std::unexpected("could not size capture's memory - nothing was started: " +
                           acquisition_held_back.error());
  }
  HeldBack sized;
  sized.outside_arena = *acquisition_held_back;
  if (request.pyramid) sized.for_arena = bitoscope::capture::pyramidPlanesHeldBack(plan);
  return sized;
}

bitoscope::capture::AllocatorSettings arenaSettings(const AcquireRequest& request, const SessionRequest& session,
                                                    const AcquisitionSeams& seams) {
  bitoscope::capture::AllocatorSettings settings;
  settings.policy = session.memory_policy;
  settings.arena_block_bytes = seams.arena_block_bytes_for_test;
  settings.clearing_workers = request.clearing_workers;
  settings.clearing_threads = seams.clearing_threads_for_test;
  settings.arena_cap = request.trigger.has_value() ? bitoscope::capture::ArenaCap::WholeSlotsWithRingMargin
                                                   : bitoscope::capture::ArenaCap::Plan;
  return settings;
}

bitoscope::capture::AllocatorSettings singleDeviceSettings(const AcquireRequest& request,
                                                           const SessionRequest& session,
                                                           const AcquisitionSeams& seams, const HeldBack& held_back) {
  bitoscope::capture::AllocatorSettings settings = arenaSettings(request, session, seams);
  settings.budget.explicit_budget = session.memory_budget_bytes;
  settings.budget.reserve = session.reserve_bytes;
  settings.budget.held_back = held_back.outside_arena;
  settings.budget.held_back_for_arena = held_back.for_arena;
  return settings;
}

bitoscope::capture::AllocatorSettings growingArenaSettings(bitoscope::capture::AllocatorSettings settings) {
  settings.held_back_taken_before_arena = true;
  settings.commit_without_clearing = true;
  return settings;
}

std::string couldNotReserve(const std::string& why) {
  return "could not reserve memory for capture - nothing was started: " + why;
}

std::expected<CapturePlanning, std::string> planSingleDevice(const AcquireRequest& request,
                                                             const SessionRequest& session,
                                                             const AcquisitionSeams& seams) {
  auto plan = planCapture(request, seams);
  if (!plan) return std::unexpected(plan.error());
  auto geometry = ringGeometryFor(request, seams);
  if (!geometry) return std::unexpected(geometry.error());
  auto held_back = heldBackFor(*plan, request, *geometry);
  if (!held_back) return std::unexpected(held_back.error());
  bitoscope::capture::AllocatorSettings settings = singleDeviceSettings(request, session, seams, *held_back);
  auto shape = bitoscope::capture::shapeArena(*plan, seams.probe, settings);
  if (!shape) return std::unexpected(couldNotReserve(shape.error()));
  return CapturePlanning{*plan, *geometry, std::move(*held_back), std::move(settings), std::move(*shape)};
}

std::expected<bitoscope::capture::Reservation, std::string> countedReservation(const CapturePlanning& planning) {
  bitoscope::capture::Reservation counted;
  counted.plan = planning.plan;
  counted.slotsRequested = planning.plan.slots();
  counted.block_bytes = planning.shape.block_bytes;
  counted.arena_bytes = planning.shape.arena_target;
  try {
    counted.blocks.resize(static_cast<size_t>(planning.shape.block_count));
  } catch (const std::bad_alloc&) {
    return std::unexpected(couldNotReserve("out of memory tracking capture's arena"));
  }
  counted.samplesFloor = (std::min)(bitoscope::capture::guaranteedSamples(counted), planning.plan.totalSamples());
  if (counted.samplesFloor == 0) {
    return std::unexpected(couldNotReserve(bitoscope::capture::noWindowRefusal(planning.plan, planning.shape.budget.bytes)));
  }
  return counted;
}

}

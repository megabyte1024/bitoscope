/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <cstdint>
#include <expected>
#include <functional>
#include <string>

#include "acquisition.h"
#include "capture_allocator.h"
#include "capture_plan.h"
#include "ring_config.h"

namespace bitoscope::acquisition {

struct HeldBack {
  uint64_t outside_arena = 0;
  std::function<uint64_t(uint64_t)> for_arena;
};

struct CapturePlanning {
  bitoscope::capture::CapturePlan plan;
  RingGeometry ring_geometry;
  HeldBack held_back;
  bitoscope::capture::AllocatorSettings settings;
  bitoscope::capture::ArenaShape shape;
};

std::expected<bitoscope::capture::CapturePlan, std::string> planCapture(const AcquireRequest& request,
                                                                        const AcquisitionSeams& seams);

std::expected<RingGeometry, std::string> ringGeometryFor(const AcquireRequest& request,
                                                         const AcquisitionSeams& seams);

std::expected<HeldBack, std::string> heldBackFor(const bitoscope::capture::CapturePlan& plan,
                                                 const AcquireRequest& request, const RingGeometry& geometry);

bitoscope::capture::AllocatorSettings arenaSettings(const AcquireRequest& request, const SessionRequest& session,
                                                    const AcquisitionSeams& seams);

bitoscope::capture::AllocatorSettings singleDeviceSettings(const AcquireRequest& request,
                                                           const SessionRequest& session,
                                                           const AcquisitionSeams& seams, const HeldBack& held_back);

bitoscope::capture::AllocatorSettings growingArenaSettings(bitoscope::capture::AllocatorSettings settings);

std::string couldNotReserve(const std::string& why);

std::expected<CapturePlanning, std::string> planSingleDevice(const AcquireRequest& request,
                                                             const SessionRequest& session,
                                                             const AcquisitionSeams& seams);

std::expected<bitoscope::capture::Reservation, std::string> countedReservation(const CapturePlanning& planning);

}

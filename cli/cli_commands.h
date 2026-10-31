/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <algorithm>
#include <chrono>
#include <expected>
#include <functional>
#include <mutex>
#include <optional>
#include <ostream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "channel_mask.h"
#include "cli_args.h"
#include "cli_settings.h"
#include "facet_access.h"
#include "facet_client.h"
#include "host_ring.h"
#include "la_pipe_client.h"
#include "driver_device.h"
#include "ring_config.h"
#include "ring_summary.h"
#include "siggen_pipe_client.h"
#include "start_messages.h"

namespace bitoscope::cli {

struct StartCaptureSeams {
  acquisition::CreateRingFn create_ring;
  std::function<uint64_t()> new_capture_id;
  std::optional<acquisition::RingGeometry> ring_geometry_for_test;
  std::chrono::milliseconds end_wait{2000};
  RingSummaryConfig summary;
  std::function<std::optional<std::string>()> driver_closed;
};

namespace detail {

// WHY: Redundant with parseArgs on purpose: runAction takes any CliOptions, and a wrapped frequency is wrong.
inline std::expected<uint64_t, std::string> toMilliHz(uint64_t hz, std::string_view what) {
  if (hz > kMaxWholeHz) {
    return std::unexpected(std::string(what) + " of " + std::to_string(hz) +
                            " Hz cannot be expressed in millihertz");
  }
  return hz * 1000;
}

}

inline std::expected<void, std::string> runAction(const CliOptions& opts,
                                                  const host::pipe::DriverDevice& session,
                                                  host::FacetTransport& transport,
                                                  std::ostream& out,
                                                  const std::function<void()>& hold,
                                                  const StartCaptureSeams& capture_seams = {}) {
  const std::string& id = session.driver_device_id;

  switch (opts.action) {
    case Action::StartCapture: {
      auto la = acquisition::makeLaClient(session, transport);
      if (!la.has_value()) {
        return std::unexpected(la.error());
      }
      if (auto ring_ok = acquisition::requireBulkRing(session, *la); !ring_ok.has_value()) {
        return std::unexpected(ring_ok.error());
      }
      const auto rate = detail::toMilliHz(opts.rate_hz, "sample rate");
      if (!rate.has_value()) {
        return std::unexpected(rate.error());
      }
      host::pipe::CaptureRequest request{
          .sample_rate_milli_hz = *rate,
          .channel_mask = opts.channel_mask,
          .sample_limit = opts.sample_limit,
          .mode = opts.buffered ? ipc::la::AcquisitionMode::buffered : ipc::la::AcquisitionMode::streaming};

      // WHY: Check everything the device can refuse BEFORE sending any setting, or it is left reconfigured for nothing.
      if (auto checked = la->check(request); !checked.has_value()) {
        return std::unexpected(acquisition::couldNotStartCapture(id, checked.error()));
      }
      if (auto ceiling = checkRateCeiling(request.sample_rate_milli_hz, request.channel_mask, la->capabilities());
          !ceiling.has_value()) {
        return std::unexpected(acquisition::couldNotStartCapture(id, ceiling.error()));
      }
      auto settings = readAnalyzerSettings(opts.configure, session, &out);
      if (!settings.has_value()) {
        return std::unexpected(settings.error());
      }

      // WHY: Make the ring before sending anything and read it before START goes out, so the host reads from chunk one.
      if (!capture_seams.create_ring || !capture_seams.new_capture_id) {
        return std::unexpected(
            acquisition::couldNotStartCapture(id, "nothing to make its bulk ring with - nothing was started"));
      }
      const uint64_t capture_id = capture_seams.new_capture_id();
      if (capture_id == 0) {
        return std::unexpected(acquisition::noCaptureIdMessage());
      }
      acquisition::RingGeometry geometry;
      if (capture_seams.ring_geometry_for_test) {
        geometry = *capture_seams.ring_geometry_for_test;
      } else {
        auto sized = acquisition::ringGeometryForMiB(opts.ring_mib);
        if (!sized) {
          return std::unexpected("cannot size bulk ring - nothing was started: " + sized.error());
        }
        geometry = *sized;
      }
      auto ring = capture_seams.create_ring(geometry, capture_id);
      if (!ring || !*ring) {
        return std::unexpected(
            acquisition::couldNotSetUpRing(id, ring ? acquisition::ringNotCreatedMessage() : ring.error()));
      }
      // WHY: Declared before the summary so the summary and its thread are gone before this mutex is destroyed.
      std::mutex out_mutex;
      const auto say = [&out, &out_mutex](const std::string& line) {
        const std::lock_guard<std::mutex> lock(out_mutex);
        out << line << '\n' << std::flush;
      };
      auto summary = RingSummary::start(**ring, id, say, capture_seams.summary);
      if (!summary) {
        return std::unexpected("could not start live summary for '" + id +
                               "' - nothing was started: " + summary.error());
      }

      if (!settings->empty()) {
        // WHY: One setProperties for all settings, so a refusal never leaves the device with half of them applied.
        if (auto set = transport.setProperties(id, host::pipe::kLaAcquisitionFacetName, *settings);
            !set.has_value()) {
          return std::unexpected(acquisition::propertiesNotSet(id, set.error()));
        }
        say("set on '" + id + "': " + describeAnalyzerSettings(*settings, session));
      }

      request.capture_id = capture_id;
      request.bulk_ring_name = (*ring)->name();
      if (auto started = la->start(request); !started.has_value()) {
        return std::unexpected(acquisition::couldNotStartCapture(id, started.error()));
      }
      say("capture started on '" + id + "': " + std::to_string(opts.rate_hz) + " Hz, channel mask 0x" +
          host::maskToHex(opts.channel_mask) + (opts.buffered ? ", buffered" : ", streaming"));

      hold();

      auto stopped = la->stop();
      (void)(*summary)->finish(capture_seams.end_wait);
      if (!stopped.has_value()) {
        if (const auto gone = capture_seams.driver_closed ? capture_seams.driver_closed() : std::nullopt) {
          return std::unexpected("driver ended during capture on '" + id + "': " + *gone);
        }
        return std::unexpected(acquisition::mayStillBeRunning(id, stopped.error()));
      }
      out << "capture stopped" << std::endl;
      return {};
    }

    case Action::Stop: {
      auto la = acquisition::makeLaClient(session, transport);
      if (!la.has_value()) {
        return std::unexpected(la.error());
      }
      if (auto stopped = la->stop(); !stopped.has_value()) {
        return std::unexpected("could not stop capture on '" + id + "': " + stopped.error());
      }
      out << "stop sent to '" << id << "'" << std::endl;
      return {};
    }

    case Action::PwmStart: {
      auto siggen = acquisition::makeSiggenClient(session, transport);
      if (!siggen.has_value()) {
        return std::unexpected(siggen.error());
      }
      const auto frequency = detail::toMilliHz(opts.freq_hz, "PWM frequency");
      if (!frequency.has_value()) {
        return std::unexpected(frequency.error());
      }
      if (auto configured = siggen->configureSquare(opts.port_mask, host::pipe::SquareWave{*frequency, opts.duty_percent});
          !configured.has_value()) {
        return std::unexpected("could not configure PWM on '" + id + "': " + configured.error());
      }
      // WHY: Independent, not Synchronized: ports may start one after another on devices that cannot start together.
      if (auto started = siggen->start(opts.port_mask, host::pipe::StartMode::Independent);
          !started.has_value()) {
        return std::unexpected("PWM was configured on '" + id +
                                "' but could not be started (ports are still off): " +
                                started.error());
      }
      out << "PWM started on '" << id << "': ports 0x" << host::maskToHex(opts.port_mask) << ", "
          << opts.freq_hz << " Hz, " << opts.duty_percent << "% duty" << std::endl;
      return {};
    }

    case Action::PwmStop: {
      auto siggen = acquisition::makeSiggenClient(session, transport);
      if (!siggen.has_value()) {
        return std::unexpected(siggen.error());
      }
      if (auto stopped = siggen->stop(opts.port_mask); !stopped.has_value()) {
        return std::unexpected("could not stop PWM on '" + id + "': " + stopped.error());
      }
      out << "PWM stopped on '" << id << "': ports 0x" << host::maskToHex(opts.port_mask)
          << std::endl;
      return {};
    }

    case Action::Acquire:
      return std::unexpected("acquisition is run by runAcquire (cli_acquire.h), not runAction");

    case Action::Listen:
    case Action::Help:
    case Action::ListDevices:
    case Action::WatchDevices:
    case Action::ListProperties:
    case Action::CheckDriver:
      break;
  }
  return std::unexpected(
      "nothing to run: give --start-capture, --stop, --pwm-start or --pwm-stop");
}

}

/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <cstdint>
#include <expected>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "capture_allocator.h"
#include "capture_plan.h"
#include "capture_session.h"
#include "capture_writer.h"
#include "facet_access.h"
#include "facet_client.h"
#include "host_ring.h"
#include "ipc_la_acquisition.h"
#include "ipc_properties.h"
#include "plugin_device.h"
#include "progress.h"
#include "ring_config.h"
#include "trigger_spec.h"

namespace bitoscope::acquisition {

struct AcquireRequest {
  uint32_t channel_mask = 0;
  uint64_t sample_rate_milli_hz = 0;
  uint64_t total_samples = 0;
  std::optional<bitoscope::trigger::TriggerSpec> trigger;
  std::vector<ipc::props::PropertyValue> properties;
  std::map<std::string, std::vector<ipc::props::PropertyValue>> other_facet_properties;
  bitoscope::capture::AllocationPolicy memory_policy = bitoscope::capture::AllocationPolicy::Chunked;
  std::optional<uint64_t> memory_budget_bytes;
  std::optional<uint64_t> reserve_bytes;
  uint32_t pack_workers = 0;
  uint32_t clearing_workers = 1;
  uint64_t ring_mib = kDefaultRingMiB;
  uint32_t start_retries = 2;
  bool keep_on_memory_full = true;
  bool pyramid = false;
  bool live_tail = false;
};

inline constexpr uint64_t kProgressIntervalMs = 250;

struct AcquisitionSeams {
  bitoscope::capture::MemoryProbe& probe;
  bitoscope::capture::BlockSource& blocks;
  std::function<uint64_t()> now_ms{};
  std::function<void(uint64_t)> sleep_ms{};
  std::function<bool()> abandon_requested{};
  std::function<bool()> stop_requested{};
  std::function<void(std::shared_ptr<bitoscope::capture::CaptureSession>)> on_session{};
  uint64_t inactivity_timeout_ms = 5000;
  uint64_t poll_ms = 50;
  uint32_t samples_per_window = bitoscope::capture::kSamplesPerWindow;
  std::function<uint64_t()> new_capture_id = randomCaptureId;
  FacetVersion min_la_version = kMinLaAcquisitionVersionWithCaptureId;
  CreateRingFn create_ring = createHostRing;
  std::optional<RingGeometry> ring_geometry_for_test{};
  uint64_t arena_block_bytes_for_test = 0;
  bitoscope::capture::ClearingThreads* clearing_threads_for_test = nullptr;
  uint64_t progress_interval_ms = kProgressIntervalMs;
  std::function<std::optional<std::string>()> plugin_closed{};
  std::function<std::optional<std::string>()> device_lost{};
  std::function<std::optional<ipc::la::CaptureEnded>(uint64_t capture_id)> capture_ended{};
};

struct AcquireResult {
  bitoscope::capture::CaptureSummary summary;
  std::shared_ptr<bitoscope::capture::CaptureSession> session;
  std::optional<bitoscope::capture::MemoryShortfall> memory_shortfall;
  uint64_t requested_samples = 0;
  std::optional<uint64_t> requested_pre_trigger_samples;
  std::optional<uint64_t> requested_post_trigger_samples;
  std::string device;
  std::vector<ipc::props::PropertyValue> properties;
  std::map<std::string, std::vector<ipc::props::PropertyValue>> other_facet_properties;
};

struct AcquireWarning {
  enum class Kind {
    Memory,
    Floor,
  };
  Kind kind = Kind::Memory;
  std::string text;
  std::optional<uint64_t> floor_samples;
};

struct AcquireRetry {
  std::string lost;
  uint64_t old_capture_id = 0;
  uint64_t new_capture_id = 0;
  uint64_t attempt = 0;
  uint64_t attempts_allowed = 0;
  std::string device;
};

class AcquisitionEvents {
 public:
  virtual ~AcquisitionEvents() = default;

  virtual void planned([[maybe_unused]] uint32_t device, [[maybe_unused]] const bitoscope::capture::CapturePlan& plan,
                       [[maybe_unused]] const std::optional<bitoscope::trigger::TriggerSpec>& trigger) {}
  virtual void warning([[maybe_unused]] uint32_t device, [[maybe_unused]] const AcquireWarning& warning) {}
  virtual void propertiesSet([[maybe_unused]] uint32_t device, [[maybe_unused]] std::string_view plugin_id,
                             [[maybe_unused]] const std::vector<ipc::props::PropertyValue>& values) {}
  virtual void started([[maybe_unused]] uint32_t device, [[maybe_unused]] std::string_view plugin_id,
                       [[maybe_unused]] uint64_t capture_id) {}
  virtual bool wantsProgress() const { return false; }
  virtual void progress([[maybe_unused]] uint32_t device, [[maybe_unused]] uint64_t now_ms,
                        [[maybe_unused]] const Progress& progress) {}
  virtual void waitEnded([[maybe_unused]] uint32_t device) {}
  virtual void ringPeak([[maybe_unused]] uint32_t device, [[maybe_unused]] uint64_t peak_slots_in_use,
                        [[maybe_unused]] uint32_t slot_count) {}
  virtual void stopped([[maybe_unused]] uint32_t device) {}
  virtual void retrying([[maybe_unused]] uint32_t device, [[maybe_unused]] const AcquireRetry& retry) {}
  virtual void completed([[maybe_unused]] uint32_t device,
                         [[maybe_unused]] const bitoscope::capture::CaptureSummary& summary,
                         [[maybe_unused]] const bitoscope::capture::CapturePlan& plan,
                         [[maybe_unused]] const bitoscope::capture::CaptureSession& session,
                         [[maybe_unused]] const std::optional<bitoscope::capture::MemoryShortfall>& shortfall) {}
};

// WHY: Per-device memory fields and start_retries are ignored in a session: the session's own are used.
struct SessionDevice {
  const bitoscope::host::pipe::PluginDevice& device;
  bitoscope::host::FacetTransport& transport;
  AcquireRequest request;
  std::function<std::optional<std::string>()> plugin_closed;
  std::function<std::optional<std::string>()> device_lost;
  std::function<std::optional<ipc::la::CaptureEnded>(uint64_t capture_id)> capture_ended;
};

struct SessionRequest {
  bitoscope::capture::AllocationPolicy memory_policy = bitoscope::capture::AllocationPolicy::Chunked;
  std::optional<uint64_t> memory_budget_bytes;
  std::optional<uint64_t> reserve_bytes;
  bool keep_on_memory_full = true;
  uint32_t start_retries = 2;
};

struct SessionResult {
  std::vector<AcquireResult> devices;
};

struct MemoryCheck {
  enum class Answer {
    Fits,
    Warn,
  };
  Answer answer = Answer::Fits;
  std::optional<uint64_t> floor_samples;
  std::string text;
};

std::expected<MemoryCheck, std::string> checkMemoryBeforeStart(const AcquireRequest& request,
                                                               const SessionRequest& session,
                                                               const AcquisitionSeams& seams);

std::expected<AcquireResult, std::string> acquire(const AcquireRequest& request,
                                                  const bitoscope::host::pipe::PluginDevice& device,
                                                  bitoscope::host::FacetTransport& transport,
                                                  const AcquisitionSeams& seams, AcquisitionEvents& events);

std::expected<SessionResult, std::string> acquireSession(std::span<const SessionDevice> devices,
                                                         const SessionRequest& session, const AcquisitionSeams& seams,
                                                         AcquisitionEvents& events);

std::string formatCaptureId(uint64_t id);

namespace detail {

std::string describeEnd(bitoscope::capture::CaptureStatus end, uint64_t inactivity_timeout_ms,
                        std::optional<uint64_t> trigger_timeout_s = std::nullopt);

std::string describeEarlyEnd(const bitoscope::capture::CaptureSummary& summary,
                             const bitoscope::capture::CapturePlan& plan, uint64_t inactivity_timeout_ms,
                             std::optional<uint64_t> trigger_timeout_s = std::nullopt);

std::optional<std::string> describeUntriggeredMemory(uint64_t samples_floor,
                                                     const bitoscope::capture::CapturePlan& plan);

std::expected<std::optional<std::string>, std::string> describeTriggeredMemory(
    uint64_t samples_floor, const bitoscope::trigger::TriggerSpec& spec, const bitoscope::capture::CapturePlan& plan);

// WHY: describeShortfall returns an error, not an unset pair, when the
// count is below pre: never assume it cannot happen.
std::expected<bitoscope::capture::MemoryShortfall, std::string> describeShortfall(
    const bitoscope::capture::CaptureSummary& summary, const bitoscope::capture::CaptureSession& capture,
    const std::optional<bitoscope::trigger::TriggerSpec>& trigger);

}

}

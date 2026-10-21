/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "bulk_ring.h"
#include "decode_ring_producer.h"
#include "ipc_decoder.h"
#include "item_feed.h"
#include "pacing_policy.h"
#include "plugin_connection.h"
#include "plugin_link.h"
#include "stream.h"
#include "work_placement.h"

namespace bitoscope::decoder_host {

inline constexpr std::string_view kDecoderDeviceId = "decoders";

enum class AllocationSite {
  FeedPlanes,
  Annotations,
  Items,
};

enum class JobEvent {
  FeedDone,
  Ended,
};

enum class JobFailure {
  Refused,
  Failed,
  Cancelled,
  PluginGone,
  OutOfMemory,
};

struct JobError {
  JobFailure kind = JobFailure::Failed;
  std::string message;
  uint64_t samples_delivered = 0;
};

struct JobResult {
  std::vector<ipc::decoder::Annotation> annotations;
  std::map<std::string, std::vector<ipc::decoder::StreamItem>> items;
  uint64_t samples_read = 0;
  uint64_t samples_delivered = 0;
  uint64_t items_read = 0;
  uint64_t items_fed = 0;
};

using StopFlag = std::atomic<bool>;

enum class FeedFailure {
  Stopped,
  ConsumerClosed,
  Failed,
  OutOfMemory,
};

struct FeedError {
  FeedFailure kind = FeedFailure::Failed;
  std::string message;
};

using FeedFunction = std::function<std::expected<void, FeedError>(host::DecodeRingProducer& ring, const StopFlag& stop)>;

using AnnotationHandler = std::function<void(std::span<const ipc::decoder::Annotation> batch)>;

using SettledHandler = std::function<void(uint64_t watermark)>;

using ByteValuesHandler = std::function<void(std::span<const std::vector<uint8_t>> values)>;

using ItemHandler = std::function<void(std::string_view output, std::span<const ipc::decoder::StreamItem> batch)>;

using ItemInputs = std::map<std::string, decode::StreamInput*>;

inline constexpr uint64_t kMaxRingBytesPerChannel = uint64_t{64} << 20;
inline constexpr uint32_t kRingSlackSlots = 4;

struct DecodeJobOptions {
  uint32_t slot_count = 64;
  uint64_t slot_bytes_per_channel = 16 * 1024;
  uint64_t max_ring_bytes_per_channel = kMaxRingBytesPerChannel;
  uint32_t flush_after_ms = bitoscope::bulk_ring::kDefaultFlushAfterMs;
  uint64_t progress_step = 0;
  PacingPolicy pacing = defaultPacingPolicy();
  bool fill_slots = false;
  bitoscope::bulk_ring::Timing timing = {};
  const std::atomic<bool>* cancel = nullptr;
  uint32_t item_feed_batch = kItemFeedBatch;
  ByteValuesHandler on_byte_values;
  std::function<void(std::string_view input)> before_item_feed;
  std::chrono::milliseconds cancel_grace{2000};
  std::function<void(AllocationSite site, uint64_t bytes)> before_allocate;
  std::function<void(JobEvent event)> on_job_event;
};

struct WindowWarning {
  uint64_t window_samples = 0;
  uint64_t floor_samples = 0;
  uint64_t ring_bytes_per_channel = 0;
  std::string text() const;
  bool operator==(const WindowWarning&) const = default;
};

struct SizedJob {
  DecodeJobOptions job;
  std::optional<WindowWarning> warning;
};

SizedJob sizedFor(const DecodeJobOptions& base, uint64_t look_ahead_samples);

struct ClientOptions {
  std::chrono::milliseconds request_timeout = host::kDefaultRequestTimeout;
  std::optional<std::chrono::milliseconds> check_timeout;
  std::function<void(std::string_view line)> on_log;
  std::vector<std::string> arguments;
  cpu::WorkPlacement placement;
};

class DecoderPluginClient {
 public:
  static std::expected<std::shared_ptr<DecoderPluginClient>, std::string> connect(std::unique_ptr<host::PluginLink> link,
                                                                                  ClientOptions options = {});
  static std::expected<std::shared_ptr<DecoderPluginClient>, std::string> start(const std::filesystem::path& executable,
                                                                                ClientOptions options = {});

  ~DecoderPluginClient();
  DecoderPluginClient(const DecoderPluginClient&) = delete;
  DecoderPluginClient& operator=(const DecoderPluginClient&) = delete;

  const std::string& name() const;
  const ipc::decoder::DecoderFacetInfo& info() const { return info_; }
  const ipc::decoder::DecoderInfo* decoder(std::string_view name) const;
  std::optional<std::string> closedReason() const;
  uint64_t processId() const { return process_id_; }

  std::expected<void, std::string> check(const ipc::decoder::JobSetup& setup);
  std::expected<ipc::decoder::CheckAnswer, std::string> checkJob(const ipc::decoder::JobSetup& setup);

  std::expected<JobResult, JobError> decode(const ipc::decoder::JobSetup& setup, FeedFunction feed,
                                            const DecodeJobOptions& options = {}, AnnotationHandler on_annotations = {},
                                            ItemHandler on_items = {}, SettledHandler on_settled = {});

  std::expected<JobResult, JobError> decodeItems(const ipc::decoder::JobSetup& setup, const ItemInputs& inputs,
                                                 const DecodeJobOptions& options = {}, AnnotationHandler on_annotations = {},
                                                 ItemHandler on_items = {}, SettledHandler on_settled = {});

  std::expected<JobResult, JobError> decodeMixed(const ipc::decoder::JobSetup& setup, FeedFunction feed,
                                                 const ItemInputs& inputs, const DecodeJobOptions& options = {},
                                                 AnnotationHandler on_annotations = {}, ItemHandler on_items = {},
                                                 SettledHandler on_settled = {});

 private:
  struct Job;

  explicit DecoderPluginClient(ClientOptions options);
  void onFacetEvent(const ipc::FacetEvent& event);
  void onClosed(std::string_view reason);
  void log(std::string_view line) const;
  uint64_t nextJobId();
  std::expected<void, host::RequestError> send(const ipc::decoder::DecoderCommand& command);
  std::expected<JobResult, JobError> runJob(const ipc::decoder::JobSetup& setup, FeedFunction feed,
                                            const ItemInputs* inputs, const DecodeJobOptions& options,
                                            AnnotationHandler on_annotations, ItemHandler on_items,
                                            SettledHandler on_settled);

  ClientOptions options_;
  std::unique_ptr<host::PluginConnection> connection_;
  ipc::decoder::DecoderFacetInfo info_;
  uint64_t process_id_ = 0;

  mutable std::mutex mutex_;
  std::condition_variable cv_;
  std::map<uint64_t, Job*> jobs_;
  std::optional<std::string> closed_;
  uint64_t next_job_id_ = 1;
};

}

/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <condition_variable>
#include <cstdint>
#include <deque>
#include <expected>
#include <memory>
#include <mutex>
#include <span>
#include <string>
#include <thread>

#include "decode_job.h"
#include "decoder_model.h"
#include "ipc_decoder.h"
#include "plugin_model.h"
#include "stream.h"

namespace bitoscope::plugin_c_sdk {

inline constexpr uint64_t kQueuedHeartbeatMs = 500;

class DecoderRuntime final : public FacetHandler {
 public:
  explicit DecoderRuntime(const DecoderFacetRegistration& facet);
  ~DecoderRuntime() override;
  DecoderRuntime(const DecoderRuntime&) = delete;
  DecoderRuntime& operator=(const DecoderRuntime&) = delete;

  FacetAnswer handle(std::span<const uint8_t> payload) override;

  void abandon() override;

 private:
  struct Job;
  class RemoteItems;

  std::expected<std::unique_ptr<Job>, std::string> prepare(uint64_t id, const ipc::decoder::JobSetup& setup);
  ipc::FacetReply check(uint64_t id, const ipc::decoder::JobSetup& setup);
  ipc::FacetReply start(uint64_t id, const ipc::decoder::DecodeStart& start);
  ipc::FacetReply cancel(uint64_t id);
  ipc::FacetReply feed(uint64_t id, const ipc::decoder::ItemFeed& feed);
  ipc::FacetReply queue(std::unique_ptr<Job> job);
  void work();
  void keep();
  void run(Job& job);

  const DecoderFacetRegistration& facet_;
  std::mutex mutex_;
  std::condition_variable cv_;
  std::deque<std::unique_ptr<Job>> queue_;
  Job* running_ = nullptr;
  bool stopping_ = false;
  std::thread worker_;
  std::thread keeper_;
};

}

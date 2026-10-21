/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "capture_extent.h"
#include "capture_writer.h"
#include "decoder.h"
#include "decoder_plugin_client.h"
#include "decoder_plugin_pool.h"
#include "decoder_registry.h"
#include "ipc_decoder.h"
#include "raw_capture_buffer.h"

namespace bitoscope::decoder_host {

struct PluginDecoderOptions {
  DecodeJobOptions job;
  uint64_t min_skip_samples = 4096;
  std::function<void(const StopFlag& stop)> before_feed;
  std::function<void(DecoderPluginClient& client)> on_job_client{};
};

class PluginDecoder final : public decode::Decoder {
 public:
  PluginDecoder(std::shared_ptr<DecoderPluginClient> client, ipc::decoder::DecoderInfo info,
                PluginDecoderOptions options = {});
  PluginDecoder(std::shared_ptr<DecoderPluginPool> pool, ipc::decoder::DecoderInfo info,
                PluginDecoderOptions options = {});
  PluginDecoder(std::nullptr_t, ipc::decoder::DecoderInfo info, PluginDecoderOptions options = {});

  std::string name() const override;
  std::string display() const override;
  std::vector<decode::ChannelSpec> channels() const override;
  std::vector<decode::OptionSpec> options() const override;
  std::vector<std::string> unsupportedOptions() const override;
  std::vector<std::string> protocols() const override;
  bool allowsSharedChannels() const override;
  std::vector<decode::AnnotationClass> annotationClasses() const override;
  std::vector<decode::AnnotationRow> rows() const override;
  std::vector<decode::RowSource> rowSources() const override;
  std::vector<decode::OutputSpec> outputs() const override;
  std::vector<decode::InputSpec> inputs() const override;

  const ipc::decoder::DecoderInfo& info() const { return info_; }
  const DecoderPluginClient& client() const { return *client_; }
  const DecoderPluginPool* pool() const { return pool_.get(); }
  std::string pluginName() const;
  std::string pluginVersion() const;

  void setCancel(const StopFlag* cancel, std::chrono::milliseconds grace);

  const std::optional<WindowWarning>& windowWarning() const { return window_warning_; }
  std::optional<std::string> windowWarningText() const;

  std::expected<void, std::string> decodeCapture(const capture::RawCaptureBuffer& buffer, capture::CaptureExtent extent,
                                                 capture::ExtentTail tail, decode::AnnotationSink& sink,
                                                 decode::StreamOutputs* outputs = nullptr);

  std::expected<void, std::string> decodeCaptureMixed(const capture::RawCaptureBuffer& buffer,
                                                      capture::CaptureExtent extent, capture::ExtentTail tail,
                                                      decode::StreamInputs& inputs, decode::AnnotationSink& sink,
                                                      decode::StreamOutputs* outputs = nullptr);

  ipc::decoder::JobSetup jobSetup(const decode::CaptureFacts& facts, std::vector<std::string> wanted = {}) const;

 protected:
  std::expected<void, std::string> checkCaptureFacts(const decode::CaptureFacts& facts) const override;
  std::expected<void, std::string> run(const decode::SignalView& view, decode::AnnotationSink& sink) override;
  std::expected<void, std::string> runStreams(decode::AnnotationSink& sink) override;
  std::expected<void, std::string> runMixed(const decode::SignalView& view, decode::AnnotationSink& sink) override;

 private:
  std::expected<FeedFunction, std::string> feedOf(const decode::SignalView& view,
                                                  const ipc::decoder::JobSetup& setup) const;
  std::vector<std::string> wantedOutputs();
  std::vector<uint32_t> bytesClasses() const;
  std::expected<void, std::string> checkSinkTakesBytes(const decode::AnnotationSink& sink) const;
  ItemHandler itemsOnward();

  struct JobClient {
    DecoderPluginClient* client = nullptr;
    DecoderPluginPool::Lease lease;
  };
  std::expected<JobClient, std::string> clientForJob();

  struct CaptureSource {
    const decode::SignalView* view = nullptr;
    const capture::RawCaptureBuffer* buffer = nullptr;
    capture::CaptureExtent extent;
    capture::ExtentTail tail = capture::ExtentTail::Exact;
  };

  std::shared_ptr<DecoderPluginClient> client_;
  std::shared_ptr<DecoderPluginPool> pool_;
  ipc::decoder::DecoderInfo info_;
  PluginDecoderOptions options_;
  CaptureSource capture_;
  // WHY: look_ahead_samples_ and facts_ come from the last check (decode() checks first); mutable as checks are const.
  mutable uint64_t look_ahead_samples_ = 0;
  mutable std::optional<WindowWarning> window_warning_;
  mutable decode::CaptureFacts facts_;
};

capture::DecoderVersions savedVersionsOf(const decode::Decoder& decoder);

std::expected<void, std::string> addPluginDecoders(decode::DecoderRegistry& registry,
                                                   std::shared_ptr<DecoderPluginClient> client,
                                                   PluginDecoderOptions options = {});
std::expected<void, std::string> addPluginDecoders(decode::DecoderRegistry& registry,
                                                   std::shared_ptr<DecoderPluginPool> pool,
                                                   PluginDecoderOptions options = {});

}

/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <cstdint>
#include <expected>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "capture_extent.h"
#include "decoder.h"
#include "decoder_plugin_client.h"
#include "growing_capture.h"
#include "raw_capture_buffer.h"

namespace bitoscope::decoder_host {

inline constexpr uint64_t kFeedSliceMs = 50;

struct CaptureFeedOptions {
  uint64_t min_skip_samples = 4096;
  std::function<void(AllocationSite site, uint64_t bytes)> before_allocate;
  std::function<void(const StopFlag& stop)> before_feed;
};

std::expected<FeedFunction, std::string> captureFeed(const capture::RawCaptureBuffer& buffer, capture::CaptureExtent extent,
                                                     capture::ExtentTail tail, std::vector<uint32_t> channels,
                                                     CaptureFeedOptions options = {});

std::expected<FeedFunction, std::string> growingCaptureFeed(std::shared_ptr<const GrowingCapture> capture,
                                                            std::shared_ptr<const CaptureEnd> end,
                                                            std::vector<uint32_t> channels,
                                                            CaptureFeedOptions options = {});

FeedFunction viewFeed(const decode::SignalView& view, std::vector<uint32_t> channels,
                      std::function<void(AllocationSite site, uint64_t bytes)> before_allocate = {});

}

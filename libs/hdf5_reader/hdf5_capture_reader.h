/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <cstdint>
#include <expected>
#include <filesystem>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "capture_allocator.h"
#include "capture_writer.h"
#include "memory_probe.h"
#include "pack_pool.h"
#include "raw_capture_buffer.h"

namespace bitoscope::hdf5 {

struct LoadedDeviceMetadata {
  std::string device;
  int64_t alignment_offset_ps = 0;

  std::vector<bitoscope::capture::SavedChannel> channels;
  std::vector<std::pair<std::string, std::string>> decoder_setup;
  std::vector<std::string> decoder_presentation;
  std::vector<bitoscope::capture::DecoderVersions> decoder_versions;
  std::vector<bitoscope::capture::SavedProperty> device_properties;

  uint64_t sample_rate_milli_hz = 0;
  uint64_t capture_id = 0;

  uint64_t stored_samples = 0;
  uint64_t requested_samples = 0;
  std::optional<uint64_t> requested_pre_trigger_samples;
  std::optional<uint64_t> requested_post_trigger_samples;
  std::optional<uint64_t> stored_post_trigger_samples;

  std::optional<uint64_t> origin_sample_index;
  std::optional<uint64_t> trigger_sample_index;

  std::optional<std::string> short_reason;
  std::optional<uint32_t> memory_full_device;

  std::optional<bitoscope::capture::ViewState> view;
  std::vector<std::string> warnings;
};

struct LoadedDevice {
  bitoscope::capture::RawCaptureBuffer buffer;
  LoadedDeviceMetadata metadata;
};

struct LoadedSession {
  uint32_t format_version = 0;
  bool alignment_locked = false;
  std::optional<std::string> short_reason;
  std::vector<LoadedDevice> devices;
};

struct Hdf5ReaderOptions {
  bool build_pyramid = true;
  bitoscope::capture::MemoryProbe* probe = nullptr;
  bitoscope::capture::BlockSource* blocks = nullptr;
  std::optional<uint64_t> reserve_bytes;
  uint64_t arena_block_bytes = 0;
  std::optional<uint32_t> clearing_workers;
  bitoscope::capture::ClearingThreads* clearing_threads = nullptr;
  std::optional<uint32_t> pack_workers;
  bitoscope::capture::PackPoolHooks pack_hooks_for_test;
};

std::expected<LoadedSession, std::string> loadSession(const std::filesystem::path& path,
                                                       const Hdf5ReaderOptions& options = {});

}

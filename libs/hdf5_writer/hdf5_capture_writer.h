/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <cstdint>
#include <expected>
#include <filesystem>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "capture_writer.h"

namespace bitoscope::hdf5 {

struct Hdf5WriterOptions {
  uint32_t gzip_level = 6;
  uint64_t chunk_bytes = 128 * 1024;
  uint32_t deflate_workers = 0;
  std::function<std::expected<void, std::string>(uint32_t channel)> after_channel;
  std::function<std::expected<void, std::string>(uint64_t chunk)> before_deflate;
};

struct SessionDeviceCapture {
  const bitoscope::capture::RawCaptureBuffer& buffer;
  const bitoscope::capture::CaptureMetadata& metadata;
  int64_t alignment_offset_ps = 0;
};

struct SessionInfo {
  bool alignment_locked = false;
};

class Hdf5CaptureWriter final : public bitoscope::capture::CaptureWriter {
 public:
  explicit Hdf5CaptureWriter(std::filesystem::path path, Hdf5WriterOptions options = {})
      : path_(std::move(path)), options_(std::move(options)) {}

  std::expected<void, std::string> write(const bitoscope::capture::RawCaptureBuffer& buffer,
                                         const bitoscope::capture::CaptureMetadata& metadata) override;

  std::expected<void, std::string> writeSession(std::span<const SessionDeviceCapture> devices,
                                                const SessionInfo& session);

  const std::filesystem::path& path() const { return path_; }

 private:
  std::filesystem::path path_;
  Hdf5WriterOptions options_;
};

std::expected<void, std::string> updateSessionView(const std::filesystem::path& path,
                                                    uint32_t device_index,
                                                    const bitoscope::capture::ViewState& view);

std::expected<void, std::string> updateSessionDecoders(
    const std::filesystem::path& path, uint32_t device_index,
    std::span<const std::pair<std::string, std::string>> decoder_setup, const Hdf5WriterOptions& options = {});

std::expected<void, std::string> updateSessionDecoders(
    const std::filesystem::path& path, uint32_t device_index,
    std::span<const std::pair<std::string, std::string>> decoder_setup,
    std::span<const std::string> decoder_presentation, const Hdf5WriterOptions& options = {});

std::expected<void, std::string> updateSessionDecoders(
    const std::filesystem::path& path, uint32_t device_index,
    std::span<const std::pair<std::string, std::string>> decoder_setup,
    std::span<const std::string> decoder_presentation,
    std::span<const bitoscope::capture::DecoderVersions> decoder_versions, const Hdf5WriterOptions& options = {});

}

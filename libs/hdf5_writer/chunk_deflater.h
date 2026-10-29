/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <functional>
#include <memory>
#include <mutex>
#include <span>
#include <string>
#include <thread>
#include <vector>

namespace bitoscope::hdf5 {

class ChunkDeflater {
 public:
  using BeforeDeflate = std::function<std::expected<void, std::string>(uint64_t chunk)>;
  using Sink = std::function<std::expected<void, std::string>(uint64_t chunk, std::span<const uint8_t> deflated)>;

  static std::expected<std::unique_ptr<ChunkDeflater>, std::string> create(uint32_t workers, uint32_t level,
                                                                           size_t chunk_bytes,
                                                                           BeforeDeflate before_deflate);

  ~ChunkDeflater();

  ChunkDeflater(const ChunkDeflater&) = delete;
  ChunkDeflater& operator=(const ChunkDeflater&) = delete;

  std::expected<std::span<uint8_t>, std::string> reserve(const Sink& sink);

  void submit(size_t valid_bytes);

  std::expected<void, std::string> finish(const Sink& sink);

 private:
  struct Slot {
    std::vector<uint8_t> raw;
    std::vector<uint8_t> deflated;
    size_t deflated_size = 0;
    bool done = false;
    std::string error;
  };

  ChunkDeflater(uint32_t level, size_t chunk_bytes, BeforeDeflate before_deflate);

  void workerLoop();
  void deflateSlot(uint64_t chunk, Slot& slot);
  std::expected<void, std::string> drainOldest(const Sink& sink);

  uint32_t level_;
  size_t chunk_bytes_;
  BeforeDeflate before_deflate_;
  std::vector<Slot> slots_;
  std::vector<std::thread> threads_;
  std::mutex mutex_;
  std::condition_variable work_;
  std::condition_variable done_;
  uint64_t submitted_ = 0;
  uint64_t taken_ = 0;
  uint64_t consumed_ = 0;
  bool stopping_ = false;
};

}

/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <cstdint>
#include <expected>
#include <functional>
#include <map>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "bitoscope_decoder_cpp.h"
#include "bitoscope_decoder_testing.h"

namespace bitoscope::plugin {

class MemoryInput {
 public:
  MemoryInput(std::span<const std::span<const uint8_t>> planes, uint64_t numSamples,
              std::span<const bitoscope_memory_record> records = {});
  MemoryInput(const MemoryInput&) = delete;
  MemoryInput& operator=(const MemoryInput&) = delete;
  ~MemoryInput();

  bool addItems(const std::vector<std::vector<StreamItem>>& inputs);

  void setProgressStep(uint64_t step) { (void)bitoscope_memory_set_progress_step(in_, step); }

  DecodeInput input() const { return DecodeInput(in_); }
  bitoscope_decode_input* get() const { return in_; }

 private:
  bitoscope_decode_input* in_ = nullptr;
};

class MemoryItemInput {
 public:
  MemoryItemInput(uint64_t totalSamples, const std::vector<std::vector<StreamItem>>& inputs);
  MemoryItemInput(const MemoryItemInput&) = delete;
  MemoryItemInput& operator=(const MemoryItemInput&) = delete;
  ~MemoryItemInput();

  DecodeInput input() const { return DecodeInput(in_); }
  bitoscope_decode_input* get() const { return in_; }

 private:
  bitoscope_decode_input* in_ = nullptr;
};

class ScanCounter {
 public:
  explicit ScanCounter(bitoscope_decode_input* in) : in_(in) { bitoscope_test_count_scans(in_, &words_); }
  ScanCounter(const ScanCounter&) = delete;
  ScanCounter& operator=(const ScanCounter&) = delete;
  ~ScanCounter() { bitoscope_test_count_scans(in_, nullptr); }

  uint64_t words() const { return words_; }

 private:
  bitoscope_decode_input* in_;
  uint64_t words_ = 0;
};

class CollectedAnnotations {
 public:
  explicit CollectedAnnotations(MemoryInput& input);
  CollectedAnnotations(MemoryInput& input, const DecodeJob& job);
  CollectedAnnotations(const CollectedAnnotations&) = delete;
  CollectedAnnotations& operator=(const CollectedAnnotations&) = delete;
  ~CollectedAnnotations();

  Annotations output() const { return Annotations(out_); }
  bool finish() { return bitoscope_finish_annotations(out_) == BITOSCOPE_OK; }
  std::vector<bitoscope_annotation> all() const;
  std::vector<bitoscope_settled> settles() const;
  std::vector<StreamItem> items(std::string_view output) const;
  bitoscope_annotations* get() const { return out_; }

 private:
  bitoscope_annotations* out_ = nullptr;
  std::vector<std::string> outputs_;
};

// IMPLEMENT: The capture a test decodes (bitoscope_test_capture).
struct TestCapture {
  std::vector<std::span<const uint8_t>> planes;
  uint64_t samples = 0;
  uint64_t sampleRateMilliHz = 0;
  std::vector<bitoscope_memory_record> records{};
};

// IMPLEMENT: One job, as a user asks for it (bitoscope_test_job).
struct TestJob {
  struct Input {
    std::string name;
    StreamType type = StreamType::words;
    uint32_t width = 0;
    std::vector<StreamItem> items;
  };
  std::string decoder;
  std::vector<std::pair<std::string, uint32_t>> channels;
  std::map<std::string, std::string, std::less<>> options{};
  std::vector<Input> inputs{};
  std::vector<std::string> outputs{};
  uint64_t progressStep = 0;
  bool asEmitted = false;
};

struct TestDecode {
  std::vector<bitoscope_annotation> annotations;
  std::map<std::string, std::vector<StreamItem>, std::less<>> items;
  std::vector<bitoscope_settled> settles;
  std::vector<std::vector<uint8_t>> bytes;
  uint64_t lookAheadSamples = 0;
};

class TestPlugin {
 public:
  TestPlugin(bitoscope_list_decoders_fn list, void* user);
  explicit TestPlugin(DecoderListFunction list);
  TestPlugin(const TestPlugin&) = delete;
  TestPlugin& operator=(const TestPlugin&) = delete;
  ~TestPlugin();

  std::string error() const;
  std::expected<TestDecode, std::string> decode(const TestCapture& capture, const TestJob& job) const;
  const bitoscope_test_plugin* get() const { return loaded_; }

 private:
  std::unique_ptr<DecoderPlugin> plugin_;
  bitoscope_test_plugin* loaded_ = nullptr;
};

}

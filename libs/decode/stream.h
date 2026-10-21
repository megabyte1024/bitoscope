/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace bitoscope::decode {

struct StreamItem {
  uint64_t begin = 0;
  uint64_t end = 0;
  uint64_t value = 0;
  uint32_t kind = 0;
  uint32_t flags = 0;
  bool operator==(const StreamItem&) const = default;
};
static_assert(sizeof(StreamItem) == 32, "a stream item is 32 bytes");

enum class StreamType { Words, SpiTransfers };

inline constexpr uint32_t kKindData = 0;
inline constexpr uint32_t kKindBoundary = 1;
inline constexpr uint32_t kKindBegin = 2;

inline constexpr uint32_t kFlagDamaged = 1u << 0;
inline constexpr uint32_t kFlagNoMosi = 1u << 1;
inline constexpr uint32_t kFlagNoMiso = 1u << 2;

std::string_view streamTypeName(StreamType type);

bool kindBelongs(StreamType type, uint32_t kind);

struct OutputSpec {
  std::string name;
  std::string description;
  StreamType type = StreamType::Words;
  uint32_t width = 0;
  std::string width_option{};
  std::string requires_channel{};
  std::string display{};
  std::optional<std::string> carries = std::nullopt;
  std::optional<std::string> carries_input = std::nullopt;
  bool operator==(const OutputSpec&) const = default;
};

struct InputSpec {
  std::string name;
  std::string description;
  StreamType type = StreamType::Words;
  uint32_t width = 0;
  bool required = true;
  std::string display{};
  bool operator==(const InputSpec&) const = default;
};

class StreamSink {
 public:
  virtual ~StreamSink() = default;
  virtual void add(uint32_t output, std::span<const StreamItem> batch) = 0;
};

template <std::size_t N = 64>
class StreamBatch {
 public:
  StreamBatch(StreamSink& sink, uint32_t output) : sink_(sink), output_(output) {}
  ~StreamBatch() { flush(); }
  StreamBatch(const StreamBatch&) = delete;
  StreamBatch& operator=(const StreamBatch&) = delete;

  void put(const StreamItem& item) {
    buffer_[count_++] = item;
    if (count_ == N) flush();
  }
  void flush() {
    if (count_ == 0) return;
    sink_.add(output_, std::span<const StreamItem>(buffer_.data(), count_));
    count_ = 0;
  }
  // WHY: discard() is for an abandoned decode: the destructor's flush must not reach the sink.
  void discard() { count_ = 0; }

 private:
  StreamSink& sink_;
  uint32_t output_;
  std::array<StreamItem, N> buffer_{};
  std::size_t count_ = 0;
};

using StreamWriter = StreamBatch<64>;

class StreamInput {
 public:
  virtual ~StreamInput() = default;
  virtual std::optional<StreamItem> next() = 0;
};

class VectorStreamInput final : public StreamInput {
 public:
  explicit VectorStreamInput(std::span<const StreamItem> items);
  std::optional<StreamItem> next() override;
  std::size_t itemsRead() const { return read_; }

 private:
  std::span<const StreamItem> items_;
  std::size_t read_ = 0;
};

using StreamInputs = std::map<std::string, StreamInput*>;

struct GuardedOutput {
  std::string name;
  StreamType type = StreamType::Words;
  uint32_t width = 0;
  bool wanted = false;
};

class GuardedStreamSink final : public StreamSink {
 public:
  GuardedStreamSink(StreamSink& out, uint64_t sample_count, std::vector<GuardedOutput> outputs);
  void add(uint32_t output, std::span<const StreamItem> batch) override;
  const std::string& error() const { return error_; }

 private:
  std::string check(uint32_t output, const StreamItem& item) const;

  StreamSink& out_;
  uint64_t sample_count_;
  std::vector<GuardedOutput> outputs_;
  std::vector<uint64_t> last_begin_;
  std::vector<bool> seen_;
  std::string error_;
};

std::expected<void, std::string> checkBinding(std::string_view reader, const InputSpec& input,
                                              std::string_view source, const OutputSpec& output,
                                              uint32_t width, std::string_view width_note);

}

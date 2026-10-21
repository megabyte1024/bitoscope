/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <cstdint>
#include <cstddef>
#include <expected>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include "ipc_decode_error.h"
#include "ipc_messages.h"
#include "ipc_properties.h"

namespace bitoscope::ipc::decoder {

inline constexpr std::string_view kFacetName = "decoder";

struct ChannelSpec {
  std::string name;
  std::string description;
  bool required = false;
  std::string display{};
  bool operator==(const ChannelSpec&) const = default;
};

enum class OptionType : uint16_t { unspecified = 0, integer = 1, string = 2 };

using Limit = props::Bound;
using props::Unbounded;
using props::UnknownMember;

struct OptionSpec {
  std::string name;
  std::string description;
  OptionType type = OptionType::unspecified;
  std::string defaultValue;
  std::vector<std::string> allowedValues;
  Limit min;
  Limit max;
  std::vector<std::string> unsupportedValues;
  std::string display{};
  bool operator==(const OptionSpec&) const = default;
};

struct NoValue {
  bool operator==(const NoValue&) const = default;
};
struct HexValue {
  uint32_t digits = 0;
  std::string digitsOption;
  bool operator==(const HexValue&) const = default;
};
struct Address7Value {
  bool operator==(const Address7Value&) const = default;
};
struct NOfMValue {
  bool operator==(const NOfMValue&) const = default;
};
struct DecimalValue {
  bool operator==(const DecimalValue&) const = default;
};
struct NOfMBytesValue {
  bool operator==(const NOfMBytesValue&) const = default;
};
struct BytesValue {
  bool operator==(const BytesValue&) const = default;
};
using ValueFormat = std::variant<NoValue, HexValue, Address7Value, NOfMValue, DecimalValue, NOfMBytesValue, BytesValue,
                                 UnknownMember>;

enum class StreamType : uint16_t { unspecified = 0, words = 1, spiTransfers = 2 };

struct OutputSpec {
  std::string name;
  std::string description;
  StreamType type = StreamType::unspecified;
  uint32_t width = 0;
  std::string widthOption;
  std::string requiresChannel;
  std::string display{};
  std::string carries{};
  std::string carriesInput{};
  bool operator==(const OutputSpec&) const = default;
};

struct InputSpec {
  std::string name;
  std::string description;
  StreamType type = StreamType::unspecified;
  uint32_t width = 0;
  bool required = false;
  std::string display{};
  bool operator==(const InputSpec&) const = default;
};

// WHY: Unlike other enums here an unknown role is read as none, not kept: it only picks a colour.
enum class AnnotationRole : uint16_t {
  none = 0,
  data = 1,
  start = 2,
  stop = 3,
  ok = 4,
  error = 5,
  control = 6,
  address = 7,
  bit = 8,
  info = 9,
};

struct AnnotationClass {
  uint32_t id = 0;
  std::string name;
  ValueFormat format;
  AnnotationRole role = AnnotationRole::none;
  bool operator==(const AnnotationClass&) const = default;
};

struct Row {
  uint32_t id = 0;
  std::string name;
  bool operator==(const Row&) const = default;
};

struct SourceRef {
  uint32_t row = 0;
  std::vector<uint32_t> classes;
  bool operator==(const SourceRef&) const = default;
};
struct NoRowRule {
  bool operator==(const NoRowRule&) const = default;
};
struct WordListRule {
  bool operator==(const WordListRule&) const = default;
};
struct BitGridRule {
  std::string rateOption;
  uint32_t offsetBits = 0;
  std::string countOption;
  std::string orderOption;
  bool operator==(const BitGridRule&) const = default;
};
struct ClockEdgesRule {
  std::string clockChannel;
  std::string dataChannel;
  std::string countOption;
  uint32_t count = 0;
  bool operator==(const ClockEdgesRule&) const = default;
};
using RowRule = std::variant<NoRowRule, WordListRule, BitGridRule, ClockEdgesRule>;

struct RowSource {
  uint32_t row = 0;
  uint32_t cls = 0;
  SourceRef source;
  std::string enableOption;
  RowRule rule;
  bool operator==(const RowSource&) const = default;
};

struct DecoderInfo {
  std::string name;
  std::string version;
  std::vector<ChannelSpec> channels;
  std::vector<OptionSpec> options;
  std::vector<std::string> unsupportedOptions;
  std::vector<AnnotationClass> annotationClasses;
  std::vector<Row> rows;
  bool allowsSharedChannels = false;
  std::vector<OutputSpec> outputs{};
  std::vector<InputSpec> inputs{};
  std::string display{};
  std::vector<std::string> protocols{};
  std::vector<RowSource> rowSources{};
  bool operator==(const DecoderInfo&) const = default;
};

struct DecoderFacetInfo {
  std::vector<DecoderInfo> decoders;
  std::string pluginVersion{};
  bool runsScripts = false;
  bool operator==(const DecoderFacetInfo&) const = default;
};

struct ChannelBinding {
  std::string name;
  uint32_t channel = 0;
  bool operator==(const ChannelBinding&) const = default;
};

struct OptionValue {
  std::string name;
  std::string value;
  bool operator==(const OptionValue&) const = default;
};

struct CaptureFacts {
  uint64_t sampleRateMilliHz = 0;
  uint32_t channelCount = 0;
  uint64_t totalSamples = 0;
  bool operator==(const CaptureFacts&) const = default;
};

struct InputBinding {
  std::string name;
  StreamType type = StreamType::unspecified;
  uint32_t width = 0;
  bool operator==(const InputBinding&) const = default;
};

struct JobSetup {
  std::string decoder;
  std::vector<ChannelBinding> channels;
  std::vector<OptionValue> options;
  CaptureFacts facts;
  std::vector<std::string> outputs{};
  std::vector<InputBinding> inputs{};
  uint64_t progressStep = 0;
  bool operator==(const JobSetup&) const = default;
};

struct RingGeometry {
  uint32_t slotCount = 0;
  uint64_t slotPayloadBytes = 0;
  bool operator==(const RingGeometry&) const = default;
};

struct CheckJob {
  JobSetup setup;
  bool operator==(const CheckJob&) const = default;
};

struct DecodeStart {
  JobSetup setup;
  std::string ringName;
  RingGeometry ring;
  bool operator==(const DecodeStart&) const = default;
};

struct CancelJob {
  bool operator==(const CancelJob&) const = default;
};

struct StreamItem {
  uint64_t begin = 0;
  uint64_t end = 0;
  uint64_t value = 0;
  uint32_t kind = 0;
  uint32_t flags = 0;
  bool operator==(const StreamItem&) const = default;
};

struct ItemFeed {
  std::string input;
  std::vector<StreamItem> items;
  bool last = false;
  bool operator==(const ItemFeed&) const = default;
};

struct DecoderCommand {
  uint64_t jobId = 0;
  std::variant<CheckJob, DecodeStart, CancelJob, ItemFeed> action;
  bool operator==(const DecoderCommand&) const = default;
};

struct Annotation {
  uint64_t begin = 0;
  uint64_t end = 0;
  uint32_t row = 0;
  uint32_t cls = 0;
  uint64_t value = 0;
  bool operator==(const Annotation&) const = default;
};

struct AnnotationBatch {
  std::vector<Annotation> annotations;
  bool operator==(const AnnotationBatch&) const = default;
};

struct ByteValues {
  std::vector<std::vector<uint8_t>> values;
  bool operator==(const ByteValues&) const = default;
};

struct ItemBatch {
  std::string output;
  std::vector<StreamItem> items;
  bool operator==(const ItemBatch&) const = default;
};

struct ItemsWanted {
  std::string input;
  bool operator==(const ItemsWanted&) const = default;
};

enum class DecodeStatus : uint16_t { unspecified = 0, completed = 1, failure = 2, cancelled = 3 };

struct DecodeEnded {
  DecodeStatus status = DecodeStatus::unspecified;
  std::string error;
  uint64_t annotationCount = 0;
  uint64_t samplesRead = 0;
  uint64_t itemCount = 0;
  uint64_t itemsRead = 0;
  bool operator==(const DecodeEnded&) const = default;
};

struct DecoderEvent {
  uint64_t jobId = 0;
  std::variant<AnnotationBatch, DecodeEnded, ItemBatch, ItemsWanted, ByteValues> body;
  uint64_t settled = 0;
  bool operator==(const DecoderEvent&) const = default;
};

struct CheckAnswer {
  uint64_t lookAheadSamples = 0;
  bool operator==(const CheckAnswer&) const = default;
};

Bytes encodeCheckAnswer(const CheckAnswer& answer);
std::expected<CheckAnswer, std::string> decodeCheckAnswer(std::span<const uint8_t> bytes);

std::expected<Bytes, std::string> encodeInfo(const DecoderFacetInfo& info);
std::expected<Bytes, std::string> encodeCommand(const DecoderCommand& command);
std::expected<Bytes, std::string> encodeEvent(const DecoderEvent& event);

std::optional<std::string> checkByteReferences(const DecoderInfo& info, std::span<const Annotation> batch,
                                               std::size_t valueCount);

std::expected<DecoderFacetInfo, DecodeError> decodeInfo(std::span<const uint8_t> bytes);
std::expected<DecoderCommand, DecodeError> decodeCommand(std::span<const uint8_t> bytes);
std::expected<DecoderEvent, DecodeError> decodeEvent(std::span<const uint8_t> bytes);

}

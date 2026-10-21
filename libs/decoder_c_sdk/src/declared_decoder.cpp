/*
 * SPDX-License-Identifier: MIT
 */

#include "declared_decoder.h"

#include <utility>
#include <variant>

namespace bitoscope::plugin_c_sdk {

namespace dec = ipc::decoder;

namespace {

std::optional<int64_t> limitOf(const dec::Limit& limit) {
  if (const auto* value = std::get_if<int64_t>(&limit)) return *value;
  return std::nullopt;
}

}

std::optional<decode::StreamType> streamTypeOf(dec::StreamType type) {
  switch (type) {
    case dec::StreamType::words:
      return decode::StreamType::Words;
    case dec::StreamType::spiTransfers:
      return decode::StreamType::SpiTransfers;
    default:
      return std::nullopt;
  }
}

decode::ValueFormat valueFormatOf(const dec::ValueFormat& in) {
  decode::ValueFormat out;
  if (const auto* hex = std::get_if<dec::HexValue>(&in)) {
    out.kind = decode::ValueFormatKind::Hex;
    out.hex_min_digits = hex->digits;
    out.hex_digits_option = hex->digitsOption;
  } else if (std::holds_alternative<dec::Address7Value>(in)) {
    out.kind = decode::ValueFormatKind::Address7;
  } else if (std::holds_alternative<dec::NOfMValue>(in)) {
    out.kind = decode::ValueFormatKind::NOfM;
  } else if (std::holds_alternative<dec::DecimalValue>(in)) {
    out.kind = decode::ValueFormatKind::Decimal;
  } else if (std::holds_alternative<dec::NOfMBytesValue>(in)) {
    out.kind = decode::ValueFormatKind::NOfMBytes;
  } else if (std::holds_alternative<dec::BytesValue>(in)) {
    out.kind = decode::ValueFormatKind::Bytes;
  }
  return out;
}

DeclaredDecoder::DeclaredDecoder(const dec::DecoderInfo& info, Check check)
    : info_(info), check_(std::move(check)) {}

std::string DeclaredDecoder::name() const { return info_.name; }

std::string DeclaredDecoder::display() const { return info_.display; }

std::vector<decode::ChannelSpec> DeclaredDecoder::channels() const {
  std::vector<decode::ChannelSpec> out;
  for (const dec::ChannelSpec& c : info_.channels) out.push_back({c.name, c.description, c.required, c.display});
  return out;
}

std::vector<decode::OptionSpec> DeclaredDecoder::options() const {
  std::vector<decode::OptionSpec> out;
  for (const dec::OptionSpec& o : info_.options) {
    out.push_back({o.name, o.description,
                   o.type == dec::OptionType::integer ? decode::OptionType::Integer : decode::OptionType::String,
                   o.defaultValue, o.allowedValues, limitOf(o.min), limitOf(o.max), o.unsupportedValues, o.display});
  }
  return out;
}

std::vector<std::string> DeclaredDecoder::unsupportedOptions() const { return info_.unsupportedOptions; }

std::vector<std::string> DeclaredDecoder::protocols() const { return info_.protocols; }

bool DeclaredDecoder::allowsSharedChannels() const { return info_.allowsSharedChannels; }

std::vector<decode::AnnotationClass> DeclaredDecoder::annotationClasses() const {
  std::vector<decode::AnnotationClass> out;
  for (const dec::AnnotationClass& k : info_.annotationClasses) {
    out.push_back({k.id, k.name, valueFormatOf(k.format), static_cast<decode::AnnotationRole>(k.role)});
  }
  return out;
}

std::vector<decode::AnnotationRow> DeclaredDecoder::rows() const {
  std::vector<decode::AnnotationRow> out;
  for (const dec::Row& r : info_.rows) out.push_back({r.id, r.name});
  return out;
}

std::vector<decode::RowSource> DeclaredDecoder::rowSources() const {
  std::vector<decode::RowSource> out;
  for (const dec::RowSource& s : info_.rowSources) {
    decode::RowSource r{s.row, s.cls, s.source.row, s.source.classes, s.enableOption, decode::RowSource::Unknown{}};
    if (std::holds_alternative<dec::WordListRule>(s.rule)) {
      r.rule = decode::RowSource::WordList{};
    } else if (const auto* grid = std::get_if<dec::BitGridRule>(&s.rule)) {
      r.rule = decode::RowSource::BitGrid{grid->rateOption, grid->offsetBits, grid->countOption, grid->orderOption};
    } else if (const auto* edges = std::get_if<dec::ClockEdgesRule>(&s.rule)) {
      r.rule = decode::RowSource::ClockEdges{edges->clockChannel, edges->dataChannel, edges->countOption, edges->count};
    }
    out.push_back(std::move(r));
  }
  return out;
}

std::vector<decode::OutputSpec> DeclaredDecoder::outputs() const {
  std::vector<decode::OutputSpec> out;
  for (const dec::OutputSpec& o : info_.outputs) {
    decode::OutputSpec spec{o.name,       o.description,     streamTypeOf(o.type).value_or(decode::StreamType::Words),
                            o.width,      o.widthOption,     o.requiresChannel,
                            o.display};
    if (!o.carries.empty()) spec.carries = o.carries;
    if (!o.carriesInput.empty()) spec.carries_input = o.carriesInput;
    out.push_back(std::move(spec));
  }
  return out;
}

std::vector<decode::InputSpec> DeclaredDecoder::inputs() const {
  std::vector<decode::InputSpec> out;
  for (const dec::InputSpec& i : info_.inputs) {
    out.push_back({i.name, i.description, streamTypeOf(i.type).value_or(decode::StreamType::Words), i.width,
                   i.required, i.display});
  }
  return out;
}

std::expected<void, std::string> DeclaredDecoder::checkCaptureFacts(const decode::CaptureFacts&) const {
  if (!check_) return {};
  return check_();
}

std::expected<void, std::string> DeclaredDecoder::run(const decode::SignalView&, decode::AnnotationSink&) {
  return std::unexpected(std::string("declared decoder is checked here, never run"));
}

}

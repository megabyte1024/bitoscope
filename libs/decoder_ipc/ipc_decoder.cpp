/*
 * SPDX-License-Identifier: MIT
 */

#include "ipc_decoder.h"

#include <algorithm>
#include <optional>
#include <string>
#include <utility>

#include <capnp/message.h>

#include "decoder.capnp.h"
#include "ipc_capnp_boundary.h"
#include "ipc_capnp_values.h"
#include "ipc_decoder_build.h"

namespace bitoscope::ipc::decoder {

namespace {

namespace w = bitoscope::ipc::wire::decoder;
using detail::asText;
using detail::toString;

DecodeError missing(std::string message) { return DecodeError{DecodeErrorKind::missingField, std::move(message), {}, 0}; }

DecodeError unknownMember(std::string_view what, uint16_t member) {
  return DecodeError{DecodeErrorKind::unknownMember,
                     "unknown " + std::string(what) + " member " + std::to_string(member) +
                         " (from newer peer that this build does not know)",
                     {},
                     member};
}

DecodeError noMessage(std::string_view what) {
  return DecodeError{DecodeErrorKind::noMessage,
                     "decoder " + std::string(what) + " holds no " + std::string(what) + " (its union is `none`)", {}, 0};
}

void buildTexts(capnp::List<capnp::Text>::Builder out, const std::vector<std::string>& in) {
  for (unsigned i = 0; i < out.size(); ++i) out.set(i, asText(in[i]));
}

void build(w::Limit::Builder out, const Limit& in) {
  if (const auto* value = std::get_if<int64_t>(&in)) {
    out.setValue(*value);
  } else {
    out.setNone();
  }
}

void build(w::ValueFormat::Builder out, const ValueFormat& in) {
  if (const auto* hex = std::get_if<HexValue>(&in)) {
    auto format = out.initHex();
    format.setMinDigits(hex->digits);
    if (!hex->digitsOption.empty()) format.setDigitsOption(asText(hex->digitsOption));
  } else if (std::holds_alternative<Address7Value>(in)) {
    out.setAddress7();
  } else if (std::holds_alternative<NOfMValue>(in)) {
    out.setNOfM();
  } else if (std::holds_alternative<DecimalValue>(in)) {
    out.setDecimal();
  } else if (std::holds_alternative<NOfMBytesValue>(in)) {
    out.setNOfMBytes();
  } else if (std::holds_alternative<BytesValue>(in)) {
    out.setBytes();
  } else {
    out.setNone();
  }
}

void build(w::RowSource::Builder out, const RowSource& in) {
  out.setRow(in.row);
  out.setCls(in.cls);
  auto source = out.initSource();
  source.setRow(in.source.row);
  if (!in.source.classes.empty()) {
    auto classes = source.initClasses(static_cast<unsigned>(in.source.classes.size()));
    for (unsigned i = 0; i < classes.size(); ++i) classes.set(i, in.source.classes[i]);
  }
  if (!in.enableOption.empty()) out.setEnableOption(asText(in.enableOption));
  auto rule = out.getRule();
  if (std::holds_alternative<WordListRule>(in.rule)) {
    rule.setWordList();
  } else if (const auto* grid = std::get_if<BitGridRule>(&in.rule)) {
    auto g = rule.initBitGrid();
    if (!grid->rateOption.empty()) g.setRateOption(asText(grid->rateOption));
    g.setOffsetBits(grid->offsetBits);
    if (!grid->countOption.empty()) g.setCountOption(asText(grid->countOption));
    if (!grid->orderOption.empty()) g.setOrderOption(asText(grid->orderOption));
  } else if (const auto* edges = std::get_if<ClockEdgesRule>(&in.rule)) {
    auto e = rule.initClockEdges();
    if (!edges->clockChannel.empty()) e.setClockChannel(asText(edges->clockChannel));
    if (!edges->dataChannel.empty()) e.setDataChannel(asText(edges->dataChannel));
    if (!edges->countOption.empty()) e.setCountOption(asText(edges->countOption));
    e.setCount(edges->count);
  } else {
    rule.setNone();
  }
}

void build(w::DecoderInfo::Builder out, const DecoderInfo& in) {
  if (!in.name.empty()) out.setName(asText(in.name));
  if (!in.version.empty()) out.setVersion(asText(in.version));
  if (!in.channels.empty()) {
    auto channels = out.initChannels(static_cast<unsigned>(in.channels.size()));
    for (unsigned i = 0; i < channels.size(); ++i) {
      const ChannelSpec& c = in.channels[i];
      if (!c.name.empty()) channels[i].setName(asText(c.name));
      if (!c.description.empty()) channels[i].setDescription(asText(c.description));
      channels[i].setRequired(c.required);
      if (!c.display.empty()) channels[i].setDisplay(asText(c.display));
    }
  }
  if (!in.options.empty()) {
    auto options = out.initOptions(static_cast<unsigned>(in.options.size()));
    for (unsigned i = 0; i < options.size(); ++i) {
      const OptionSpec& o = in.options[i];
      auto option = options[i];
      if (!o.name.empty()) option.setName(asText(o.name));
      if (!o.description.empty()) option.setDescription(asText(o.description));
      option.setType(static_cast<w::OptionType>(static_cast<uint16_t>(o.type)));
      if (!o.defaultValue.empty()) option.setDefaultValue(asText(o.defaultValue));
      if (!o.allowedValues.empty()) {
        buildTexts(option.initAllowedValues(static_cast<unsigned>(o.allowedValues.size())), o.allowedValues);
      }
      build(option.initMin(), o.min);
      build(option.initMax(), o.max);
      if (!o.unsupportedValues.empty()) {
        buildTexts(option.initUnsupportedValues(static_cast<unsigned>(o.unsupportedValues.size())),
                   o.unsupportedValues);
      }
      if (!o.display.empty()) option.setDisplay(asText(o.display));
    }
  }
  if (!in.unsupportedOptions.empty()) {
    buildTexts(out.initUnsupportedOptions(static_cast<unsigned>(in.unsupportedOptions.size())), in.unsupportedOptions);
  }
  if (!in.annotationClasses.empty()) {
    auto classes = out.initAnnotationClasses(static_cast<unsigned>(in.annotationClasses.size()));
    for (unsigned i = 0; i < classes.size(); ++i) {
      const AnnotationClass& k = in.annotationClasses[i];
      classes[i].setId(k.id);
      if (!k.name.empty()) classes[i].setName(asText(k.name));
      build(classes[i].initFormat(), k.format);
      classes[i].setRole(static_cast<w::Role>(static_cast<uint16_t>(k.role)));
    }
  }
  if (!in.rows.empty()) {
    auto rows = out.initRows(static_cast<unsigned>(in.rows.size()));
    for (unsigned i = 0; i < rows.size(); ++i) {
      rows[i].setId(in.rows[i].id);
      if (!in.rows[i].name.empty()) rows[i].setName(asText(in.rows[i].name));
    }
  }
  out.setAllowsSharedChannels(in.allowsSharedChannels);
  if (!in.outputs.empty()) {
    auto outputs = out.initOutputs(static_cast<unsigned>(in.outputs.size()));
    for (unsigned i = 0; i < outputs.size(); ++i) {
      const OutputSpec& o = in.outputs[i];
      if (!o.name.empty()) outputs[i].setName(asText(o.name));
      if (!o.description.empty()) outputs[i].setDescription(asText(o.description));
      outputs[i].setType(static_cast<w::StreamType>(static_cast<uint16_t>(o.type)));
      outputs[i].setWidth(o.width);
      if (!o.widthOption.empty()) outputs[i].setWidthOption(asText(o.widthOption));
      if (!o.requiresChannel.empty()) outputs[i].setRequiresChannel(asText(o.requiresChannel));
      if (!o.display.empty()) outputs[i].setDisplay(asText(o.display));
      if (!o.carries.empty()) outputs[i].setCarries(asText(o.carries));
      if (!o.carriesInput.empty()) outputs[i].setCarriesInput(asText(o.carriesInput));
    }
  }
  if (!in.inputs.empty()) {
    auto inputs = out.initInputs(static_cast<unsigned>(in.inputs.size()));
    for (unsigned i = 0; i < inputs.size(); ++i) {
      const InputSpec& n = in.inputs[i];
      if (!n.name.empty()) inputs[i].setName(asText(n.name));
      if (!n.description.empty()) inputs[i].setDescription(asText(n.description));
      inputs[i].setType(static_cast<w::StreamType>(static_cast<uint16_t>(n.type)));
      inputs[i].setWidth(n.width);
      inputs[i].setRequired(n.required);
      if (!n.display.empty()) inputs[i].setDisplay(asText(n.display));
    }
  }
  if (!in.display.empty()) out.setDisplay(asText(in.display));
  if (!in.protocols.empty()) {
    buildTexts(out.initProtocols(static_cast<unsigned>(in.protocols.size())), in.protocols);
  }
  if (!in.rowSources.empty()) {
    auto sources = out.initRowSources(static_cast<unsigned>(in.rowSources.size()));
    for (unsigned i = 0; i < sources.size(); ++i) build(sources[i], in.rowSources[i]);
  }
}

void build(w::JobSetup::Builder out, const JobSetup& in) {
  if (!in.decoder.empty()) out.setDecoder(asText(in.decoder));
  if (!in.channels.empty()) {
    auto channels = out.initChannels(static_cast<unsigned>(in.channels.size()));
    for (unsigned i = 0; i < channels.size(); ++i) {
      if (!in.channels[i].name.empty()) channels[i].setName(asText(in.channels[i].name));
      channels[i].setChannel(in.channels[i].channel);
    }
  }
  if (!in.options.empty()) {
    auto options = out.initOptions(static_cast<unsigned>(in.options.size()));
    for (unsigned i = 0; i < options.size(); ++i) {
      if (!in.options[i].name.empty()) options[i].setName(asText(in.options[i].name));
      if (!in.options[i].value.empty()) options[i].setValue(asText(in.options[i].value));
    }
  }
  auto facts = out.initFacts();
  facts.setSampleRateMilliHz(in.facts.sampleRateMilliHz);
  facts.setChannelCount(in.facts.channelCount);
  facts.setTotalSamples(in.facts.totalSamples);
  facts.setTotalIsBound(in.facts.totalIsBound);
  if (!in.outputs.empty()) buildTexts(out.initOutputs(static_cast<unsigned>(in.outputs.size())), in.outputs);
  if (!in.inputs.empty()) {
    auto inputs = out.initInputs(static_cast<unsigned>(in.inputs.size()));
    for (unsigned i = 0; i < inputs.size(); ++i) {
      if (!in.inputs[i].name.empty()) inputs[i].setName(asText(in.inputs[i].name));
      inputs[i].setType(static_cast<w::StreamType>(static_cast<uint16_t>(in.inputs[i].type)));
      inputs[i].setWidth(in.inputs[i].width);
    }
  }
  out.setProgressStep(in.progressStep);
}

std::vector<std::string> readTexts(capnp::List<capnp::Text>::Reader in) {
  std::vector<std::string> out;
  out.reserve(in.size());
  for (capnp::Text::Reader text : in) out.push_back(toString(text));
  return out;
}

Limit read(w::Limit::Reader in) {
  switch (in.which()) {
    case w::Limit::NONE:
      return Unbounded{};
    case w::Limit::VALUE:
      return in.getValue();
  }
  return UnknownMember{static_cast<uint16_t>(in.which())};
}

AnnotationRole read(w::Role in) {
  const auto number = static_cast<uint16_t>(in);
  return number <= static_cast<uint16_t>(AnnotationRole::info) ? static_cast<AnnotationRole>(number)
                                                                : AnnotationRole::none;
}

ValueFormat read(w::ValueFormat::Reader in) {
  switch (in.which()) {
    case w::ValueFormat::NONE:
      return NoValue{};
    case w::ValueFormat::HEX: {
      auto hex = in.getHex();
      return HexValue{hex.getMinDigits(), toString(hex.getDigitsOption())};
    }
    case w::ValueFormat::ADDRESS7:
      return Address7Value{};
    case w::ValueFormat::N_OF_M:
      return NOfMValue{};
    case w::ValueFormat::DECIMAL:
      return DecimalValue{};
    case w::ValueFormat::N_OF_M_BYTES:
      return NOfMBytesValue{};
    case w::ValueFormat::BYTES:
      return BytesValue{};
  }
  return UnknownMember{static_cast<uint16_t>(in.which())};
}

RowSource read(w::RowSource::Reader in) {
  RowSource out;
  out.row = in.getRow();
  out.cls = in.getCls();
  auto source = in.getSource();
  out.source.row = source.getRow();
  for (const uint32_t c : source.getClasses()) out.source.classes.push_back(c);
  out.enableOption = toString(in.getEnableOption());
  auto rule = in.getRule();
  switch (rule.which()) {
    case w::RowSource::Rule::WORD_LIST:
      out.rule = WordListRule{};
      break;
    case w::RowSource::Rule::BIT_GRID: {
      auto g = rule.getBitGrid();
      out.rule = BitGridRule{toString(g.getRateOption()), g.getOffsetBits(), toString(g.getCountOption()),
                             toString(g.getOrderOption())};
      break;
    }
    case w::RowSource::Rule::CLOCK_EDGES: {
      auto e = rule.getClockEdges();
      out.rule = ClockEdgesRule{toString(e.getClockChannel()), toString(e.getDataChannel()),
                                toString(e.getCountOption()), e.getCount()};
      break;
    }
    default:
      out.rule = NoRowRule{};
      break;
  }
  return out;
}

std::optional<DecodeError> read(w::DecoderInfo::Reader in, DecoderInfo& out) {
  out.name = toString(in.getName());
  if (out.name.empty()) return missing("decoder facet's info lists decoder with no name");
  out.version = toString(in.getVersion());
  out.display = toString(in.getDisplay());
  out.protocols = readTexts(in.getProtocols());
  for (w::ChannelSpec::Reader c : in.getChannels()) {
    ChannelSpec channel{toString(c.getName()), toString(c.getDescription()), c.getRequired(),
                        toString(c.getDisplay())};
    if (channel.name.empty()) return missing("decoder '" + out.name + "' has channel with no name");
    out.channels.push_back(std::move(channel));
  }
  for (w::OptionSpec::Reader o : in.getOptions()) {
    OptionSpec option;
    option.name = toString(o.getName());
    if (option.name.empty()) return missing("decoder '" + out.name + "' has option with no name");
    option.description = toString(o.getDescription());
    option.type = static_cast<OptionType>(static_cast<uint16_t>(o.getType()));
    if (option.type == OptionType::unspecified) {
      return missing("option '" + option.name + "' of decoder '" + out.name + "' has no type");
    }
    option.defaultValue = toString(o.getDefaultValue());
    option.allowedValues = readTexts(o.getAllowedValues());
    option.min = read(o.getMin());
    option.max = read(o.getMax());
    option.unsupportedValues = readTexts(o.getUnsupportedValues());
    option.display = toString(o.getDisplay());
    out.options.push_back(std::move(option));
  }
  out.unsupportedOptions = readTexts(in.getUnsupportedOptions());
  for (w::AnnotationClass::Reader k : in.getAnnotationClasses()) {
    out.annotationClasses.push_back(
        AnnotationClass{k.getId(), toString(k.getName()), read(k.getFormat()), read(k.getRole())});
  }
  for (w::Row::Reader r : in.getRows()) out.rows.push_back(Row{r.getId(), toString(r.getName())});
  for (w::RowSource::Reader s : in.getRowSources()) out.rowSources.push_back(read(s));
  out.allowsSharedChannels = in.getAllowsSharedChannels();
  for (w::OutputSpec::Reader o : in.getOutputs()) {
    OutputSpec output{toString(o.getName()),
                      toString(o.getDescription()),
                      static_cast<StreamType>(static_cast<uint16_t>(o.getType())),
                      o.getWidth(),
                      toString(o.getWidthOption()),
                      toString(o.getRequiresChannel()),
                      toString(o.getDisplay()),
                      toString(o.getCarries()),
                      toString(o.getCarriesInput())};
    if (output.name.empty()) return missing("decoder '" + out.name + "' has output with no name");
    if (output.type == StreamType::unspecified) {
      return missing("output '" + output.name + "' of decoder '" + out.name + "' has no type");
    }
    out.outputs.push_back(std::move(output));
  }
  for (w::InputSpec::Reader n : in.getInputs()) {
    InputSpec input{toString(n.getName()), toString(n.getDescription()),
                    static_cast<StreamType>(static_cast<uint16_t>(n.getType())), n.getWidth(), n.getRequired(),
                    toString(n.getDisplay())};
    if (input.name.empty()) return missing("decoder '" + out.name + "' has input with no name");
    if (input.type == StreamType::unspecified) {
      return missing("input '" + input.name + "' of decoder '" + out.name + "' has no type");
    }
    out.inputs.push_back(std::move(input));
  }
  return std::nullopt;
}

std::vector<StreamItem> readItems(capnp::List<w::StreamItem>::Reader in) {
  std::vector<StreamItem> out;
  out.reserve(in.size());
  for (w::StreamItem::Reader i : in) {
    out.push_back(StreamItem{i.getBegin(), i.getEnd(), i.getValue(), i.getKind(), i.getFlags()});
  }
  return out;
}

std::optional<DecodeError> read(w::JobSetup::Reader in, JobSetup& out) {
  out.decoder = toString(in.getDecoder());
  if (out.decoder.empty()) return missing("decoder job names no decoder");
  for (w::ChannelBinding::Reader c : in.getChannels()) {
    ChannelBinding binding{toString(c.getName()), c.getChannel()};
    if (binding.name.empty()) return missing("decoder job binds channel with no name");
    out.channels.push_back(std::move(binding));
  }
  for (w::OptionValue::Reader o : in.getOptions()) {
    OptionValue value{toString(o.getName()), toString(o.getValue())};
    if (value.name.empty()) return missing("decoder job sets option with no name");
    out.options.push_back(std::move(value));
  }
  auto facts = in.getFacts();
  out.facts = CaptureFacts{facts.getSampleRateMilliHz(), facts.getChannelCount(), facts.getTotalSamples(),
                           facts.getTotalIsBound()};
  if (out.facts.sampleRateMilliHz == 0) return missing("decoder job's capture has no sample rate");
  if (out.facts.channelCount == 0) return missing("decoder job's capture has no channel count");
  for (capnp::Text::Reader o : in.getOutputs()) {
    out.outputs.push_back(toString(o));
    if (out.outputs.back().empty()) return missing("decoder job wants output with no name");
  }
  for (w::InputBinding::Reader i : in.getInputs()) {
    InputBinding binding{toString(i.getName()), static_cast<StreamType>(static_cast<uint16_t>(i.getType())),
                         i.getWidth()};
    if (binding.name.empty()) return missing("decoder job binds input with no name");
    out.inputs.push_back(std::move(binding));
  }
  out.progressStep = in.getProgressStep();
  return std::nullopt;
}

std::optional<DecodeError> read(w::DecodeStart::Reader in, DecodeStart& out) {
  if (auto e = read(in.getSetup(), out.setup)) return e;
  out.ringName = toString(in.getRingName());
  if (out.ringName.empty() && out.setup.channels.empty()) return std::nullopt;
  if (out.ringName.empty()) {
    return missing("DECODE names no ring: job's samples travel only through ring host creates for it");
  }
  auto ring = in.getRing();
  out.ring = RingGeometry{ring.getSlotCount(), ring.getSlotPayloadBytes()};
  if (out.ring.slotCount == 0) return missing("DECODE's ring has no slots");
  if (out.ring.slotPayloadBytes == 0) return missing("DECODE's ring has no slot payload size");
  return std::nullopt;
}

}

std::expected<Bytes, std::string> encodeInfo(const DecoderFacetInfo& info) {
  return detail::buildMessage([&](capnp::MessageBuilder& message) {
    auto root = message.initRoot<w::DecoderFacetInfo>();
    if (!info.pluginVersion.empty()) root.setPluginVersion(asText(info.pluginVersion));
    root.setRunsScripts(info.runsScripts);
    if (info.decoders.empty()) return;
    auto decoders = root.initDecoders(static_cast<unsigned>(info.decoders.size()));
    for (unsigned i = 0; i < decoders.size(); ++i) build(decoders[i], info.decoders[i]);
  });
}

std::expected<Bytes, std::string> encodeCommand(const DecoderCommand& command) {
  return detail::buildMessage([&](capnp::MessageBuilder& message) {
    auto root = message.initRoot<w::DecoderCommand>();
    root.setJobId(command.jobId);
    if (const auto* check = std::get_if<CheckJob>(&command.action)) {
      build(root.initCheck(), check->setup);
    } else if (const auto* start = std::get_if<DecodeStart>(&command.action)) {
      auto out = root.initDecode();
      build(out.initSetup(), start->setup);
      if (!start->ringName.empty()) out.setRingName(asText(start->ringName));
      auto ring = out.initRing();
      ring.setSlotCount(start->ring.slotCount);
      ring.setSlotPayloadBytes(start->ring.slotPayloadBytes);
    } else if (const auto* feed = std::get_if<ItemFeed>(&command.action)) {
      auto out = root.initItems();
      if (!feed->input.empty()) out.setInput(asText(feed->input));
      // WHY: Initialized even when empty: a last, empty batch still says "no more".
      buildItems(out.initItems(static_cast<unsigned>(feed->items.size())), feed->items);
      out.setLast(feed->last);
    } else {
      root.setCancel();
    }
  });
}

std::expected<Bytes, std::string> encodeEvent(const DecoderEvent& event) {
  return detail::buildMessage([&](capnp::MessageBuilder& message) {
    if (const auto* batch = std::get_if<AnnotationBatch>(&event.body)) {
      buildAnnotationEvent(message, event.jobId, event.settled, batch->annotations,
                           [](const Annotation& a) { return a.value; });
      return;
    }
    if (const auto* items = std::get_if<ItemBatch>(&event.body)) {
      buildItemEvent(message, event.jobId, event.settled, items->output, items->items);
      return;
    }
    auto root = eventRoot(message, event.jobId, event.settled);
    if (const auto* ended = std::get_if<DecodeEnded>(&event.body)) {
      auto out = root.initDecodeEnded();
      out.setStatus(static_cast<w::DecodeStatus>(static_cast<uint16_t>(ended->status)));
      if (!ended->error.empty()) out.setError(asText(ended->error));
      out.setAnnotationCount(ended->annotationCount);
      out.setSamplesRead(ended->samplesRead);
      out.setItemCount(ended->itemCount);
      out.setItemsRead(ended->itemsRead);
    } else if (const auto* byteValues = std::get_if<ByteValues>(&event.body)) {
      auto out = root.initByteValues(static_cast<unsigned>(byteValues->values.size()));
      for (unsigned i = 0; i < out.size(); ++i) {
        const std::vector<uint8_t>& value = byteValues->values[i];
        out.set(i, capnp::Data::Reader(value.data(), value.size()));
      }
    } else {
      const auto& wanted = std::get<ItemsWanted>(event.body);
      auto out = root.initItemsWanted();
      if (!wanted.input.empty()) out.setInput(asText(wanted.input));
    }
  });
}

std::optional<std::string> checkByteReferences(const DecoderInfo& info, std::span<const Annotation> batch,
                                               std::size_t valueCount) {
  std::vector<uint32_t> bytesClasses;
  for (const AnnotationClass& k : info.annotationClasses) {
    if (std::holds_alternative<BytesValue>(k.format)) bytesClasses.push_back(k.id);
  }
  std::size_t used = 0;
  if (!bytesClasses.empty()) {
    for (std::size_t i = 0; i < batch.size(); ++i) {
      const Annotation& a = batch[i];
      if (std::find(bytesClasses.begin(), bytesClasses.end(), a.cls) == bytesClasses.end()) continue;
      const std::string what = "annotation " + std::to_string(i) + " of class " + std::to_string(a.cls) +
                               " refers to byte value " + std::to_string(a.value) + ", but ";
      if (a.value >= valueCount) return what + "batch carries only " + std::to_string(valueCount) + " byte values";
      if (a.value != used) return what + "byte values are used in order: expected " + std::to_string(used);
      ++used;
    }
  }
  if (used == valueCount) return std::nullopt;
  return "batch carries " + std::to_string(valueCount) + " byte values, but " + std::to_string(used) +
         " of its annotations are of classes that show bytes";
}

std::expected<DecoderFacetInfo, DecodeError> decodeInfo(std::span<const uint8_t> bytes) {
  DecoderFacetInfo out;
  auto error = detail::readMessage(bytes, [&](capnp::MessageReader& reader) -> std::optional<DecodeError> {
    auto root = reader.getRoot<w::DecoderFacetInfo>();
    out.pluginVersion = toString(root.getPluginVersion());
    out.runsScripts = root.getRunsScripts();
    for (w::DecoderInfo::Reader in : root.getDecoders()) {
      DecoderInfo info;
      if (auto e = read(in, info)) return e;
      out.decoders.push_back(std::move(info));
    }
    return std::nullopt;
  });
  if (error) return std::unexpected(std::move(*error));
  return out;
}

std::expected<DecoderCommand, DecodeError> decodeCommand(std::span<const uint8_t> bytes) {
  DecoderCommand out;
  auto error = detail::readMessage(bytes, [&](capnp::MessageReader& reader) -> std::optional<DecodeError> {
    auto root = reader.getRoot<w::DecoderCommand>();
    // WHY: Test the union first: an all-zero message is the none sentinel, whatever its job id.
    switch (root.which()) {
      case w::DecoderCommand::NONE:
        return noMessage("command");
      case w::DecoderCommand::CHECK: {
        CheckJob check;
        if (auto e = read(root.getCheck(), check.setup)) return e;
        out.action = std::move(check);
        break;
      }
      case w::DecoderCommand::DECODE: {
        DecodeStart start;
        if (auto e = read(root.getDecode(), start)) return e;
        out.action = std::move(start);
        break;
      }
      case w::DecoderCommand::CANCEL:
        out.action = CancelJob{};
        break;
      case w::DecoderCommand::ITEMS: {
        auto in = root.getItems();
        ItemFeed feed{toString(in.getInput()), readItems(in.getItems()), in.getLast()};
        if (feed.input.empty()) return missing("decoder's item feed names no input");
        out.action = std::move(feed);
        break;
      }
      default:
        return unknownMember("decoder command", static_cast<uint16_t>(root.which()));
    }
    out.jobId = root.getJobId();
    if (out.jobId == 0) return missing("decoder command names no job");
    return std::nullopt;
  });
  if (error) return std::unexpected(std::move(*error));
  return out;
}

std::expected<DecoderEvent, DecodeError> decodeEvent(std::span<const uint8_t> bytes) {
  DecoderEvent out;
  auto error = detail::readMessage(bytes, [&](capnp::MessageReader& reader) -> std::optional<DecodeError> {
    auto root = reader.getRoot<w::DecoderEvent>();
    switch (root.which()) {
      case w::DecoderEvent::NONE:
        return noMessage("event");
      case w::DecoderEvent::ANNOTATIONS: {
        AnnotationBatch batch;
        auto in = root.getAnnotations();
        batch.annotations.reserve(in.size());
        for (w::Annotation::Reader a : in) {
          batch.annotations.push_back(Annotation{a.getBegin(), a.getEnd(), a.getRow(), a.getCls(), a.getValue()});
        }
        out.body = std::move(batch);
        break;
      }
      case w::DecoderEvent::DECODE_ENDED: {
        auto in = root.getDecodeEnded();
        DecodeEnded ended{static_cast<DecodeStatus>(static_cast<uint16_t>(in.getStatus())), toString(in.getError()),
                          in.getAnnotationCount(), in.getSamplesRead(), in.getItemCount(), in.getItemsRead()};
        if (ended.status == DecodeStatus::unspecified) return missing("decode ended with no status");
        out.body = std::move(ended);
        break;
      }
      case w::DecoderEvent::ITEMS: {
        auto in = root.getItems();
        ItemBatch batch{toString(in.getOutput()), readItems(in.getItems())};
        if (batch.output.empty()) return missing("decoder's items name no output");
        out.body = std::move(batch);
        break;
      }
      case w::DecoderEvent::BYTE_VALUES: {
        ByteValues values;
        auto in = root.getByteValues();
        values.values.reserve(in.size());
        for (const auto data : in) values.values.emplace_back(data.begin(), data.end());
        out.body = std::move(values);
        break;
      }
      case w::DecoderEvent::ITEMS_WANTED: {
        ItemsWanted wanted{toString(root.getItemsWanted().getInput())};
        if (wanted.input.empty()) return missing("decoder job asks for items of no input");
        out.body = std::move(wanted);
        break;
      }
      default:
        return unknownMember("decoder event", static_cast<uint16_t>(root.which()));
    }
    out.jobId = root.getJobId();
    if (out.jobId == 0) return missing("decoder event names no job");
    out.settled = root.getSettled();
    return std::nullopt;
  });
  if (error) return std::unexpected(std::move(*error));
  return out;
}

Bytes encodeCheckAnswer(const CheckAnswer& answer) {
  Bytes out;
  if (answer.lookAheadSamples == 0) return out;
  for (unsigned i = 0; i < 8; ++i) out.push_back(static_cast<uint8_t>(answer.lookAheadSamples >> (8 * i)));
  return out;
}

std::expected<CheckAnswer, std::string> decodeCheckAnswer(std::span<const uint8_t> bytes) {
  CheckAnswer out;
  if (bytes.empty()) return out;
  if (bytes.size() < 8) {
    return std::unexpected("answer to CHECK is " + std::to_string(bytes.size()) + " bytes, not eight of look-ahead");
  }
  for (unsigned i = 0; i < 8; ++i) out.lookAheadSamples |= static_cast<uint64_t>(bytes[i]) << (8 * i);
  return out;
}

}

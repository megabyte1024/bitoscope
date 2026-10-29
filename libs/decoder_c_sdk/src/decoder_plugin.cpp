/*
 * SPDX-License-Identifier: MIT
 */

#include "decoder_plugin.h"

#include <algorithm>
#include <charconv>
#include <cstdio>
#include <cstring>
#include <exception>
#include <memory>
#include <optional>
#include <set>
#include <system_error>
#include <utility>
#include <vector>

#include "declared_decoder.h"
#include "decoder_model.h"
#include "decoder_runtime.h"
#include "display_text.h"
#include "frame_output.h"
#include "plugin_model.h"
#include "plugin_version.h"
#include "private_stdio.h"

namespace bitoscope::plugin_c_sdk {

namespace dec = ipc::decoder;

namespace {

std::string text(const char* s) { return s != nullptr ? std::string(s) : std::string(); }

bool missing(const void* array, size_t count) { return count > 0 && array == nullptr; }

std::string inQuotes(const std::string& s) { return "\"" + s + "\""; }

// WHY: Descriptions get display spelling; class and row names stay as written for the program to spell later.
std::string prose(const char* s) { return display_text::displayText(text(s)); }

std::string displayOfItem(const char* given) {
  const std::string text_ = text(given);
  return text_.empty() ? text_ : display_text::displayName(text_);
}

std::optional<dec::StreamType> streamTypeOfC(int type) {
  switch (type) {
    case BITOSCOPE_STREAM_WORDS:
      return dec::StreamType::words;
    case BITOSCOPE_STREAM_SPI_TRANSFERS:
      return dec::StreamType::spiTransfers;
    default:
      return std::nullopt;
  }
}

constexpr const char* kStreamTypes = "BITOSCOPE_STREAM_WORDS or BITOSCOPE_STREAM_SPI_TRANSFERS";

bool outsideLimits(const bitoscope_decoder_option& o, const std::string& value) {
  int64_t number = 0;
  const char* end = value.data() + value.size();
  const auto parsed = std::from_chars(value.data(), end, number);
  if (parsed.ec != std::errc() || parsed.ptr != end) return false;
  return (o.has_min != 0 && number < o.min) || (o.has_max != 0 && number > o.max);
}

}

std::expected<void, std::string> checkPluginVersion(std::string_view version) {
  if (version.empty()) {
    return std::unexpected(std::string(
        "decoder plugin has no version: set it with bitoscope_set_decoder_plugin_version() as X.Y.Z (\"1.0.0\")"));
  }
  if (auto parsed = ipc::parsePluginVersion(version); !parsed) {
    return std::unexpected("decoder plugin's " + parsed.error());
  }
  return {};
}

std::expected<dec::DecoderInfo, std::string> declarationOf(const bitoscope_decoder* decoder, size_t size) {
  if (decoder == nullptr) return std::unexpected(std::string("no decoder"));
  bitoscope_decoder d{};
  std::memcpy(&d, decoder, (std::min)(size, sizeof d));

  dec::DecoderInfo info;
  info.name = text(d.name);
  if (info.name.empty()) return std::unexpected(std::string("decoder needs name (host picks decoder by it)"));
  const std::string where = "decoder " + inQuotes(info.name) + ": ";
  const auto refuse = [&where](std::string why) { return std::unexpected(where + std::move(why)); };
  if (d.decode == nullptr) return refuse("no decode function");
  info.version = text(d.version);
  if (info.version.empty()) return refuse("no version: decoder declares it as X.Y.Z (\"1.0.0\")");
  if (auto parsed = ipc::parsePluginVersion(info.version); !parsed) return refuse(std::move(parsed.error()));
  info.allowsSharedChannels = d.allows_shared_channels != 0;
  info.display = displayOfItem(d.display);

  // WHY: A decoder with neither channels nor inputs is refused here; decode::checkDeclaration() does not refuse it.
  if (d.num_channels == 0 && d.num_inputs == 0) {
    return refuse("no channels or inputs: decoder reads at least one of them");
  }
  if (missing(d.channels, d.num_channels)) return refuse("channels missing");
  for (size_t i = 0; i < d.num_channels; ++i) {
    const bitoscope_decoder_channel& c = d.channels[i];
    dec::ChannelSpec channel{text(c.name), prose(c.description), c.optional == 0, displayOfItem(c.display)};
    if (channel.name.empty()) return refuse("channel needs name (user binds it by name)");
    for (const dec::ChannelSpec& other : info.channels) {
      if (other.name == channel.name) return refuse("channel " + inQuotes(channel.name) + " is declared twice");
    }
    info.channels.push_back(std::move(channel));
  }

  if (missing(d.inputs, d.num_inputs)) return refuse("inputs missing");
  for (size_t i = 0; i < d.num_inputs; ++i) {
    const bitoscope_decoder_input& in = d.inputs[i];
    dec::InputSpec input;
    input.name = text(in.name);
    if (input.name.empty()) return refuse("input needs name (user binds it by name)");
    const auto type = streamTypeOfC(in.type);
    if (!type) {
      return refuse("input " + inQuotes(input.name) + " has stream type " + std::to_string(in.type) + "; it is " +
                    kStreamTypes);
    }
    input.description = prose(in.description);
    input.type = *type;
    input.width = in.width;
    input.required = in.optional == 0;
    input.display = displayOfItem(in.display);
    info.inputs.push_back(std::move(input));
  }

  if (missing(d.outputs, d.num_outputs)) return refuse("outputs missing");
  for (size_t i = 0; i < d.num_outputs; ++i) {
    const bitoscope_decoder_output& o = d.outputs[i];
    dec::OutputSpec output;
    output.name = text(o.name);
    if (output.name.empty()) return refuse("output needs name (user binds it by name)");
    const auto type = streamTypeOfC(o.type);
    if (!type) {
      return refuse("output " + inQuotes(output.name) + " has stream type " + std::to_string(o.type) + "; it is " +
                    kStreamTypes);
    }
    output.description = prose(o.description);
    output.type = *type;
    output.width = o.width;
    output.widthOption = text(o.width_option);
    output.requiresChannel = text(o.requires_channel);
    output.display = displayOfItem(o.display);
    // WHY: Empty carries name is refused here: the wire carries NULL and empty alike, so this is the last place to tell.
    if (o.carries != nullptr) {
      if (*o.carries == '\0') return refuse("output " + inQuotes(output.name) + " carries protocol with no name");
      output.carries = o.carries;
    }
    if (o.carries_input != nullptr) {
      if (*o.carries_input == '\0') {
        return refuse("output " + inQuotes(output.name) + " passes through input with no name");
      }
      output.carriesInput = o.carries_input;
    }
    info.outputs.push_back(std::move(output));
  }

  if (missing(d.options, d.num_options)) return refuse("options missing");
  for (size_t i = 0; i < d.num_options; ++i) {
    const bitoscope_decoder_option& o = d.options[i];
    dec::OptionSpec option;
    option.name = text(o.name);
    if (option.name.empty()) return refuse("option needs name");
    const std::string name = inQuotes(option.name);
    for (const dec::OptionSpec& other : info.options) {
      if (other.name == option.name) return refuse("option " + name + " is declared twice");
    }
    if (o.type != BITOSCOPE_OPTION_STRING && o.type != BITOSCOPE_OPTION_INTEGER) {
      return refuse("option " + name + " has type " + std::to_string(o.type) +
                    "; it is BITOSCOPE_OPTION_STRING or BITOSCOPE_OPTION_INTEGER");
    }
    option.type = o.type == BITOSCOPE_OPTION_INTEGER ? dec::OptionType::integer : dec::OptionType::string;
    option.description = prose(o.description);
    option.display = displayOfItem(o.display);
    option.defaultValue = text(o.default_value);
    if (missing(o.allowed_values, o.num_allowed_values)) return refuse("option " + name + "'s allowed values missing");
    if (missing(o.unsupported_values, o.num_unsupported_values)) {
      return refuse("option " + name + "'s unsupported values missing");
    }
    for (size_t k = 0; k < o.num_allowed_values; ++k) option.allowedValues.push_back(text(o.allowed_values[k]));
    for (size_t k = 0; k < o.num_unsupported_values; ++k) {
      option.unsupportedValues.push_back(text(o.unsupported_values[k]));
    }
    if ((o.has_min != 0 || o.has_max != 0) && option.type != dec::OptionType::integer) {
      return refuse("option " + name + " has limits, but only integer option can have them");
    }
    if (o.has_min != 0 && o.has_max != 0 && o.min > o.max) {
      return refuse("option " + name + "'s minimum " + std::to_string(o.min) + " is above its maximum " +
                    std::to_string(o.max));
    }
    const auto listed = [&option](const std::vector<std::string>& values) {
      return std::find(values.begin(), values.end(), option.defaultValue) != values.end();
    };
    if (!option.allowedValues.empty() && !listed(option.allowedValues)) {
      return refuse("option " + name + "'s default " + inQuotes(option.defaultValue) + " is not one of its values");
    }
    if (outsideLimits(o, option.defaultValue)) {
      return refuse("option " + name + "'s default " + option.defaultValue + " is outside its limits");
    }
    if (listed(option.unsupportedValues)) {
      return refuse("option " + name + "'s default " + inQuotes(option.defaultValue) +
                    " is one of its unsupported values");
    }
    option.min = o.has_min != 0 ? dec::Limit{o.min} : dec::Limit{dec::Unbounded{}};
    option.max = o.has_max != 0 ? dec::Limit{o.max} : dec::Limit{dec::Unbounded{}};
    info.options.push_back(std::move(option));
  }
  const auto findOption = [&info](const std::string& name) -> const dec::OptionSpec* {
    for (const dec::OptionSpec& o : info.options) {
      if (o.name == name) return &o;
    }
    return nullptr;
  };

  if (auto checked = decode::checkDeclaration(DeclaredDecoder(info), decode::MixedDeclarations::Allowed); !checked) {
    return std::unexpected(std::move(checked.error()));
  }

  if (missing(d.unsupported_options, d.num_unsupported_options)) return refuse("unsupported options missing");
  for (size_t i = 0; i < d.num_unsupported_options; ++i) {
    std::string id = text(d.unsupported_options[i]);
    if (id.empty()) return refuse("unsupported option needs name");
    if (findOption(id) != nullptr) return refuse("unsupported option " + inQuotes(id) + " is also declared option");
    info.unsupportedOptions.push_back(std::move(id));
  }

  if (missing(d.protocols, d.num_protocols)) return refuse("protocols missing");
  for (size_t i = 0; i < d.num_protocols; ++i) {
    std::string id = text(d.protocols[i]);
    if (id.empty()) return refuse("protocol needs name (it names decoder)");
    for (const std::string& other : info.protocols) {
      if (other == id) return refuse("protocol " + inQuotes(id) + " is declared twice");
    }
    info.protocols.push_back(std::move(id));
  }

  if (d.num_classes == 0) return refuse("no annotation classes: decoder that declares none can emit nothing");
  if (missing(d.classes, d.num_classes)) return refuse("annotation classes missing");
  for (size_t i = 0; i < d.num_classes; ++i) {
    const bitoscope_annotation_class& k = d.classes[i];
    const std::string what = "annotation class " + std::to_string(k.id);
    for (const dec::AnnotationClass& other : info.annotationClasses) {
      if (other.id == k.id) return refuse(what + " is declared twice");
    }
    dec::AnnotationClass cls{k.id, text(k.name), dec::NoValue{}};
    const std::string option = text(k.hex_digits_option);
    switch (k.format) {
      case BITOSCOPE_VALUE_NONE:
        break;
      case BITOSCOPE_VALUE_HEX:
        cls.format = dec::HexValue{k.hex_digits, option};
        break;
      case BITOSCOPE_VALUE_ADDRESS7:
        cls.format = dec::Address7Value{};
        break;
      case BITOSCOPE_VALUE_N_OF_M:
        cls.format = dec::NOfMValue{};
        break;
      case BITOSCOPE_VALUE_DECIMAL:
        cls.format = dec::DecimalValue{};
        break;
      case BITOSCOPE_VALUE_N_OF_M_BYTES:
        cls.format = dec::NOfMBytesValue{};
        break;
      case BITOSCOPE_VALUE_BYTES:
        cls.format = dec::BytesValue{};
        break;
      default:
        return refuse(what + " has format " + std::to_string(k.format) +
                      "; it is BITOSCOPE_VALUE_NONE, BITOSCOPE_VALUE_HEX, BITOSCOPE_VALUE_ADDRESS7, "
                      "BITOSCOPE_VALUE_N_OF_M, BITOSCOPE_VALUE_DECIMAL, BITOSCOPE_VALUE_N_OF_M_BYTES or "
                      "BITOSCOPE_VALUE_BYTES");
    }
    if (k.role < BITOSCOPE_ROLE_NONE || k.role > BITOSCOPE_ROLE_INFO) {
      return refuse(what + " has role " + std::to_string(k.role) +
                    "; it is one of BITOSCOPE_ROLE_NONE to BITOSCOPE_ROLE_INFO");
    }
    cls.role = static_cast<dec::AnnotationRole>(k.role);
    if (!option.empty()) {
      if (k.format != BITOSCOPE_VALUE_HEX) {
        return refuse(what + " names hex_digits_option " + inQuotes(option) + " but is not BITOSCOPE_VALUE_HEX");
      }
      const dec::OptionSpec* spec = findOption(option);
      if (spec == nullptr) {
        return refuse(what + " takes its hex digits from option " + inQuotes(option) +
                      ", which decoder does not declare");
      }
      if (spec->type != dec::OptionType::integer) {
        return refuse(what + " takes its hex digits from option " + inQuotes(option) +
                      ", which is not integer option");
      }
    }
    info.annotationClasses.push_back(std::move(cls));
  }

  if (d.num_rows == 0) return refuse("no rows: every annotation is drawn in row decoder declares");
  if (missing(d.rows, d.num_rows)) return refuse("rows missing");
  for (size_t i = 0; i < d.num_rows; ++i) {
    for (const dec::Row& other : info.rows) {
      if (other.id == d.rows[i].id) return refuse("row " + std::to_string(d.rows[i].id) + " is declared twice");
    }
    info.rows.push_back(dec::Row{d.rows[i].id, text(d.rows[i].name)});
  }

  if (missing(d.row_sources, d.num_row_sources)) return refuse("row sources missing");
  for (size_t i = 0; i < d.num_row_sources; ++i) {
    const bitoscope_row_source& s = d.row_sources[i];
    const std::string what = "row source of row " + std::to_string(s.row);
    if (missing(s.source_classes, s.num_source_classes)) return refuse(what + ": source classes missing");
    dec::RowSource source{s.row, s.cls, dec::SourceRef{s.source_row, {}}, text(s.enable_option), dec::NoRowRule{}};
    source.source.classes.assign(s.source_classes, s.source_classes + s.num_source_classes);
    switch (s.rule) {
      case BITOSCOPE_ROW_RULE_WORD_LIST:
        source.rule = dec::WordListRule{};
        break;
      case BITOSCOPE_ROW_RULE_BIT_GRID:
        source.rule = dec::BitGridRule{text(s.grid.rate_option), s.grid.offset_bits, text(s.grid.count_option),
                                       text(s.grid.order_option)};
        break;
      case BITOSCOPE_ROW_RULE_CLOCK_EDGES:
        source.rule = dec::ClockEdgesRule{text(s.edges.clock_channel), text(s.edges.data_channel),
                                          text(s.edges.count_option), s.edges.count};
        break;
      default:
        return refuse(what + " has rule " + std::to_string(s.rule) +
                      "; it is BITOSCOPE_ROW_RULE_WORD_LIST, BITOSCOPE_ROW_RULE_BIT_GRID or "
                      "BITOSCOPE_ROW_RULE_CLOCK_EDGES");
    }
    info.rowSources.push_back(std::move(source));
  }
  if (auto checked = decode::checkRowSources(DeclaredDecoder(info)); !checked) {
    return std::unexpected(std::move(checked.error()));
  }
  return info;
}

namespace {

int listAsDevice(bitoscope_list_decoders_fn list, void* user, bitoscope_devices& devices) {
  if (list == nullptr) {
    logLine(ipc::LogLevel::error, "bitoscope_run_decoder_plugin() was given no list function");
    return BITOSCOPE_ERROR;
  }
  bitoscope_decoders decoders;
  int result = BITOSCOPE_ERROR;
  try {
    result = list(user, &decoders);
  } catch (...) {
    result = BITOSCOPE_ERROR;
  }
  if (result != BITOSCOPE_OK) return result;
  if (auto checked = checkPluginVersion(decoders.pluginVersion); !checked) {
    devices.listProblem = std::move(checked.error());
    return BITOSCOPE_ERROR;
  }
  devices.pluginVersion = decoders.pluginVersion;
  dec::DecoderFacetInfo info;
  for (const DecoderRegistration& d : decoders.decoders) info.decoders.push_back(d.info);
  info.pluginVersion = decoders.pluginVersion;
  info.runsScripts = decoders.runsScripts;
  auto encoded = dec::encodeInfo(info);
  if (!encoded) {
    logLine(ipc::LogLevel::error, "decoders' description could not be encoded: " + encoded.error());
    return BITOSCOPE_ERROR;
  }
  auto device = std::make_unique<bitoscope_device>();
  device->id = kDecoderDeviceId;
  device->name = kDecoderDeviceName;
  auto facet = std::make_shared<const DecoderFacetRegistration>(DecoderFacetRegistration{std::move(decoders.decoders)});
  device->facets.push_back(FacetRegistration{
      std::string(dec::kFacetName), kDecoderFacetMajor, kDecoderFacetMinor, 0, {}, std::move(*encoded),
      [facet](const std::vector<ipc::props::PropertyValue>&) {
        return std::unique_ptr<FacetHandler>(std::make_unique<DecoderRuntime>(*facet));
      }});
  devices.devices.push_back(std::move(device));
  return BITOSCOPE_OK;
}

}

int runDecoderPlugin(std::span<const std::string> args, ipc::ByteSource& in, ipc::ByteSink& out,
                     bitoscope_list_decoders_fn listDecoders, void* user, SessionOptions options) {
  options.interfaceKind = InterfaceKind::none;
  return runSession(
      args, in, out, [listDecoders, user](bitoscope_devices& devices) { return listAsDevice(listDecoders, user, devices); },
      options);
}

}

namespace sdk = bitoscope::plugin_c_sdk;

extern "C" {

int bitoscope_run_decoder_plugin(int argc, char** argv, bitoscope_list_decoders_fn list_decoders, void* user) {
  try {
    std::vector<std::string> args;
    for (int i = 1; argv != nullptr && i < argc; ++i) args.emplace_back(argv[i] != nullptr ? argv[i] : "");
    // WHY: Take over stdin and stdout BEFORE any plugin code runs: they carry the protocol.
    auto stdio = sdk::takeStdio(2);
    if (!stdio) {
      std::fprintf(stderr, "bitoscope plugin: cannot take stdin and stdout for host: %s\n", stdio.error().c_str());
      return sdk::kExitFailed;
    }
    return sdk::runDecoderPlugin(args, *stdio->in, *stdio->out, list_decoders, user);
  } catch (const std::exception& e) {
    std::fprintf(stderr, "bitoscope plugin: %s\n", e.what());
    return sdk::kExitFailed;
  } catch (...) {
    return sdk::kExitFailed;
  }
}

int bitoscope_add_decoder_sized(bitoscope_decoders* decoders, const bitoscope_decoder* decoder, size_t decoder_size) {
  try {
    if (decoders == nullptr || decoder == nullptr) return BITOSCOPE_ERROR;
    const auto refuse = [decoders](std::string why) {
      std::string line = "bitoscope_add_decoder: " + std::move(why);
      sdk::logLine(bitoscope::ipc::LogLevel::error, line);
      decoders->refusals.push_back(std::move(line));
      return BITOSCOPE_ERROR;
    };
    auto info = sdk::declarationOf(decoder, decoder_size);
    if (!info) return refuse(std::move(info.error()));
    for (const sdk::DecoderRegistration& d : decoders->decoders) {
      if (d.info.name == info->name) return refuse("decoder \"" + info->name + "\" is declared already");
    }
    bitoscope_decoder d{};
    std::memcpy(&d, decoder, (std::min)(decoder_size, sizeof d));
    decoders->decoders.push_back(sdk::DecoderRegistration{std::move(*info), d.decode, d.check, d.user});
    return BITOSCOPE_OK;
  } catch (...) {
    return BITOSCOPE_ERROR;
  }
}

int bitoscope_set_decoder_plugin_version(bitoscope_decoders* decoders, const char* version) {
  try {
    if (decoders == nullptr) return BITOSCOPE_ERROR;
    decoders->pluginVersion = version != nullptr ? std::string(version) : std::string();
    return BITOSCOPE_OK;
  } catch (...) {
    return BITOSCOPE_ERROR;
  }
}

int bitoscope_set_decoder_plugin_runs_scripts(bitoscope_decoders* decoders, int runs_scripts) {
  if (decoders == nullptr) return BITOSCOPE_ERROR;
  decoders->runsScripts = runs_scripts != 0;
  return BITOSCOPE_OK;
}

int bitoscope_find_job_channel(const bitoscope_decode_job* job, const char* name, uint32_t* input_channel) {
  if (job == nullptr || name == nullptr || input_channel == nullptr) return BITOSCOPE_ERROR;
  for (size_t i = 0; i < job->num_channels; ++i) {
    if (job->channels[i].name != nullptr && std::strcmp(job->channels[i].name, name) == 0) {
      *input_channel = job->channels[i].input_channel;
      return BITOSCOPE_OK;
    }
  }
  return BITOSCOPE_NONE;
}

int bitoscope_output_wanted(const bitoscope_decode_job* job, const char* name, uint32_t* index) {
  if (job == nullptr || name == nullptr || index == nullptr) return BITOSCOPE_ERROR;
  for (size_t i = 0; job->outputs != nullptr && i < job->num_outputs; ++i) {
    if (job->outputs[i].name != nullptr && std::strcmp(job->outputs[i].name, name) == 0) {
      *index = static_cast<uint32_t>(i);
      return job->outputs[i].wanted != 0 ? BITOSCOPE_OK : BITOSCOPE_NONE;
    }
  }
  return BITOSCOPE_ERROR;
}

const char* bitoscope_find_job_option(const bitoscope_decode_job* job, const char* name) {
  if (job == nullptr || name == nullptr) return nullptr;
  for (size_t i = 0; i < job->num_options; ++i) {
    if (job->options[i].name != nullptr && std::strcmp(job->options[i].name, name) == 0) return job->options[i].value;
  }
  return nullptr;
}

int bitoscope_find_job_option_int(const bitoscope_decode_job* job, const char* name, int64_t* value) {
  if (value == nullptr) return BITOSCOPE_ERROR;
  const char* text = bitoscope_find_job_option(job, name);
  if (text == nullptr) return job == nullptr || name == nullptr ? BITOSCOPE_ERROR : BITOSCOPE_NONE;
  int64_t parsed = 0;
  const char* end = text + std::strlen(text);
  const auto r = std::from_chars(text, end, parsed);
  if (text == end || r.ec != std::errc() || r.ptr != end) return BITOSCOPE_ERROR;
  *value = parsed;
  return BITOSCOPE_OK;
}

int bitoscope_input_bound(const bitoscope_decode_job* job, const char* name, uint32_t* index) {
  if (job == nullptr || name == nullptr || index == nullptr) return BITOSCOPE_ERROR;
  for (size_t i = 0; job->inputs != nullptr && i < job->num_inputs; ++i) {
    if (job->inputs[i].name != nullptr && std::strcmp(job->inputs[i].name, name) == 0) {
      *index = static_cast<uint32_t>(i);
      return job->inputs[i].bound != 0 ? BITOSCOPE_OK : BITOSCOPE_NONE;
    }
  }
  return BITOSCOPE_ERROR;
}

}

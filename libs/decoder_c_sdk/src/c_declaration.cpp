/*
 * SPDX-License-Identifier: MIT
 */

#include "c_declaration.h"

#include <string>
#include <utility>

namespace bitoscope::plugin_c_sdk {

namespace {

const char* textOrNone(const std::string& text) { return text.empty() ? nullptr : text.c_str(); }

template <typename T>
const T* dataOrNone(const std::vector<T>& values) {
  return values.empty() ? nullptr : values.data();
}

}

CDeclaration::CDeclaration(plugin::DecoderDeclaration declaration, bitoscope_decode_fn decode, bitoscope_check_fn check,
                           void* user)
    : declaration_(std::move(declaration)) {
  const plugin::DecoderDeclaration& d = declaration_;
  for (const plugin::DecoderChannel& c : d.channels) {
    channels_.push_back({c.name.c_str(), c.description.c_str(), c.optional ? 1 : 0, c.display.c_str()});
  }
  optionTexts_.reserve(d.options.size() * 2);
  for (const plugin::DecoderOption& o : d.options) {
    auto& allowed = optionTexts_.emplace_back();
    for (const std::string& v : o.allowedValues) allowed.push_back(v.c_str());
    auto& unsupported = optionTexts_.emplace_back();
    for (const std::string& v : o.unsupportedValues) unsupported.push_back(v.c_str());
    options_.push_back({o.name.c_str(), o.description.c_str(), static_cast<int>(o.type), o.defaultValue.c_str(),
                        dataOrNone(allowed), allowed.size(), o.min ? 1 : 0, o.min.value_or(0), o.max ? 1 : 0,
                        o.max.value_or(0), dataOrNone(unsupported), unsupported.size(), o.display.c_str()});
  }
  for (const std::string& id : d.unsupportedOptions) unsupportedOptions_.push_back(id.c_str());
  for (const std::string& id : d.protocols) protocols_.push_back(id.c_str());
  for (const plugin::AnnotationClass& k : d.classes) {
    classes_.push_back({k.id, k.name.c_str(), static_cast<int>(k.format), k.hexDigits, textOrNone(k.hexDigitsOption),
                        static_cast<int>(k.role)});
  }
  for (const plugin::AnnotationRow& r : d.rows) rows_.push_back({r.id, r.name.c_str()});
  for (const plugin::DecoderOutput& o : d.outputs) {
    outputs_.push_back({o.name.c_str(), o.description.c_str(), static_cast<int>(o.type), o.width,
                        textOrNone(o.widthOption), textOrNone(o.requiresChannel), o.display.c_str(),
                        textOrNone(o.carries), textOrNone(o.carriesInput)});
  }
  for (const plugin::DecoderInput& i : d.inputs) {
    inputs_.push_back({i.name.c_str(), i.description.c_str(), static_cast<int>(i.type), i.width, i.optional ? 1 : 0,
                       i.display.c_str()});
  }
  for (const plugin::RowSource& s : d.rowSources) {
    rowSources_.push_back({s.row, s.cls, s.sourceRow, dataOrNone(s.sourceClasses), s.sourceClasses.size(),
                           s.enableOption.c_str(), static_cast<int>(s.rule),
                           {s.grid.rateOption.c_str(), s.grid.offsetBits, s.grid.countOption.c_str(),
                            s.grid.orderOption.c_str()},
                           {s.edges.clockChannel.c_str(), s.edges.dataChannel.c_str(), s.edges.countOption.c_str(),
                            s.edges.count}});
  }

  decoder_.name = d.name.c_str();
  decoder_.decode = decode;
  decoder_.channels = dataOrNone(channels_);
  decoder_.num_channels = channels_.size();
  decoder_.classes = dataOrNone(classes_);
  decoder_.num_classes = classes_.size();
  decoder_.rows = dataOrNone(rows_);
  decoder_.num_rows = rows_.size();
  decoder_.version = d.version.c_str();
  decoder_.options = dataOrNone(options_);
  decoder_.num_options = options_.size();
  decoder_.unsupported_options = dataOrNone(unsupportedOptions_);
  decoder_.num_unsupported_options = unsupportedOptions_.size();
  decoder_.allows_shared_channels = d.allowsSharedChannels ? 1 : 0;
  decoder_.check = check;
  decoder_.user = user;
  decoder_.outputs = dataOrNone(outputs_);
  decoder_.num_outputs = outputs_.size();
  decoder_.inputs = dataOrNone(inputs_);
  decoder_.num_inputs = inputs_.size();
  decoder_.display = d.display.c_str();
  decoder_.protocols = dataOrNone(protocols_);
  decoder_.num_protocols = protocols_.size();
  decoder_.row_sources = dataOrNone(rowSources_);
  decoder_.num_row_sources = rowSources_.size();
}

}

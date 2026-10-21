/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "color_choice.h"
#include "decoder.h"
#include "decoder_registry.h"
#include "decoder_visibility.h"
#include "number_format.h"

namespace bitoscope::gui_decode {

struct CapturedChannel {
  std::uint32_t deviceChannel = 0;
  std::string name;
};

std::string channelFieldKey(std::string_view channelName);
std::string inputFieldKey(std::string_view inputName);
std::string optionFieldKey(std::string_view optionName);
std::string idFieldKey();

class DecoderConfig {
 public:
  explicit DecoderConfig(std::string decoderName);
  DecoderConfig(std::string decoderName, std::string id);

  const std::string& decoderName() const { return decoderName_; }

  const std::string& id() const { return id_; }
  bool idExplicit() const { return idExplicit_; }
  void setId(std::string id) {
    id_ = std::move(id);
    idExplicit_ = true;
  }
  void setGeneratedId(std::string id) {
    id_ = std::move(id);
    idExplicit_ = false;
  }

  std::optional<std::string> inputBinding(std::string_view inputName) const;
  void setInputBinding(std::string_view inputName, std::optional<std::string> binding);
  const std::map<std::string, std::string, std::less<>>& inputBindings() const { return inputBindings_; }

  struct LoadedFrom {
    std::size_t entry = 0;
    std::string specText;
    bool operator==(const LoadedFrom&) const = default;
  };
  const std::optional<LoadedFrom>& loadedFrom() const { return loadedFrom_; }
  void setLoadedFrom(std::optional<LoadedFrom> from) { loadedFrom_ = std::move(from); }

  const std::optional<waveform_view::ColorChoice>& color() const { return color_; }
  void setColor(std::optional<waveform_view::ColorChoice> color) { color_ = color; }
  std::optional<waveform_view::ColorChoice> classColor(std::uint32_t rowId, std::uint32_t classId) const;
  void setClassColor(std::uint32_t rowId, std::uint32_t classId, std::optional<waveform_view::ColorChoice> color);
  const std::map<waveform_view::RowClass, waveform_view::ColorChoice>& classColors() const { return classColors_; }

  bitoscope::decode::DataFormat rowDataFormat(std::uint32_t rowId) const;
  bool setRowDataFormat(std::uint32_t rowId, bitoscope::decode::DataFormat format);
  const std::map<std::uint32_t, bitoscope::decode::DataFormat>& rowDataFormats() const { return rowDataFormats_; }

  const waveform_view::DecoderVisibility& visibility() const { return visibility_; }
  waveform_view::DecoderVisibility& visibility() { return visibility_; }

  std::optional<std::uint32_t> channelBinding(std::string_view channelName) const;
  std::vector<std::uint32_t> boundDeviceChannels() const;
  void setChannelBinding(std::string_view channelName, std::optional<std::uint32_t> deviceChannel);

  std::string optionValue(std::string_view optionName, const bitoscope::decode::OptionSpec& spec) const;
  void setOptionValue(std::string_view optionName, std::string value);

  std::expected<std::unique_ptr<bitoscope::decode::Decoder>, std::string> validate(
      const bitoscope::decode::DecoderRegistry& registry, std::span<const CapturedChannel> captured);

  std::optional<std::string> errorFor(std::string_view fieldKey) const;

  void recordError(const std::string& message, std::string_view why, const bitoscope::decode::Decoder* decoder);
  void clearErrors() { errors_.clear(); }

  std::string toSpecString(const bitoscope::decode::Decoder& decoder, std::span<const DecoderConfig> list) const;
  std::string toSpecString(const bitoscope::decode::Decoder& decoder) const;
  bool writesId(std::span<const DecoderConfig> list) const;

  // WHY: Built from declarations, not from a running decoder: that one is gone once its DecoderRunner ends.
  struct AnnotationFormatContext {
    std::vector<bitoscope::decode::AnnotationClass> classes;
    bitoscope::decode::OptionMap options;
  };
  AnnotationFormatContext annotationFormatContext(const bitoscope::decode::DecoderRegistry& registry) const;

 private:
  std::string decoderName_;
  std::string id_;
  bool idExplicit_ = false;
  std::optional<LoadedFrom> loadedFrom_;
  std::map<std::string, std::uint32_t, std::less<>> channelBindings_;
  std::map<std::string, std::string, std::less<>> inputBindings_;
  std::map<std::string, std::string, std::less<>> optionValues_;
  std::map<std::string, std::string, std::less<>> errors_;
  std::optional<waveform_view::ColorChoice> color_;
  std::map<waveform_view::RowClass, waveform_view::ColorChoice> classColors_;
  std::map<std::uint32_t, bitoscope::decode::DataFormat> rowDataFormats_;
  waveform_view::DecoderVisibility visibility_;
};

std::string uniqueDecoderId(std::string_view decoderName, std::span<const DecoderConfig> configs);

std::expected<DecoderConfig, std::string> decoderConfigFromSetup(
    const bitoscope::decode::DecoderRegistry& registry, const std::string& name, const std::string& specRest,
    std::span<DecoderConfig> earlier = {}, std::span<const CapturedChannel> captured = {});

}

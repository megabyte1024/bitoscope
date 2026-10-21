/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <expected>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "decoder.h"
#include "ipc_decoder.h"

namespace bitoscope::plugin_c_sdk {

std::optional<decode::StreamType> streamTypeOf(ipc::decoder::StreamType type);

decode::ValueFormat valueFormatOf(const ipc::decoder::ValueFormat& format);

class DeclaredDecoder final : public decode::Decoder {
 public:
  using Check = std::function<std::expected<void, std::string>()>;

  explicit DeclaredDecoder(const ipc::decoder::DecoderInfo& info, Check check = {});

  std::string name() const override;
  std::string display() const override;
  std::vector<decode::ChannelSpec> channels() const override;
  std::vector<decode::OptionSpec> options() const override;
  std::vector<std::string> unsupportedOptions() const override;
  std::vector<std::string> protocols() const override;
  bool allowsSharedChannels() const override;
  std::vector<decode::AnnotationClass> annotationClasses() const override;
  std::vector<decode::AnnotationRow> rows() const override;
  std::vector<decode::RowSource> rowSources() const override;
  std::vector<decode::OutputSpec> outputs() const override;
  std::vector<decode::InputSpec> inputs() const override;

 protected:
  std::expected<void, std::string> checkCaptureFacts(const decode::CaptureFacts&) const override;
  std::expected<void, std::string> run(const decode::SignalView&, decode::AnnotationSink&) override;

 private:
  const ipc::decoder::DecoderInfo& info_;
  Check check_;
};

}

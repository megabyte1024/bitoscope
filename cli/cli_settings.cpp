/*
 * SPDX-License-Identifier: MIT
 */

#include "cli_settings.h"

#include <bit>
#include <ostream>
#include <variant>

#include "device_properties.h"
#include "ipc_la_acquisition.h"

namespace bitoscope::cli {

namespace {

const std::vector<ipc::props::PropertyNode>& analyzerTree(const host::pipe::PluginDevice& session) {
  static const std::vector<ipc::props::PropertyNode> kNone;
  for (const ipc::FacetInfo& facet : session.device.facets) {
    if (facet.name == ipc::la::kFacetName) return facet.properties;
  }
  return kNone;
}

std::string ceilingRateText(uint64_t milli_hz) {
  const ipc::props::Property rate{"rate", "rate", "", false, ipc::props::Unit::hertz,
                                  ipc::props::DecimalSpec{3, ipc::props::Unbounded{}, ipc::props::Unbounded{}, 0}};
  auto text = device_properties::formatValue(rate, ipc::props::PropertyValue{"rate", static_cast<int64_t>(milli_hz)});
  return text ? *text : std::to_string(milli_hz) + " mHz";
}

}

std::expected<std::vector<ipc::props::PropertyValue>, std::string> readAnalyzerSettings(
    const std::map<std::string, std::string>& settings, const host::pipe::PluginDevice& session, std::ostream* notes) {
  std::map<std::string, std::string> current = settings;
  if (session.plugin_id.starts_with("demo/") && !current.contains("layout")) {
    if (auto old = current.extract("encoding"); !old.empty()) {
      current.emplace("layout", std::move(old.mapped()));
      if (notes != nullptr) *notes << "bitoscope_cli: setting 'encoding' is now named 'layout'; old name still works\n";
    }
  }
  auto values = device_properties::readSettings(analyzerTree(session), current);
  if (!values) {
    return std::unexpected("settings for '" + session.plugin_id + "' were refused - nothing was started: " +
                           values.error());
  }
  return std::move(*values);
}

std::string describeAnalyzerSettings(const std::vector<ipc::props::PropertyValue>& values,
                                     const host::pipe::PluginDevice& session) {
  const auto& tree = analyzerTree(session);
  std::string line;
  for (const ipc::props::PropertyValue& value : values) {
    std::string text = "?";
    if (const auto* property = device_properties::findProperty(tree, value.id)) {
      if (auto formatted = device_properties::formatValue(*property, value)) text = std::move(*formatted);
    }
    line += (line.empty() ? "" : " ") + value.id + "=" + text;
  }
  return line;
}

std::expected<void, std::string> checkRateCeiling(uint64_t sample_rate_milli_hz, uint32_t channel_mask,
                                                   const host::pipe::LaCapabilities& capabilities) {
  const auto& ceilings = capabilities.rate_ceilings_milli_hz;
  if (ceilings.empty()) return {};
  const auto selected = static_cast<size_t>(std::popcount(channel_mask));
  if (selected < 1 || selected > ceilings.size()) return {};
  if (sample_rate_milli_hz > ceilings[selected - 1]) {
    return std::unexpected(std::to_string(selected) + " channels: at most " + ceilingRateText(ceilings[selected - 1]) +
                           "; " + ceilingRateText(sample_rate_milli_hz) + " was asked for");
  }
  return {};
}

}

/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <cstdint>
#include <atomic>
#include <expected>
#include <filesystem>
#include <functional>
#include <map>
#include <optional>
#include <ostream>
#include <span>
#include <string>
#include <system_error>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

#include "capture_writer.h"
#include "cli_acquire.h"
#include "cli_decode.h"
#include "device_properties.h"
#include "ipc_la_acquisition.h"
#include "ipc_properties.h"
#include "plugin_decoder.h"

namespace bitoscope::cli {

namespace save_detail {

inline std::optional<bitoscope::capture::SavedPropertyValue> exactValue(
    const ipc::props::Property& property, const ipc::props::PropertyValue& value) {
  return std::visit(
      [&](const auto& spec) -> std::optional<bitoscope::capture::SavedPropertyValue> {
        using T = std::decay_t<decltype(spec)>;
        if constexpr (std::is_same_v<T, ipc::props::IntegerSpec>) {
          const auto* v = std::get_if<int64_t>(&value.value);
          if (v == nullptr) return std::nullopt;
          return bitoscope::capture::SavedIntegerValue{*v, 0, std::string(device_properties::unitSymbol(property.unit))};
        } else if constexpr (std::is_same_v<T, ipc::props::DecimalSpec>) {
          const auto* v = std::get_if<int64_t>(&value.value);
          if (v == nullptr) return std::nullopt;
          return bitoscope::capture::SavedIntegerValue{*v, spec.digits, std::string(device_properties::unitSymbol(property.unit))};
        } else if constexpr (std::is_same_v<T, ipc::props::BooleanSpec>) {
          const auto* v = std::get_if<bool>(&value.value);
          if (v == nullptr) return std::nullopt;
          return bitoscope::capture::SavedPropertyValue{*v};
        } else if constexpr (std::is_same_v<T, ipc::props::EnumerationSpec>) {
          const auto* idx = std::get_if<ipc::props::EnumIndex>(&value.value);
          if (idx == nullptr || idx->index >= spec.items.size()) return std::nullopt;
          const auto& item = spec.items[idx->index];
          bitoscope::capture::SavedEnumValue out;
          if (const auto* s = std::get_if<std::string>(&item)) {
            out.name = *s;
          } else if (const auto* n = std::get_if<int64_t>(&item)) {
            out.number = *n;
            out.digits = spec.digits;
            out.unit = std::string(device_properties::unitSymbol(property.unit));
          } else {
            return std::nullopt;
          }
          return bitoscope::capture::SavedPropertyValue{std::move(out)};
        } else {
          return std::nullopt;
        }
      },
      property.type);
}

// WHY: Record only analyzer properties actually sent: other facets' defaults were once saved as values in use.
inline std::vector<bitoscope::capture::SavedProperty> capturedDeviceProperties(
    std::span<const ipc::props::PropertyValue> sent_properties,
    const bitoscope::host::pipe::DriverDevice& session) {
  std::map<std::string, ipc::props::PropertyValue> analyzer_set;
  for (const ipc::props::PropertyValue& v : sent_properties) analyzer_set.emplace(v.id, v);

  std::vector<bitoscope::capture::SavedProperty> out;
  for (const ipc::FacetInfo& facet : session.device.facets) {
    if (facet.name != ipc::la::kFacetName) continue;
    for (const auto& listing : device_properties::listProperties(facet.properties)) {
      const ipc::props::Property& property = *listing.property;
      if (property.changeableDuringCapture) continue;

      ipc::props::PropertyValue value;
      if (analyzer_set.contains(property.id)) {
        value = analyzer_set.at(property.id);
      } else {
        auto def = device_properties::effectiveDefault(property);
        if (!def.has_value()) continue;
        value = std::move(*def);
      }

      auto formatted = device_properties::formatValue(property, value);
      if (!formatted.has_value()) continue;
      auto exact = exactValue(property, value);
      if (!exact.has_value()) continue;

      bitoscope::capture::SavedProperty saved;
      saved.facet = facet.name;
      saved.id = property.id;
      saved.name = property.name;
      saved.formatted = std::move(*formatted);
      saved.value = std::move(*exact);
      out.push_back(std::move(saved));
    }
  }
  return out;
}

}

inline std::expected<void, std::string> finishAcquisition(const CliOptions& opts,
                                                          const acquisition::AcquireResult& result,
                                                          const bitoscope::host::pipe::DriverDevice& session,
                                                          bitoscope::capture::CaptureWriter* writer,
                                                          std::ostream& out,
                                                          std::span<ConfiguredDecoder> decoders = {},
                                                          const std::atomic<bool>* decode_cancel = nullptr,
                                                          std::function<void(uint64_t)> before_collect = {}) {
  namespace pd = bitoscope::capture::plan_detail;
  const bitoscope::capture::RawCaptureBuffer& buffer = result.session->buffer();
  const bitoscope::capture::CapturePlan& plan = buffer.plan();
  const std::string figures =
      pd::withThousands(result.summary.stored_samples) + " samples on " +
      std::to_string(plan.channels()) + " channel" + (plan.channels() == 1 ? "" : "s") + " (" +
      pd::formatDuration(result.summary.stored_samples, plan.sampleRateMilliHz()) + ")";

  const bitoscope::capture::CaptureExtent extent = result.session->captureExtent();

  // WHY: Report decoder failure AFTER save, so a failing decoder does not lose capture.
  const bitoscope::capture::ExtentTail tail =
      result.session->triggerOrigin().has_value()
          ? bitoscope::capture::ExtentTail::InsideLastWindow
          : bitoscope::capture::ExtentTail::Exact;
  std::string decode_error;
  auto filter = resolveAnnotationFilter(opts.annotation_classes, decoders);
  if (!filter) return std::unexpected(filter.error());
  if (auto decoded = runDecoders(decoders, buffer, extent, out, tail, nullptr, before_collect, &*filter); !decoded) {
    decode_error = decoded.error();
  }
  // WHY: Ctrl+C during decoding saves nothing, even though capture is intact: user's stop outranks, owner decision.
  if (!decoders.empty() && decode_cancel != nullptr && decode_cancel->load(std::memory_order_relaxed)) {
    return std::unexpected("interrupted (Ctrl+C) while decoding; nothing was saved" +
                           (decode_error.empty() ? std::string() : ": " + decode_error));
  }
  const auto withDecodeError = [&decode_error](std::string message) {
    return decode_error.empty() ? message : message + "; " + decode_error;
  };

  if (writer == nullptr) {
    out << "captured " << figures << "; not saved (no -o output file given)" << std::endl;
    if (!decode_error.empty()) {
      return std::unexpected(decode_error + "; capture itself was not saved (no -o output file given)");
    }
    return {};
  }

  bitoscope::capture::CaptureMetadata metadata;
  for (const Channel& channel : opts.channels) {
    metadata.channels.push_back({channel.index, channel.name});
  }
  metadata.decoder_setup = decoderSetup(decoders);
  for (const ConfiguredDecoder& entry : decoders) {
    metadata.decoder_versions.push_back(bitoscope::decoder_host::savedVersionsOf(*entry.decoder));
  }
  metadata.capture_id = result.summary.capture_id;
  metadata.origin_sample_index = result.session->triggerOrigin();
  metadata.trigger_sample_index = result.session->triggerSampleIndex();
  metadata.requested_samples = result.requested_samples;
  metadata.requested_pre_trigger_samples = result.requested_pre_trigger_samples;
  metadata.requested_post_trigger_samples = result.requested_post_trigger_samples;
  metadata.memory_shortfall = result.memory_shortfall;
  metadata.extent = extent;
  metadata.device = result.device;
  metadata.device_properties = save_detail::capturedDeviceProperties(result.properties, session);
  if (auto written = writer->write(buffer, metadata); !written) {
    return std::unexpected(withDecodeError(
        "capture of " + figures + " could not be saved to '" + opts.output_file +
        "': " + written.error() +
        "; nothing was saved and capture is lost with this process"));
  }

  out << "saved " << figures << " to '" << opts.output_file << "'";
  if (result.memory_shortfall) {
    out << " - SHORT: " << pd::withThousands(result.memory_shortfall->stored_samples) << " of "
        << pd::withThousands(result.memory_shortfall->requested_samples)
        << " samples, rest had no memory; file records this";
  }
  std::error_code ec;
  const auto bytes = std::filesystem::file_size(std::filesystem::path(opts.output_file), ec);
  if (!ec) out << " (" << pd::withThousands(bytes) << " bytes)";
  out << std::endl;
  if (!decode_error.empty()) {
    return std::unexpected(decode_error + "; capture was saved to '" + opts.output_file + "'");
  }
  return {};
}

}

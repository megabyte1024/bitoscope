/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <cstdint>
#include <expected>
#include <string>
#include <string_view>
#include <vector>

#include "facet_client.h"
#include "ipc_messages.h"
#include "property_editor_model.h"

namespace bitoscope::gui_properties {

struct OutputLeftUnknown {
  uint32_t output = 0;
  std::string reason;
  bool operator==(const OutputLeftUnknown&) const = default;
};

struct GeneratorSwitchOff {
  std::string facet;
  uint32_t outputs = 0;
  std::vector<OutputLeftUnknown> leftUnknown;
  std::string unreadable;
  bool allOff() const { return leftUnknown.empty() && unreadable.empty(); }
  bool operator==(const GeneratorSwitchOff&) const = default;
};

std::vector<GeneratorSwitchOff> switchGeneratorOutputsOff(const ipc::DeviceInfo& device,
                                                          host::FacetTransport& transport, std::string_view pluginId);

void markLeftUnknown(DevicePropertiesModel& model, const std::vector<GeneratorSwitchOff>& switchedOff);

std::expected<void, std::string> switchOutputOffFromPanel(GeneratorSwitchOff& generator, FacetPropertiesModel& model,
                                                          std::string_view propertyId, bool capturing,
                                                          host::FacetTransport& transport, std::string_view pluginId);

std::expected<void, std::string> resetOutputFromPanel(GeneratorSwitchOff& generator, FacetPropertiesModel& model,
                                                      std::string_view propertyId, bool capturing,
                                                      host::FacetTransport& transport, std::string_view pluginId);

std::expected<void, std::string> resetGeneratorFromPanel(GeneratorSwitchOff& generator, FacetPropertiesModel& model,
                                                         bool capturing, host::FacetTransport& transport,
                                                         std::string_view pluginId);

struct OpenedDeviceProperties {
  DevicePropertiesModel model;
  std::vector<GeneratorSwitchOff> generators;
};

OpenedDeviceProperties openDeviceProperties(const ipc::DeviceInfo& device, host::FacetTransport& transport,
                                            std::string_view pluginId);

}

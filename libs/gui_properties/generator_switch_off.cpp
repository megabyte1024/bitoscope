/*
 * SPDX-License-Identifier: MIT
 */

#include "generator_switch_off.h"

#include <utility>

#include "generator_facet.h"
#include "siggen_pipe_client.h"

namespace bitoscope::gui_properties {

namespace {

constexpr uint32_t kMostOutputs = 32;

GeneratorSwitchOff switchFacetOff(const ipc::FacetInfo& facet, host::FacetTransport& transport,
                                  std::string_view driverDeviceId) {
  GeneratorSwitchOff result;
  result.facet = facet.name;
  auto capabilities = host::pipe::SiggenClient::parseCapabilities(facet.info);
  if (!capabilities) {
    result.unreadable = capabilities.error();
    return result;
  }
  result.outputs = capabilities->num_ports;
  if (result.outputs > kMostOutputs) {
    result.unreadable = "generator lists " + std::to_string(result.outputs) + " outputs, at most " +
                        std::to_string(kMostOutputs) + " are possible";
    return result;
  }
  host::pipe::SiggenClient client(host::FacetClient(transport, std::string(driverDeviceId), facet.name),
                                  std::move(*capabilities));
  for (uint32_t output = 0; output < result.outputs; ++output) {
    if (auto stopped = client.stop(uint32_t{1} << output); !stopped) {
      result.leftUnknown.push_back(OutputLeftUnknown{output, std::move(stopped.error())});
    }
  }
  return result;
}

std::expected<void, std::string> stopOutputsLeftUnknown(GeneratorSwitchOff& generator, host::FacetTransport& transport,
                                                        std::string_view driverDeviceId) {
  if (!generator.unreadable.empty()) {
    return std::unexpected(generator.unreadable);
  }
  host::pipe::SiggenCapabilities capabilities;
  capabilities.num_ports = generator.outputs;
  host::pipe::SiggenClient client(host::FacetClient(transport, std::string(driverDeviceId), generator.facet),
                                  std::move(capabilities));
  std::vector<OutputLeftUnknown> stillUnknown;
  std::string refusals;
  for (const OutputLeftUnknown& output : generator.leftUnknown) {
    if (auto stopped = client.stop(uint32_t{1} << output.output); !stopped) {
      refusals += (refusals.empty() ? "" : "; ") + std::string("output ") + std::to_string(output.output) + ": " +
                  stopped.error();
      stillUnknown.push_back(OutputLeftUnknown{output.output, std::move(stopped.error())});
    }
  }
  generator.leftUnknown = std::move(stillUnknown);
  if (!generator.leftUnknown.empty()) {
    return std::unexpected(refusals);
  }
  return {};
}

std::expected<void, std::string> stopOutputsLeftUnknownUnlessCapturing(GeneratorSwitchOff& generator, bool capturing,
                                                                       host::FacetTransport& transport,
                                                                       std::string_view driverDeviceId) {
  // WHY: A generator command sent while its device captures has wedged real hardware: never send one then.
  if (capturing) {
    return std::unexpected(std::string("stop command is not sent while device captures, so state stays unknown"));
  }
  return stopOutputsLeftUnknown(generator, transport, driverDeviceId);
}

}

std::vector<GeneratorSwitchOff> switchGeneratorOutputsOff(const ipc::DeviceInfo& device,
                                                          host::FacetTransport& transport, std::string_view driverDeviceId) {
  std::vector<GeneratorSwitchOff> results;
  for (const ipc::FacetInfo& facet : device.facets) {
    if (isGeneratorFacet(facet.name)) {
      results.push_back(switchFacetOff(facet, transport, driverDeviceId));
    }
  }
  return results;
}

void markLeftUnknown(DevicePropertiesModel& model, const std::vector<GeneratorSwitchOff>& switchedOff) {
  for (const GeneratorSwitchOff& generator : switchedOff) {
    if (generator.allOff()) {
      continue;
    }
    if (FacetPropertiesModel* facet = model.facet(generator.facet); facet != nullptr) {
      facet->markBooleansUnknown();
    }
  }
}

std::expected<void, std::string> switchOutputOffFromPanel(GeneratorSwitchOff& generator, FacetPropertiesModel& model,
                                                          std::string_view propertyId, bool capturing,
                                                          host::FacetTransport& transport, std::string_view driverDeviceId) {
  if (!model.isStateUnknown(propertyId)) {
    return model.setBoolean(propertyId, false, capturing);
  }
  if (auto stopped = stopOutputsLeftUnknown(generator, transport, driverDeviceId); !stopped) {
    return stopped;
  }
  model.endUnknownState();
  return model.setBoolean(propertyId, false, capturing);
}

std::expected<void, std::string> resetOutputFromPanel(GeneratorSwitchOff& generator, FacetPropertiesModel& model,
                                                      std::string_view propertyId, bool capturing,
                                                      host::FacetTransport& transport, std::string_view driverDeviceId) {
  if (model.isStateUnknown(propertyId)) {
    if (auto stopped = stopOutputsLeftUnknownUnlessCapturing(generator, capturing, transport, driverDeviceId); !stopped) {
      return stopped;
    }
    model.endUnknownState();
  }
  model.resetToDefault(propertyId);
  return {};
}

std::expected<void, std::string> resetGeneratorFromPanel(GeneratorSwitchOff& generator, FacetPropertiesModel& model,
                                                         bool capturing, host::FacetTransport& transport,
                                                         std::string_view driverDeviceId) {
  if (model.anyStateUnknown()) {
    if (auto stopped = stopOutputsLeftUnknownUnlessCapturing(generator, capturing, transport, driverDeviceId); !stopped) {
      return stopped;
    }
  }
  model.resetAllToDefault();
  return {};
}

OpenedDeviceProperties openDeviceProperties(const ipc::DeviceInfo& device, host::FacetTransport& transport,
                                            std::string_view driverDeviceId) {
  std::vector<GeneratorSwitchOff> generators = switchGeneratorOutputsOff(device, transport, driverDeviceId);
  OpenedDeviceProperties opened{DevicePropertiesModel(device), std::move(generators)};
  markLeftUnknown(opened.model, opened.generators);
  return opened;
}

}

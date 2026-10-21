/*
 * SPDX-License-Identifier: MIT
 */

#include "facet_access.h"

#include <algorithm>
#include <utility>
#include <vector>

namespace bitoscope::acquisition {

namespace {

std::string joinSorted(std::vector<std::string> names) {
  std::sort(names.begin(), names.end());
  std::string joined;
  for (const std::string& name : names) {
    if (!joined.empty()) {
      joined += ", ";
    }
    joined += name;
  }
  return joined;
}

}

std::expected<const ipc::FacetInfo*, std::string> findFacet(const host::pipe::PluginDevice& session,
                                                            std::string_view name) {
  std::vector<std::string> advertised;
  for (const ipc::FacetInfo& facet : session.device.facets) {
    if (facet.name == name) {
      if (!facet.unusable.empty()) {
        return std::unexpected("driver '" + session.plugin_id + "' has '" + std::string(name) +
                               "' facet this host cannot use: " + facet.unusable);
      }
      return &facet;
    }
    advertised.push_back(facet.name);
  }
  return std::unexpected("driver '" + session.plugin_id + "' has no '" + std::string(name) +
                          "' facet (" +
                          (advertised.empty() ? "it advertises none"
                                              : "it advertises: " + joinSorted(advertised)) +
                          ")");
}

std::expected<host::pipe::LaAcquisitionClient, std::string> makeLaClient(const host::pipe::PluginDevice& session,
                                                                         host::FacetTransport& transport,
                                                                         FacetVersion required) {
  const auto facet = findFacet(session, host::pipe::kLaAcquisitionFacetName);
  if (!facet.has_value()) {
    return std::unexpected(facet.error());
  }
  const FacetVersion advertised{(*facet)->version.major, (*facet)->version.minor};
  if (advertised.major < required.major ||
      (advertised.major == required.major && advertised.minor < required.minor)) {
    return std::unexpected("driver '" + session.plugin_id + "' advertises " +
                            std::string(host::pipe::kLaAcquisitionFacetName) + " version " +
                            std::to_string(advertised.major) + "." +
                            std::to_string(advertised.minor) + ", but this host needs at least " +
                            std::to_string(required.major) + "." + std::to_string(required.minor) +
                            " (version that echoes capture id)");
  }
  auto caps = host::pipe::LaAcquisitionClient::parseCapabilities((*facet)->info);
  if (!caps.has_value()) {
    return std::unexpected("driver '" + session.plugin_id + "': " + caps.error());
  }
  return host::pipe::LaAcquisitionClient(
      host::FacetClient(transport, session.plugin_id, std::string(host::pipe::kLaAcquisitionFacetName)),
      std::move(*caps));
}

std::expected<void, std::string> requireBulkRing(const host::pipe::PluginDevice& session,
                                                 const host::pipe::LaAcquisitionClient& la) {
  if (la.capabilities().bulk_ring) return {};
  return std::unexpected("driver '" + session.plugin_id + "' cannot deliver samples: its " +
                         std::string(host::pipe::kLaAcquisitionFacetName) +
                         " facet does not write bulk ring (LaAcquisitionInfo.bulk_ring), and samples "
                         "travel no other way; nothing was started");
}

std::expected<host::pipe::SiggenClient, std::string> makeSiggenClient(const host::pipe::PluginDevice& session,
                                                                      host::FacetTransport& transport) {
  const auto facet = findFacet(session, host::pipe::kSiggenFacetName);
  if (!facet.has_value()) {
    return std::unexpected(facet.error());
  }
  auto caps = host::pipe::SiggenClient::parseCapabilities((*facet)->info);
  if (!caps.has_value()) {
    return std::unexpected("driver '" + session.plugin_id + "': " + caps.error());
  }
  return host::pipe::SiggenClient(
      host::FacetClient(transport, session.plugin_id, std::string(host::pipe::kSiggenFacetName)), std::move(*caps));
}

}

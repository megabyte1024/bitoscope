/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <cstdint>
#include <expected>
#include <string>
#include <string_view>

#include "facet_client.h"
#include "la_pipe_client.h"
#include "driver_device.h"
#include "siggen_pipe_client.h"

namespace bitoscope::acquisition {

struct FacetVersion {
  uint32_t major = 0;
  uint32_t minor = 0;
};

inline constexpr FacetVersion kMinLaAcquisitionVersionWithCaptureId{0, 0};

std::expected<const ipc::FacetInfo*, std::string> findFacet(const host::pipe::DriverDevice& session,
                                                            std::string_view name);

std::expected<host::pipe::LaAcquisitionClient, std::string> makeLaClient(const host::pipe::DriverDevice& session,
                                                                         host::FacetTransport& transport,
                                                                         FacetVersion required = {});

std::expected<void, std::string> requireBulkRing(const host::pipe::DriverDevice& session,
                                                 const host::pipe::LaAcquisitionClient& la);

std::expected<host::pipe::SiggenClient, std::string> makeSiggenClient(const host::pipe::DriverDevice& session,
                                                                      host::FacetTransport& transport);

}

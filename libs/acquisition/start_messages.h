/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <string>
#include <string_view>

#include "facet_client.h"

namespace bitoscope::acquisition {

std::string couldNotStartCapture(std::string_view id, std::string_view why);

std::string noCaptureIdMessage();

std::string couldNotSetUpRing(std::string_view id, std::string_view why);

std::string ringNotCreatedMessage();

std::string propertiesNotSet(std::string_view who, const bitoscope::host::PropertiesRefusal& refusal);

std::string mayStillBeRunning(std::string_view id, std::string_view why);

}

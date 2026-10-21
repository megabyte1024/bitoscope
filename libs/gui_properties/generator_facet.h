/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <cstddef>
#include <string_view>

namespace bitoscope::gui_properties {

bool facetNodesShown(std::size_t facetsInSection);

bool isGeneratorFacet(std::string_view facetName);

}

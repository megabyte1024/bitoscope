/*
 * SPDX-License-Identifier: MIT
 */

#include "generator_facet.h"

#include "ipc_signal_generator.h"

namespace bitoscope::gui_properties {

bool isGeneratorFacet(std::string_view facetName) { return facetName == ipc::siggen::kFacetName; }

bool facetNodesShown(std::size_t facetsInSection) { return facetsInSection > 1; }

}

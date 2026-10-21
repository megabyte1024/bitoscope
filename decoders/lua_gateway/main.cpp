/*
 * SPDX-License-Identifier: MIT
 */

#include "embedded_script.h"
#include "gateway.h"

int main(int argc, char** argv) {
  return bitoscope::lua_gateway::runGateway(argc, argv, bitoscope::lua_gateway::builtinScripts());
}

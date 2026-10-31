/*
 * SPDX-License-Identifier: MIT
 */

#include "demo_analyzer.h"

int main(int argc, char** argv) {
  return bitoscope::plugin::runDriver(argc, argv, demo_driver::demoDriver());
}

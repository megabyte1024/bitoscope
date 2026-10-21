/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <string>
#include <vector>

#include "decoder.h"
#include "stream.h"

namespace bitoscope::decode {

std::string displayOf(const Decoder& decoder);
std::string displayOf(const ChannelSpec& channel);
std::string displayOf(const OptionSpec& option);
std::string displayOf(const InputSpec& input);
std::string displayOf(const OutputSpec& output);

std::vector<AnnotationClass> displayClassesOf(const Decoder& decoder);

}

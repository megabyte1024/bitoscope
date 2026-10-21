/*
 * SPDX-License-Identifier: MIT
 */

#include "spec_display.h"

#include "display_text.h"

namespace bitoscope::decode {

std::string displayOf(const Decoder& decoder) { return display_text::shownName(decoder.name(), decoder.display()); }
std::string displayOf(const ChannelSpec& channel) { return display_text::shownName(channel.name, channel.display); }
std::string displayOf(const OptionSpec& option) { return display_text::shownName(option.name, option.display); }
std::string displayOf(const InputSpec& input) { return display_text::shownName(input.name, input.display); }
std::string displayOf(const OutputSpec& output) { return display_text::shownName(output.name, output.display); }

std::vector<AnnotationClass> displayClassesOf(const Decoder& decoder) {
  std::vector<AnnotationClass> classes = decoder.annotationClasses();
  for (AnnotationClass& cls : classes) cls.name = display_text::displayText(cls.name);
  return classes;
}

}

/*
 * SPDX-License-Identifier: MIT
 */

#include "output_writer.h"

namespace bitoscope::decoders_common {

std::expected<plugin::StreamWriter, std::string> outputWriter(const plugin::DecodeJob& job, plugin::Annotations& out,
                                                              std::string_view name) {
  if (job.outputs.empty()) return plugin::StreamWriter();
  return out.output(job, name);
}

}

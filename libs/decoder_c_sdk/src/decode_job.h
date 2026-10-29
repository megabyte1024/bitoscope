/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <cstdint>
#include <expected>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "bitoscope_decoder.h"
#include "decode_input.h"
#include "declared_ids.h"
#include "decoder.h"
#include "decoder_model.h"
#include "ipc_decoder.h"
#include "stream.h"

namespace bitoscope::plugin_c_sdk {

struct JobInput {
  std::string name;
  decode::StreamType type = decode::StreamType::Words;
  uint32_t width = 0;
  bool bound = false;
};

// WHY: PreparedJob is never moved: the C structs the decoder holds point into it.
struct PreparedJob {
  uint64_t id = 0;
  const DecoderRegistration* decoder = nullptr;
  std::vector<std::string> channelNames;
  std::vector<bitoscope_job_channel> channels;
  std::vector<std::pair<std::string, std::string>> optionTexts;
  std::vector<bitoscope_job_option> options;
  std::vector<decode::GuardedOutput> declaredOutputs;
  std::vector<bitoscope_job_output> outputs;
  std::vector<JobInput> declaredInputs;
  std::vector<bitoscope_job_input> inputs;
  bitoscope_decode_job c{};
  uint64_t totalSamples = 0;
  bool totalIsBound = false;
  uint64_t lookAhead = 0;

  PreparedJob(uint64_t jobId, const DecoderRegistration& registration, const ipc::decoder::JobSetup& setup,
              const decode::OptionMap& resolved, std::vector<decode::GuardedOutput> jobOutputs,
              std::vector<JobInput> jobInputs);
  PreparedJob(const PreparedJob&) = delete;
  PreparedJob& operator=(const PreparedJob&) = delete;
};

std::expected<std::unique_ptr<PreparedJob>, std::string> prepareJob(std::span<const DecoderRegistration> decoders,
                                                                    uint64_t id,
                                                                    const ipc::decoder::JobSetup& setup);

class DeliveryCheck {
 public:
  explicit DeliveryCheck(const DecoderRegistration& decoder);

  std::optional<std::string> refusal(std::span<const bitoscope_annotation> batch, size_t byteValues) const;
  bool showsBytes(uint32_t cls) const;
  bool anyBytes() const { return !bytesClasses_.empty(); }
  const std::string& prefix() const { return prefix_; }

 private:
  const DecoderRegistration& decoder_;
  std::string prefix_;
  DeclaredIds classIds_;
  DeclaredIds rowIds_;
  std::vector<uint32_t> bytesClasses_;
};

void decodePrepared(PreparedJob& job, bitoscope_decode_input& in, bitoscope_annotations& out,
                    bitoscope_annotations::DeliverItems deliverItems);

}

/*
 * SPDX-License-Identifier: MIT
 */

#include "decode_job.h"

#include <algorithm>
#include <exception>
#include <utility>
#include <variant>

#include "declared_decoder.h"
#include "plugin_model.h"

namespace bitoscope::plugin_c_sdk {

namespace dec = ipc::decoder;

namespace {

constexpr const char* kHostSource = "host";

std::string describe(const bitoscope_annotation& a) {
  return "annotation [" + std::to_string(a.begin) + ", " + std::to_string(a.end) + ") in row " +
         std::to_string(a.row) + ", class " + std::to_string(a.cls) + ", value " + std::to_string(a.value);
}

int cStreamType(decode::StreamType type) {
  return type == decode::StreamType::SpiTransfers ? BITOSCOPE_STREAM_SPI_TRANSFERS : BITOSCOPE_STREAM_WORDS;
}

struct NamedLookAhead {
  const bitoscope_reply* reply = nullptr;
  uint64_t samples = 0;
};
thread_local NamedLookAhead tLookAhead;

std::expected<void, std::string> callCheck(const DecoderRegistration& decoder, const bitoscope_decode_job& job,
                                           uint64_t& lookAhead) {
  if (decoder.check == nullptr) return {};
  bitoscope_reply reply;
  tLookAhead = {};
  int status = BITOSCOPE_ERROR;
  try {
    status = decoder.check(decoder.user, &job, &reply);
  } catch (const std::exception& e) {
    reply.message = "decoder '" + decoder.info.name + "': check function threw: " + e.what();
  } catch (...) {
    reply.message = "decoder '" + decoder.info.name + "': check function threw";
  }
  if (status == BITOSCOPE_OK) {
    lookAhead = tLookAhead.reply == &reply ? tLookAhead.samples : 0;
    return {};
  }
  if (reply.message.empty()) {
    reply.message = "decoder '" + decoder.info.name + "' refused job (its check function returned " +
                    std::to_string(status) + ")";
  }
  return std::unexpected(std::move(reply.message));
}

std::vector<uint32_t> idsOf(const std::vector<dec::AnnotationClass>& classes) {
  std::vector<uint32_t> ids;
  for (const dec::AnnotationClass& k : classes) ids.push_back(k.id);
  return ids;
}

std::vector<uint32_t> idsOf(const std::vector<dec::Row>& rows) {
  std::vector<uint32_t> ids;
  for (const dec::Row& r : rows) ids.push_back(r.id);
  return ids;
}

}

PreparedJob::PreparedJob(uint64_t jobId, const DecoderRegistration& registration, const dec::JobSetup& setup,
                         const decode::OptionMap& resolved, std::vector<decode::GuardedOutput> jobOutputs,
                         std::vector<JobInput> jobInputs)
    : id(jobId),
      decoder(&registration),
      declaredOutputs(std::move(jobOutputs)),
      declaredInputs(std::move(jobInputs)),
      totalSamples(setup.facts.totalSamples),
      totalIsBound(setup.facts.totalIsBound) {
  for (const dec::ChannelBinding& b : setup.channels) channelNames.push_back(b.name);
  for (size_t i = 0; i < setup.channels.size(); ++i) {
    channels.push_back({channelNames[i].c_str(), static_cast<uint32_t>(i), setup.channels[i].channel});
  }
  for (const dec::OptionSpec& spec : registration.info.options) {
    const auto value = resolved.find(spec.name);
    optionTexts.emplace_back(spec.name, value != resolved.end() ? value->second : spec.defaultValue);
  }
  for (const auto& [name, value] : optionTexts) options.push_back({name.c_str(), value.c_str()});
  for (const decode::GuardedOutput& o : declaredOutputs) {
    outputs.push_back({o.name.c_str(), cStreamType(o.type), o.width, o.wanted ? 1 : 0});
  }
  c.job_id = jobId;
  c.decoder = registration.info.name.c_str();
  c.channels = channels.empty() ? nullptr : channels.data();
  c.num_channels = channels.size();
  c.options = options.empty() ? nullptr : options.data();
  c.num_options = options.size();
  c.sample_rate_milli_hz = setup.facts.sampleRateMilliHz;
  c.capture_channels = setup.facts.channelCount;
  c.total_samples = setup.facts.totalSamples;
  c.total_is_bound = setup.facts.totalIsBound ? 1 : 0;
  c.progress_step = setup.progressStep;
  c.outputs = outputs.empty() ? nullptr : outputs.data();
  c.num_outputs = outputs.size();
  for (const JobInput& i : declaredInputs) inputs.push_back({i.name.c_str(), cStreamType(i.type), i.width, i.bound ? 1 : 0});
  c.inputs = inputs.empty() ? nullptr : inputs.data();
  c.num_inputs = inputs.size();
}

std::expected<std::unique_ptr<PreparedJob>, std::string> prepareJob(std::span<const DecoderRegistration> decoders,
                                                                    uint64_t id, const dec::JobSetup& setup) {
  const DecoderRegistration* found = nullptr;
  std::string names;
  for (const DecoderRegistration& d : decoders) {
    if (d.info.name == setup.decoder) found = &d;
    names += (names.empty() ? "" : ", ") + d.info.name;
  }
  if (found == nullptr) {
    return std::unexpected("this plugin has no decoder '" + setup.decoder + "' (it has: " +
                           (names.empty() ? std::string("none") : names) + ")");
  }
  decode::ChannelMap channels;
  for (const dec::ChannelBinding& b : setup.channels) {
    if (!channels.emplace(b.name, b.channel).second) return std::unexpected("'" + b.name + "' is given twice");
  }
  decode::OptionMap options;
  for (const dec::OptionValue& o : setup.options) {
    if (!options.emplace(o.name, o.value).second) return std::unexpected("'" + o.name + "' is given twice");
  }
  // WHY: Inputs declare no protocol: the host settles protocol fit at plan time; configure() judges type and width.
  decode::InputMap inputs;
  for (const dec::InputBinding& b : setup.inputs) {
    const std::optional<decode::StreamType> type = streamTypeOf(b.type);
    if (!type) {
      return std::unexpected("input '" + b.name + "' carries stream type this plugin does not know (" +
                             std::to_string(static_cast<unsigned>(b.type)) + ")");
    }
    const decode::BoundInput bound{kHostSource, decode::OutputSpec{b.name, "", *type, b.width, "", ""}, b.width, ""};
    if (!inputs.emplace(b.name, bound).second) return std::unexpected("'" + b.name + "' is given twice");
  }
  std::unique_ptr<PreparedJob> job;
  DeclaredDecoder declared(found->info, [&]() { return callCheck(*found, job->c, job->lookAhead); });
  if (auto configured = declared.configure(channels, options, inputs); !configured) {
    return std::unexpected(std::move(configured.error()));
  }
  const std::vector<decode::OutputSpec> declaredOutputs = declared.outputs();
  std::vector<decode::GuardedOutput> jobOutputs;
  for (const decode::OutputSpec& o : declaredOutputs) {
    const bool wanted = std::find(setup.outputs.begin(), setup.outputs.end(), o.name) != setup.outputs.end();
    jobOutputs.push_back({o.name, o.type, declared.resolvedWidth(o), wanted});
  }
  for (const std::string& wanted : setup.outputs) {
    const auto spec = std::find_if(declaredOutputs.begin(), declaredOutputs.end(),
                                   [&](const decode::OutputSpec& o) { return o.name == wanted; });
    if (spec == declaredOutputs.end()) {
      return std::unexpected("decoder '" + setup.decoder + "' has no output '" + wanted + "'");
    }
    if (!declared.outputAvailable(*spec)) {
      return std::unexpected(decode::displayOf(declared) + "'s output '" + wanted + "' needs its channel '" +
                             spec->requires_channel + "' bound");
    }
  }
  std::vector<JobInput> jobInputs;
  for (const decode::InputSpec& i : declared.inputs()) {
    const auto b = std::find_if(setup.inputs.begin(), setup.inputs.end(),
                                [&](const dec::InputBinding& x) { return x.name == i.name; });
    jobInputs.push_back({i.name, i.type, b != setup.inputs.end() ? b->width : 0u, b != setup.inputs.end()});
  }
  job = std::make_unique<PreparedJob>(id, *found, setup, declared.resolvedOptions(), std::move(jobOutputs),
                                      std::move(jobInputs));
  const decode::CaptureFacts facts{setup.facts.sampleRateMilliHz, setup.facts.channelCount, setup.facts.totalSamples,
                                   setup.facts.totalIsBound};
  if (auto checked = declared.checkCapture(facts); !checked) return std::unexpected(std::move(checked.error()));
  return job;
}

DeliveryCheck::DeliveryCheck(const DecoderRegistration& decoder)
    : decoder_(decoder),
      prefix_("decoder '" + decoder.info.name + "': "),
      classIds_(idsOf(decoder.info.annotationClasses)),
      rowIds_(idsOf(decoder.info.rows)) {
  for (const dec::AnnotationClass& k : decoder.info.annotationClasses) {
    if (std::holds_alternative<dec::BytesValue>(k.format)) bytesClasses_.push_back(k.id);
  }
}

bool DeliveryCheck::showsBytes(uint32_t cls) const {
  return std::find(bytesClasses_.begin(), bytesClasses_.end(), cls) != bytesClasses_.end();
}

std::optional<std::string> DeliveryCheck::refusal(std::span<const bitoscope_annotation> batch, size_t byteValues) const {
  for (const bitoscope_annotation& a : batch) {
    if (!classIds_.contains(a.cls)) {
      return prefix_ + describe(a) + " is of class " + std::to_string(a.cls) + ", which decoder does not declare";
    }
    if (!rowIds_.contains(a.row)) {
      return prefix_ + describe(a) + " is in row " + std::to_string(a.row) + ", which decoder does not declare";
    }
  }
  if (bytesClasses_.empty() && byteValues == 0) return std::nullopt;
  std::vector<dec::Annotation> wire;
  wire.reserve(batch.size());
  for (const bitoscope_annotation& a : batch) wire.push_back({a.begin, a.end, a.row, a.cls, a.value});
  if (auto why = dec::checkByteReferences(decoder_.info, wire, byteValues)) return prefix_ + *why;
  return std::nullopt;
}

void decodePrepared(PreparedJob& job, bitoscope_decode_input& in, bitoscope_annotations& out,
                    bitoscope_annotations::DeliverItems deliverItems) {
  const DecoderRegistration& decoder = *job.decoder;
  const std::string prefix = "decoder '" + decoder.info.name + "': ";
  try {
    if (auto opened = out.openOutputs(job.outputs, job.totalSamples, std::move(deliverItems)); !opened) {
      in.fail(prefix + opened.error());
    }
  } catch (...) {
    in.failThrown();
  }
  int status = BITOSCOPE_ERROR;
  try {
    status = decoder.decode(decoder.user, &job.c, &in, &out);
  } catch (const std::exception& e) {
    in.fail(prefix + "decode function threw: " + e.what());
  } catch (...) {
    in.fail(prefix + "decode function threw");
  }
  try {
    if (status != BITOSCOPE_OK && !in.failed()) {
      in.fail(prefix + "decode function returned " + std::to_string(status) + " without saying why");
    }
    if (!in.failed()) (void)out.finish();
    if (!in.failed() && in.drain()) {
      if (!job.totalIsBound && job.totalSamples != 0 && in.arrived() != job.totalSamples) {
        in.failIntegrity("host's stream ended after " + std::to_string(in.arrived()) +
                         " samples; job's capture has " + std::to_string(job.totalSamples));
      } else {
        (void)out.itemsEndWithin(in.arrived());
      }
    }
    if (!in.failed()) (void)out.markEnd(in.arrived());
  } catch (...) {
    in.failThrown();
  }
}

}

extern "C" {

int bitoscope_reply_look_ahead_samples(bitoscope_reply* reply, uint64_t samples) {
  if (reply == nullptr) return BITOSCOPE_ERROR;
  bitoscope::plugin_c_sdk::tLookAhead = {reply, samples};
  return BITOSCOPE_OK;
}

}

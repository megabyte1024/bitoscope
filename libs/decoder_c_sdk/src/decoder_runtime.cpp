/*
 * SPDX-License-Identifier: MIT
 */

#include "decoder_runtime.h"

#include <algorithm>
#include <chrono>
#include <exception>
#include <functional>
#include <optional>
#include <set>
#include <string>
#include <system_error>
#include <utility>
#include <variant>
#include <vector>

#include <capnp/message.h>

#include "decode_ring_reader.h"
#include "decode_input.h"
#include "decode_job.h"
#include "declared_decoder.h"
#include "decoder.h"
#include "frame_output.h"
#include "ipc_decoder_build.h"

namespace bitoscope::plugin_c_sdk {

namespace dec = ipc::decoder;


struct DecoderRuntime::Job {
  std::unique_ptr<PreparedJob> setup;
  RemoteItems* items = nullptr;
  std::unique_ptr<bitoscope_decode_input> input;
  bool cancelled = false;
};

class DecoderRuntime::RemoteItems final : public ItemSource {
 public:
  RemoteItems(uint64_t job, const std::vector<JobInput>& inputs) : job_(job) {
    for (const JobInput& i : inputs) inputs_.push_back({i.name, i.bound, false, std::nullopt});
  }

  std::expected<void, std::string> request(uint32_t input) override {
    std::string name;
    {
      std::lock_guard lock(mutex_);
      if (!error_.empty()) return std::unexpected(error_);
      inputs_[input].asked = true;
      name = inputs_[input].name;
    }
    auto payload = dec::encodeEvent(dec::DecoderEvent{job_, dec::ItemsWanted{name}});
    if (!payload) return std::unexpected("request for items of input '" + name + "' could not be encoded: " +
                                         payload.error());
    if (auto sent = sendFacetEvent(std::string(dec::kFacetName), std::move(*payload)); !sent) {
      return std::unexpected("request for items of input '" + name + "' could not be sent to host: " +
                             sent.error());
    }
    return {};
  }

  std::expected<std::optional<ItemBatchIn>, std::string> await(uint32_t input, uint64_t wait_ms) override {
    std::unique_lock lock(mutex_);
    cv_.wait_for(lock, std::chrono::milliseconds(wait_ms),
                 [&] { return !error_.empty() || inputs_[input].arrived.has_value(); });
    if (!error_.empty()) return std::unexpected(error_);
    return std::exchange(inputs_[input].arrived, std::nullopt);
  }

  std::expected<void, std::string> deliver(const dec::ItemFeed& feed) {
    std::lock_guard lock(mutex_);
    const auto it = std::find_if(inputs_.begin(), inputs_.end(),
                                 [&](const Input& i) { return i.bound && i.name == feed.input; });
    std::string refused;
    if (it == inputs_.end()) {
      fail("host sent items of input '" + feed.input + "', which job does not read");
      refused = "job " + std::to_string(job_) + " does not read input '" + feed.input + "'";
    } else if (!it->asked || it->arrived) {
      fail("host sent items of input '" + feed.input + "' that were not asked for");
      refused = "job " + std::to_string(job_) + " did not ask for items of input '" + feed.input + "'";
    }
    if (!refused.empty()) {
      cv_.notify_all();
      return std::unexpected(std::move(refused));
    }
    ItemBatchIn batch;
    batch.items.reserve(feed.items.size());
    for (const dec::StreamItem& i : feed.items) batch.items.push_back({i.begin, i.end, i.value, i.kind, i.flags});
    batch.last = feed.last;
    it->arrived = std::move(batch);
    it->asked = false;
    cv_.notify_all();
    return {};
  }

  std::optional<std::string> failure() {
    std::lock_guard lock(mutex_);
    return error_.empty() ? std::nullopt : std::optional<std::string>(error_);
  }

 private:
  struct Input {
    std::string name;
    bool bound = false;
    bool asked = false;
    std::optional<ItemBatchIn> arrived;
  };
  void fail(std::string why) {
    if (error_.empty()) error_ = std::move(why);
  }

  uint64_t job_;
  std::mutex mutex_;
  std::condition_variable cv_;
  std::vector<Input> inputs_;
  std::string error_;
};

DecoderRuntime::DecoderRuntime(const DecoderFacetRegistration& facet) : facet_(facet) {}

DecoderRuntime::~DecoderRuntime() { abandon(); }

void DecoderRuntime::abandon() {
  std::deque<std::unique_ptr<Job>> dropped;
  {
    std::lock_guard lock(mutex_);
    stopping_ = true;
    if (running_ != nullptr) {
      running_->cancelled = true;
      running_->input->cancel();
    }
    dropped.swap(queue_);
  }
  cv_.notify_all();
  if (worker_.joinable()) worker_.join();
  if (keeper_.joinable()) keeper_.join();
  dropped.clear();
}

FacetAnswer DecoderRuntime::handle(std::span<const uint8_t> payload) {
  auto command = dec::decodeCommand(payload);
  if (!command) {
    const ipc::DecodeError& error = command.error();
    return ipc::Refusal{"decoder facet cannot act on this command: " + error.message,
                        error.kind == ipc::DecodeErrorKind::unknownMember ? error.member : uint16_t{0}};
  }
  if (const auto* c = std::get_if<dec::CheckJob>(&command->action)) return check(command->jobId, c->setup);
  if (const auto* s = std::get_if<dec::DecodeStart>(&command->action)) return start(command->jobId, *s);
  if (std::holds_alternative<dec::CancelJob>(command->action)) return cancel(command->jobId);
  return feed(command->jobId, std::get<dec::ItemFeed>(command->action));
}

ipc::FacetReply DecoderRuntime::feed(uint64_t id, const dec::ItemFeed& f) {
  std::lock_guard lock(mutex_);
  if (running_ == nullptr || running_->setup->id != id || running_->items == nullptr) {
    return ipc::FacetReply{false, "job " + std::to_string(id) + " is not running: its items have nowhere to go", {}};
  }
  if (auto delivered = running_->items->deliver(f); !delivered) {
    return ipc::FacetReply{false, std::move(delivered.error()), {}};
  }
  return ipc::FacetReply{true, "job " + std::to_string(id) + " took " + std::to_string(f.items.size()) +
                                   " items of input '" + f.input + "'",
                         {}};
}

std::expected<std::unique_ptr<DecoderRuntime::Job>, std::string> DecoderRuntime::prepare(uint64_t id,
                                                                                         const dec::JobSetup& setup) {
  auto prepared = prepareJob(facet_.decoders, id, setup);
  if (!prepared) return std::unexpected(std::move(prepared.error()));
  auto job = std::make_unique<Job>();
  job->setup = std::move(*prepared);
  return job;
}

ipc::FacetReply DecoderRuntime::check(uint64_t id, const dec::JobSetup& setup) {
  auto job = prepare(id, setup);
  if (!job) return ipc::FacetReply{false, std::move(job.error()), {}};
  return ipc::FacetReply{true, "decoder '" + setup.decoder + "' accepts job",
                         dec::encodeCheckAnswer(dec::CheckAnswer{(*job)->setup->lookAhead})};
}

ipc::FacetReply DecoderRuntime::start(uint64_t id, const dec::DecodeStart& s) {
  {
    std::lock_guard lock(mutex_);
    const bool known = (running_ != nullptr && running_->setup->id == id) ||
                       std::any_of(queue_.begin(), queue_.end(), [&](const auto& j) { return j->setup->id == id; });
    if (known) return ipc::FacetReply{false, "job " + std::to_string(id) + " is running or queued already", {}};
  }
  auto job = prepare(id, s.setup);
  if (!job) return ipc::FacetReply{false, std::move(job.error()), {}};
  Job& j = **job;
  PreparedJob& prepared = *j.setup;
  if (prepared.totalIsBound && (prepared.channels.empty() || !s.setup.inputs.empty())) {
    return ipc::FacetReply{false,
                           "job " + std::to_string(id) + " of decoder '" + s.setup.decoder + "' " +
                               std::string(decode::kStreamsNeedFinalLength),
                           {}};
  }
  if (!s.setup.outputs.empty() && s.setup.facts.totalSamples == 0) {
    ipc::FacetReply refused;
    refused.message = "job " + std::to_string(id) + " wants items from output '" + s.setup.outputs.front() +
                      "' but names no capture length: its items could not be checked against capture";
    return refused;
  }

  const bool readsItems = !prepared.declaredInputs.empty();
  if (readsItems && prepared.channels.empty()) {
    const std::string what = "job " + std::to_string(id) + " of upper decoder '" + s.setup.decoder + "'";
    if (!s.ringName.empty()) {
      return ipc::FacetReply{false, what + " names ring; upper decoder reads its inputs from host", {}};
    }
    if (s.setup.facts.totalSamples == 0) {
      return ipc::FacetReply{false, what + " names no capture length: its annotations are in capture samples", {}};
    }
    std::vector<ItemInputSpec> specs;
    for (const JobInput& i : prepared.declaredInputs) specs.push_back({i.name, i.bound});
    auto remote = std::make_unique<RemoteItems>(id, prepared.declaredInputs);
    j.items = remote.get();
    j.input = bitoscope_decode_input::fromItems(s.setup.facts.totalSamples, std::move(specs), std::move(remote));
    j.input->setProgressStep(s.setup.progressStep);
    return queue(std::move(*job));
  }

  auto reader = plugin_ring::DecodeRingReader::open(s.ringName, id, static_cast<uint32_t>(prepared.channels.size()));
  if (!reader) {
    return ipc::FacetReply{false, "ring \"" + s.ringName + "\" of job " + std::to_string(id) +
                                      " cannot be opened: " + reader.error(),
                           {}};
  }
  if (reader->slotCount() != s.ring.slotCount || reader->ring().slotPayloadBytes() != s.ring.slotPayloadBytes) {
    return ipc::FacetReply{false,
                           "ring \"" + s.ringName + "\" of job " + std::to_string(id) + " has " +
                               std::to_string(reader->slotCount()) + " slots of " +
                               std::to_string(reader->ring().slotPayloadBytes()) + " bytes; DECODE says " +
                               std::to_string(s.ring.slotCount) + " slots of " +
                               std::to_string(s.ring.slotPayloadBytes) + " bytes",
                           {}};
  }
  j.input = bitoscope_decode_input::fromRing(std::move(*reader));
  j.input->setProgressStep(s.setup.progressStep);
  if (prepared.totalIsBound) j.input->setBound(prepared.totalSamples);
  if (readsItems) {
    std::vector<ItemInputSpec> specs;
    for (const JobInput& i : prepared.declaredInputs) specs.push_back({i.name, i.bound});
    auto remote = std::make_unique<RemoteItems>(id, prepared.declaredInputs);
    RemoteItems* items = remote.get();
    if (auto added = j.input->addItems(std::move(specs), std::move(remote)); !added) {
      return ipc::FacetReply{false, "job " + std::to_string(id) + " cannot read its items: " + added.error(), {}};
    }
    j.items = items;
  }
  return queue(std::move(*job));
}

ipc::FacetReply DecoderRuntime::queue(std::unique_ptr<Job> job) {
  const uint64_t id = job->setup->id;
  std::string where;
  {
    std::lock_guard lock(mutex_);
    if (!worker_.joinable()) {
      try {
        worker_ = std::thread([this] { work(); });
        keeper_ = std::thread([this] { keep(); });
      } catch (const std::system_error& e) {
        if (worker_.joinable()) {
          stopping_ = true;
        }
        return ipc::FacetReply{false, std::string("no job thread could be started: ") + e.what(), {}};
      }
    }
    if (stopping_) return ipc::FacetReply{false, "decoder plugin is stopping", {}};
    if (running_ != nullptr || !queue_.empty()) {
      where = " (queued behind job " +
              std::to_string(queue_.empty() ? running_->setup->id : queue_.back()->setup->id) + ")";
    }
    queue_.push_back(std::move(job));
  }
  cv_.notify_all();
  return ipc::FacetReply{true, "job " + std::to_string(id) + " accepted" + where, {}};
}

ipc::FacetReply DecoderRuntime::cancel(uint64_t id) {
  std::unique_ptr<Job> queued;
  {
    std::lock_guard lock(mutex_);
    if (running_ != nullptr && running_->setup->id == id) {
      running_->cancelled = true;
      running_->input->cancel();
      return ipc::FacetReply{true, "job " + std::to_string(id) + " is cancelled", {}};
    }
    const auto it = std::find_if(queue_.begin(), queue_.end(), [&](const auto& j) { return j->setup->id == id; });
    if (it == queue_.end()) {
      return ipc::FacetReply{true, "no job " + std::to_string(id) + " is running or queued (it may have ended)", {}};
    }
    queued = std::move(*it);
    queue_.erase(it);
  }
  queued->input.reset();
  auto payload = dec::encodeEvent(
      dec::DecoderEvent{id, dec::DecodeEnded{dec::DecodeStatus::cancelled, "cancelled by host", 0, 0}});
  if (payload) {
    if (auto sent = sendFacetEvent(std::string(dec::kFacetName), std::move(*payload)); !sent) {
      logLine(ipc::LogLevel::error, "job " + std::to_string(id) + "'s end could not be sent: " + sent.error());
    }
  }
  return ipc::FacetReply{true, "job " + std::to_string(id) + " is cancelled (it had not started)", {}};
}

void DecoderRuntime::work() {
  for (;;) {
    std::unique_ptr<Job> job;
    {
      std::unique_lock lock(mutex_);
      cv_.wait(lock, [this] { return stopping_ || !queue_.empty(); });
      if (stopping_) return;
      job = std::move(queue_.front());
      queue_.pop_front();
      running_ = job.get();
    }
    run(*job);
  }
}

void DecoderRuntime::keep() {
  std::unique_lock lock(mutex_);
  for (;;) {
    if (cv_.wait_for(lock, std::chrono::milliseconds(kQueuedHeartbeatMs), [this] { return stopping_; })) return;
    for (const auto& job : queue_) job->input->heartbeat();
  }
}

void DecoderRuntime::run(Job& runtimeJob) {
  PreparedJob& job = *runtimeJob.setup;
  const DeliveryCheck check(*job.decoder);
  uint64_t sent = 0;
  std::span<const std::vector<uint8_t>> table;
  bitoscope_annotations out(runtimeJob.input.get(), [&](std::span<const bitoscope_annotation> batch, uint64_t settled)
                                                        -> std::expected<void, std::string> {
    const std::span<const std::vector<uint8_t>> values = std::exchange(table, {});
    if (auto why = check.refusal(batch, values.size())) return std::unexpected(std::move(*why));
    const bool anyBytes = check.anyBytes();
    size_t at = 0;
    do {
      const auto chunk = batch.subspan(at, (std::min)(kAnnotationBatch, batch.size() - at));
      at += chunk.size();
      dec::ByteValues chunkValues;
      if (anyBytes) {
        for (const bitoscope_annotation& a : chunk) {
          if (check.showsBytes(a.cls)) chunkValues.values.push_back(values[a.value]);
        }
      }
      if (!chunkValues.values.empty()) {
        auto values_payload = dec::encodeEvent(dec::DecoderEvent{job.id, std::move(chunkValues), 0});
        if (!values_payload) return std::unexpected("byte values could not be encoded: " + values_payload.error());
        if (auto s = sendFacetEvent(std::string(dec::kFacetName), std::move(*values_payload)); !s) {
          return std::unexpected("byte values could not be sent to host: " + s.error());
        }
      }
      const uint64_t mark = at == batch.size() ? settled : 0;
      uint64_t nextValue = 0;
      const auto valueOf = [&](const bitoscope_annotation& a) {
        return anyBytes && check.showsBytes(a.cls) ? nextValue++ : a.value;
      };
      if (auto s = sendFacetEvent(dec::kFacetName, [&](capnp::MessageBuilder& message) {
            dec::buildAnnotationEvent(message, job.id, mark, chunk, valueOf);
          });
          !s) {
        return std::unexpected("annotations could not be sent to host: " + s.error());
      }
      sent += chunk.size();
    } while (at < batch.size());
    return {};
  });

  out.onBytes([&table](std::span<const std::vector<uint8_t>> values) -> std::expected<void, std::string> {
    table = values;
    return {};
  });

  uint64_t itemsSent = 0;
  const auto sendItems = [&](uint32_t output, std::span<const bitoscope_stream_item> batch)
      -> std::expected<void, std::string> {
    const std::string& name = job.declaredOutputs[output].name;
    for (size_t at = 0; at < batch.size(); at += kItemBatch) {
      const auto chunk = batch.subspan(at, (std::min)(kItemBatch, batch.size() - at));
      if (auto s = sendFacetEvent(dec::kFacetName, [&](capnp::MessageBuilder& message) {
            dec::buildItemEvent(message, job.id, 0, name, chunk);
          });
          !s) {
        return std::unexpected("stream items could not be sent to host: " + s.error());
      }
      itemsSent += chunk.size();
    }
    return {};
  };

  bitoscope_decode_input& in = *runtimeJob.input;
  decodePrepared(job, in, out, sendItems);
  const std::string& prefix = check.prefix();

  dec::DecodeEnded end;
  end.annotationCount = sent;
  end.samplesRead = in.holdsSamples() ? in.arrived() : 0;
  end.itemCount = itemsSent;
  end.itemsRead = in.itemsRead();
  bool cancelled = false;
  bool stopping = false;
  std::optional<std::string> fedOutOfTurn;
  {
    std::lock_guard lock(mutex_);
    cancelled = runtimeJob.cancelled;
    stopping = stopping_;
    running_ = nullptr;
    if (runtimeJob.items != nullptr) fedOutOfTurn = runtimeJob.items->failure();
  }
  if (fedOutOfTurn && !in.failed()) in.fail(std::move(*fedOutOfTurn));
  // WHY: A hole the plugin found outranks a CANCEL with it; a stream the host stopped is the host's own cancel.
  if (cancelled && !(in.failed() && in.integrityFailed())) {
    end.status = dec::DecodeStatus::cancelled;
    end.error = "cancelled by host";
  } else if (in.failed()) {
    end.status = dec::DecodeStatus::failure;
    end.error = in.error().empty() ? prefix + "job failed" : in.error();
  } else {
    end.status = dec::DecodeStatus::completed;
  }
  runtimeJob.input.reset();
  if (stopping) return;
  auto payload = dec::encodeEvent(dec::DecoderEvent{job.id, std::move(end)});
  if (!payload) {
    logLine(ipc::LogLevel::error, "job " + std::to_string(job.id) + "'s end could not be encoded: " + payload.error());
    return;
  }
  if (auto s = sendFacetEvent(std::string(dec::kFacetName), std::move(*payload)); !s) {
    logLine(ipc::LogLevel::error, "job " + std::to_string(job.id) + "'s end could not be sent: " + s.error());
  }
}

}

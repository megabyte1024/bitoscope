/*
 * SPDX-License-Identifier: MIT
 */

#include "decoder_plugin_client.h"

#include <algorithm>
#include <deque>
#include <exception>
#include <new>
#include <set>
#include <span>
#include <thread>
#include <utility>
#include <variant>

#include "display_text.h"
#include "ipc_protocol_version.h"
#include "plugin_process.h"

namespace bitoscope::decoder_host {

namespace dec = ipc::decoder;
namespace br = bitoscope::bulk_ring;

namespace {

constexpr auto kWatchInterval = std::chrono::milliseconds(20);

std::string prefixed(std::string_view decoder, std::string_view message) {
  return "decoder '" + std::string(decoder) + "': " + std::string(message);
}

}

struct DecoderPluginClient::Job {
  const DecodeJobOptions* options = nullptr;
  const std::vector<std::string>* outputs = nullptr;
  const std::set<std::string>* inputs = nullptr;
  struct Arrived {
    std::vector<dec::Annotation> annotations;
    uint64_t settled = 0;
    std::vector<std::vector<uint8_t>> byte_values;
  };
  const dec::DecoderInfo* info = nullptr;
  bool shows_bytes = false;
  std::optional<std::vector<std::vector<uint8_t>>> byte_values;
  std::vector<Arrived> pending;
  uint64_t received = 0;
  std::vector<std::pair<std::string, std::vector<dec::StreamItem>>> pending_items;
  uint64_t items_received = 0;
  std::deque<std::string> wanted;
  bool ended = false;
  dec::DecodeEnded end;
  std::optional<std::string> out_of_memory;
  std::exception_ptr handler_threw;
  std::optional<std::string> undecodable;
  bool feed_done = false;
  std::expected<void, FeedError> feed = {};
  uint64_t delivered = 0;
};

DecoderPluginClient::DecoderPluginClient(ClientOptions options) : options_(std::move(options)) {}

DecoderPluginClient::~DecoderPluginClient() {
  // WHY: connection_ is reset first: its reader thread calls into this object until it is joined.
  connection_.reset();
}

std::expected<std::shared_ptr<DecoderPluginClient>, std::string> DecoderPluginClient::connect(
    std::unique_ptr<host::PluginLink> link, ClientOptions options) {
  if (!link) return std::unexpected(std::string("no link to decoder plugin was given"));
  std::shared_ptr<DecoderPluginClient> client(new DecoderPluginClient(std::move(options)));
  DecoderPluginClient* self = client.get();
  host::PluginEvents events;
  events.onLog = [self](const ipc::LogEvent& e) { self->log(e.message); };
  events.onFacetEvent = [self](const ipc::FacetEvent& e) { self->onFacetEvent(e); };
  events.onClosed = [self](std::string_view reason) { self->onClosed(reason); };
  events.onNotice = [self](std::string_view notice) { self->log(notice); };
  client->connection_ =
      std::make_unique<host::PluginConnection>(std::move(link), std::move(events), client->options_.placement);
  const std::string& name = client->connection_->name();

  host::HandshakeOffer offer;
  offer.facets = {ipc::FacetRequest{std::string(dec::kFacetName), 1, 1}};
  auto reply = client->connection_->handshake(offer, client->options_.request_timeout);
  if (!reply) return std::unexpected(name + ": handshake failed: " + reply.error().message);
  const ipc::FacetInfo* facet = nullptr;
  for (const ipc::FacetInfo& f : reply->device.facets) {
    if (f.name == dec::kFacetName) facet = &f;
  }
  if (facet == nullptr) {
    return std::unexpected(name + " is not decoder plugin: it offers no \"" + std::string(dec::kFacetName) + "\" facet");
  }
  if (!facet->unusable.empty()) {
    return std::unexpected(name + ": its \"" + std::string(dec::kFacetName) + "\" facet cannot be used: " + facet->unusable);
  }
  auto info = dec::decodeInfo(facet->info);
  if (!info) {
    return std::unexpected(name + ": its \"" + std::string(dec::kFacetName) + "\" facet's info does not decode: " +
                           info.error().message);
  }
  client->info_ = std::move(*info);
  return client;
}

std::expected<std::shared_ptr<DecoderPluginClient>, std::string> DecoderPluginClient::start(
    const std::filesystem::path& executable, ClientOptions options) {
  host::PluginProcessOptions process;
  process.executable = executable;
  process.arguments = {"--device", std::string(kDecoderDeviceId)};
  process.arguments.insert(process.arguments.end(), options.arguments.begin(), options.arguments.end());
  process.placement = options.placement;
  if (options.on_log) process.onStderrLine = options.on_log;
  auto started = host::PluginProcess::start(std::move(process));
  if (!started) return std::unexpected(started.error());
  const uint64_t process_id = (*started)->processId();
  auto client = connect(std::move(*started), std::move(options));
  if (client) (*client)->process_id_ = process_id;
  return client;
}

const std::string& DecoderPluginClient::name() const { return connection_->name(); }

const dec::DecoderInfo* DecoderPluginClient::decoder(std::string_view name) const {
  for (const dec::DecoderInfo& d : info_.decoders) {
    if (d.name == name) return &d;
  }
  return nullptr;
}

std::optional<std::string> DecoderPluginClient::closedReason() const {
  std::lock_guard lock(mutex_);
  return closed_;
}

void DecoderPluginClient::log(std::string_view line) const {
  if (options_.on_log) options_.on_log(line);
}

uint64_t DecoderPluginClient::nextJobId() {
  std::lock_guard lock(mutex_);
  return next_job_id_++;
}

std::expected<void, host::RequestError> DecoderPluginClient::send(const dec::DecoderCommand& command) {
  auto payload = dec::encodeCommand(command);
  if (!payload) {
    return std::unexpected(
        host::RequestError{host::RequestFailure::SendFailed, "decoder command could not be encoded: " + payload.error()});
  }
  auto answer = connection_->command(dec::kFacetName, *payload, options_.request_timeout);
  if (!answer) return std::unexpected(std::move(answer.error()));
  return {};
}

std::expected<void, std::string> DecoderPluginClient::check(const dec::JobSetup& setup) {
  auto answer = checkJob(setup);
  if (!answer) return std::unexpected(std::move(answer.error()));
  return {};
}

std::expected<dec::CheckAnswer, std::string> DecoderPluginClient::checkJob(const dec::JobSetup& setup) {
  auto payload = dec::encodeCommand(dec::DecoderCommand{nextJobId(), dec::CheckJob{setup}});
  if (!payload) return std::unexpected(prefixed(setup.decoder, "CHECK could not be encoded: " + payload.error()));
  auto answer = connection_->command(dec::kFacetName, *payload, options_.check_timeout.value_or(options_.request_timeout));
  if (!answer) {
    if (answer.error().kind == host::RequestFailure::Rejected) return std::unexpected(std::move(answer.error().message));
    return std::unexpected(prefixed(setup.decoder, "decoder plugin did not answer CHECK: " + answer.error().message));
  }
  auto decoded = dec::decodeCheckAnswer(*answer);
  if (!decoded) {
    log(name() + ": answer to CHECK does not read, so default decode ring is used: " + decoded.error());
    return dec::CheckAnswer{};
  }
  return *decoded;
}

namespace {

// WHY: Size text is exact, never rounded: a ring whose slots do not divide the limit is some bytes short of it.
std::string sizeText(uint64_t bytes) {
  constexpr uint64_t kKiB = 1024;
  constexpr uint64_t kMiB = kKiB * 1024;
  if (bytes % kMiB == 0) return std::to_string(bytes / kMiB) + " MiB";
  if (bytes % kKiB == 0) return std::to_string(bytes / kKiB) + " KiB";
  return std::to_string(bytes) + " bytes";
}

}

std::string WindowWarning::text() const {
  return "it may need window of " + std::to_string(window_samples) +
         " samples at once for this job; decode ring holds at least " + std::to_string(floor_samples) + " samples (" +
         sizeText(ring_bytes_per_channel) +
         " for each bound channel) even if nothing packs - job fails if signal does not pack well enough; lower "
         "sample rate avoids this";
}

SizedJob sizedFor(const DecodeJobOptions& base, uint64_t look_ahead_samples) {
  SizedJob sized{base, std::nullopt};
  if (look_ahead_samples == 0 || base.slot_bytes_per_channel == 0) return sized;
  sized.job.fill_slots = true;
  const uint64_t slot_samples = base.slot_bytes_per_channel * 8;
  const uint64_t covering = look_ahead_samples / slot_samples + (look_ahead_samples % slot_samples != 0 ? 1 : 0);
  const uint64_t slots = covering + kRingSlackSlots;
  if (slots <= base.slot_count) return sized;
  const uint64_t largest =
      (std::min)(base.max_ring_bytes_per_channel / base.slot_bytes_per_channel, uint64_t{UINT32_MAX});
  if (slots <= largest) {
    sized.job.slot_count = static_cast<uint32_t>(slots);
    return sized;
  }
  sized.job.slot_count = static_cast<uint32_t>((std::max)(largest, uint64_t{base.slot_count}));
  const uint64_t counted = sized.job.slot_count > kRingSlackSlots ? sized.job.slot_count - kRingSlackSlots : 0;
  sized.warning = WindowWarning{look_ahead_samples, counted * slot_samples,
                                uint64_t{sized.job.slot_count} * base.slot_bytes_per_channel};
  return sized;
}

void DecoderPluginClient::onFacetEvent(const ipc::FacetEvent& event) {
  if (event.facet != dec::kFacetName) {
    log(name() + ": event of facet \"" + event.facet + "\", which decoder plugin does not have, was ignored");
    return;
  }
  auto decoded = dec::decodeEvent(event.payload);
  if (!decoded) {
    log(name() + ": decoder event that does not decode was ignored: " + decoded.error().message);
    // WHY: An undecodable event cannot be tied to a job and may have carried annotations: every waiting job fails.
    {
      std::lock_guard lock(mutex_);
      for (auto& [id, job] : jobs_) {
        if (!job->ended && !job->undecodable) {
          job->undecodable = "event of decoder plugin did not decode, so annotations may have been lost: " +
                             decoded.error().message;
        }
      }
    }
    cv_.notify_all();
    return;
  }
  {
    std::lock_guard lock(mutex_);
    const auto it = jobs_.find(decoded->jobId);
    if (it == jobs_.end()) {
      return;
    }
    Job& job = *it->second;
    if (job.ended) return;
    if (job.byte_values && !job.undecodable && !std::holds_alternative<dec::ByteValues>(decoded->body) &&
        !std::holds_alternative<dec::AnnotationBatch>(decoded->body)) {
      job.undecodable = "byte values arrived, but no annotations came to use them";
    }
    if (auto* values = std::get_if<dec::ByteValues>(&decoded->body)) {
      if (job.byte_values && !job.undecodable) job.undecodable = "byte values arrived twice, with no annotations between";
      job.byte_values = std::move(values->values);
    } else if (auto* batch = std::get_if<dec::AnnotationBatch>(&decoded->body)) {
      // WHY: Reader thread only queues batches (moved); decode()'s thread hands them on so reading never stalls.
      const size_t arriving = batch->annotations.size();
      job.received += arriving;
      std::vector<std::vector<uint8_t>> table;
      if (job.byte_values) table = std::move(*job.byte_values);
      job.byte_values.reset();
      if (job.shows_bytes || !table.empty()) {
        const dec::DecoderInfo none;
        if (auto refused = dec::checkByteReferences(job.info != nullptr ? *job.info : none, batch->annotations, table.size())) {
          if (!job.undecodable) job.undecodable = std::move(*refused);
          batch->annotations.clear();
          decoded->settled = 0;
        }
      }
      if (!job.out_of_memory && !job.handler_threw && (arriving != 0 || decoded->settled != 0)) {
        try {
          job.pending.push_back(Job::Arrived{std::move(batch->annotations), decoded->settled, std::move(table)});
        } catch (const std::bad_alloc&) {
          job.out_of_memory = "out of memory queuing its annotations (" + std::to_string(arriving) +
                              " arriving); rest were discarded";
          job.pending.clear();
        }
      }
    } else if (auto* items = std::get_if<dec::ItemBatch>(&decoded->body)) {
      const bool asked = std::find(job.outputs->begin(), job.outputs->end(), items->output) != job.outputs->end();
      if (!asked) {
        if (!job.undecodable) {
          job.undecodable = "decoder plugin sent items of output '" + items->output + "', which job did not ask for";
        }
      } else {
        job.items_received += items->items.size();
        if (!job.out_of_memory && !job.handler_threw && !items->items.empty()) {
          try {
            job.pending_items.emplace_back(std::move(items->output), std::move(items->items));
          } catch (const std::bad_alloc&) {
            job.out_of_memory = "out of memory queuing its items (" + std::to_string(items->items.size()) +
                                " arriving); rest were discarded";
            job.pending_items.clear();
          }
        }
      }
    } else if (auto* wanted = std::get_if<dec::ItemsWanted>(&decoded->body)) {
      if (job.inputs == nullptr || !job.inputs->contains(wanted->input)) {
        if (!job.undecodable) {
          job.undecodable =
              "decoder plugin asked for items of input '" + wanted->input + "', which job does not read";
        }
      } else {
        // WHY: Item requests are answered by decode()'s own thread: the reader thread must never wait.
        try {
          job.wanted.push_back(std::move(wanted->input));
        } catch (const std::bad_alloc&) {
          if (!job.out_of_memory) job.out_of_memory = "out of memory queuing plugin's request for items";
        }
      }
    } else {
      job.end = std::get<dec::DecodeEnded>(std::move(decoded->body));
      job.ended = true;
      if (job.options->on_job_event) job.options->on_job_event(JobEvent::Ended);
    }
  }
  cv_.notify_all();
}

void DecoderPluginClient::onClosed(std::string_view reason) {
  {
    std::lock_guard lock(mutex_);
    if (!closed_) closed_ = std::string(reason);
  }
  cv_.notify_all();
}

std::expected<JobResult, JobError> DecoderPluginClient::decode(const dec::JobSetup& setup, FeedFunction feed,
                                                               const DecodeJobOptions& options,
                                                               AnnotationHandler on_annotations, ItemHandler on_items,
                                                               SettledHandler on_settled) {
  const auto refuse = [&setup](std::string_view message) {
    return std::unexpected(JobError{JobFailure::Refused, prefixed(setup.decoder, message), 0});
  };
  if (setup.channels.empty()) return refuse("decode job needs at least one bound channel");
  if (options.slot_count == 0 || options.slot_bytes_per_channel < 8 || options.slot_bytes_per_channel % 8 != 0) {
    return refuse("decode ring needs at least one slot, of whole number of 64-bit words channel");
  }
  if (!feed) return refuse("decode job needs feed");
  return runJob(setup, std::move(feed), nullptr, options, std::move(on_annotations), std::move(on_items),
                std::move(on_settled));
}

std::expected<JobResult, JobError> DecoderPluginClient::decodeItems(const dec::JobSetup& setup, const ItemInputs& inputs,
                                                                    const DecodeJobOptions& options,
                                                                    AnnotationHandler on_annotations, ItemHandler on_items,
                                                                    SettledHandler on_settled) {
  const auto refuse = [&setup](std::string_view message) {
    return std::unexpected(JobError{JobFailure::Refused, prefixed(setup.decoder, message), 0});
  };
  if (!setup.channels.empty()) return refuse("upper decoder's job binds inputs, not channels");
  if (setup.inputs.empty()) return refuse("upper decoder's job binds at least one input");
  for (const dec::InputBinding& b : setup.inputs) {
    const auto it = inputs.find(b.name);
    if (it == inputs.end() || it->second == nullptr) {
      return refuse("input '" + b.name + "' is bound, but job was given no stream for it");
    }
  }
  return runJob(setup, {}, &inputs, options, std::move(on_annotations), std::move(on_items), std::move(on_settled));
}

std::expected<JobResult, JobError> DecoderPluginClient::decodeMixed(const dec::JobSetup& setup, FeedFunction feed,
                                                                    const ItemInputs& inputs, const DecodeJobOptions& options,
                                                                    AnnotationHandler on_annotations, ItemHandler on_items,
                                                                    SettledHandler on_settled) {
  const auto refuse = [&setup](std::string_view message) {
    return std::unexpected(JobError{JobFailure::Refused, prefixed(setup.decoder, message), 0});
  };
  if (setup.channels.empty()) return refuse("job that reads samples and streams binds at least one channel");
  if (setup.inputs.empty()) return refuse("job that reads samples and streams binds at least one input");
  if (options.slot_count == 0 || options.slot_bytes_per_channel < 8 || options.slot_bytes_per_channel % 8 != 0) {
    return refuse("decode ring needs at least one slot, of whole number of 64-bit words channel");
  }
  for (const dec::InputBinding& b : setup.inputs) {
    const auto it = inputs.find(b.name);
    if (it == inputs.end() || it->second == nullptr) {
      return refuse("input '" + b.name + "' is bound, but job was given no stream for it");
    }
  }
  if (!feed) return refuse("decode job needs feed");
  return runJob(setup, std::move(feed), &inputs, options, std::move(on_annotations), std::move(on_items),
                std::move(on_settled));
}

std::expected<JobResult, JobError> DecoderPluginClient::runJob(const dec::JobSetup& setup, FeedFunction feed,
                                                               const ItemInputs* inputs, const DecodeJobOptions& options,
                                                               AnnotationHandler on_annotations, ItemHandler on_items,
                                                               SettledHandler on_settled) {
  const std::string& decoder = setup.decoder;
  const auto fail = [&decoder](JobFailure kind, std::string_view message, uint64_t delivered = 0) {
    return std::unexpected(JobError{kind, prefixed(decoder, message), delivered});
  };
  {
    std::lock_guard lock(mutex_);
    if (closed_) return fail(JobFailure::PluginGone, "decoder plugin is gone: " + *closed_);
  }

  const uint64_t job_id = nextJobId();
  std::optional<host::DecodeRingProducer> ring;
  std::map<std::string, InputItemFeed> feeds;
  std::set<std::string> fed_inputs;
  try {
    if (feed) {
      host::DecodeRingSpec spec;
      spec.num_channels = static_cast<uint32_t>(setup.channels.size());
      spec.sample_rate_milli_hz = setup.facts.sampleRateMilliHz;
      spec.slot_count = options.slot_count;
      spec.slot_payload_bytes = options.slot_bytes_per_channel * spec.num_channels;
      spec.flush_after_ms = options.flush_after_ms;
      spec.fill_slots = options.fill_slots;
      auto made = host::DecodeRingProducer::create(br::makeRingName(), job_id, spec, options.timing);
      if (!made) return fail(JobFailure::Refused, made.error());
      ring.emplace(std::move(*made));
    }
  } catch (const std::bad_alloc&) {
    return fail(JobFailure::OutOfMemory, "out of memory creating its ring");
  }
  try {
    if (inputs != nullptr) {
      for (const dec::InputBinding& b : setup.inputs) {
        feeds.try_emplace(b.name, b.name, *inputs->at(b.name), options.item_feed_batch);
        fed_inputs.insert(b.name);
      }
    }
  } catch (const std::bad_alloc&) {
    return fail(JobFailure::OutOfMemory, "out of memory setting up its inputs");
  }

  Job job;
  const auto listed = std::find_if(info_.decoders.begin(), info_.decoders.end(),
                                   [&decoder](const dec::DecoderInfo& d) { return d.name == decoder; });
  job.info = listed != info_.decoders.end() ? &*listed : nullptr;
  if (job.info != nullptr) {
    job.shows_bytes = std::any_of(job.info->annotationClasses.begin(), job.info->annotationClasses.end(),
                                  [](const dec::AnnotationClass& k) { return std::holds_alternative<dec::BytesValue>(k.format); });
  }
  if (job.shows_bytes && (!options.on_byte_values || !on_annotations)) {
    return fail(JobFailure::Refused,
                "has classes that show bytes, but its annotations go to reader that takes no byte values");
  }
  job.options = &options;
  job.outputs = &setup.outputs;
  job.inputs = inputs != nullptr ? &fed_inputs : nullptr;
  {
    std::lock_guard lock(mutex_);
    jobs_[job_id] = &job;
  }
  struct Unregister {
    DecoderPluginClient* client;
    uint64_t id;
    ~Unregister() {
      std::lock_guard lock(client->mutex_);
      client->jobs_.erase(id);
    }
  } unregister{this, job_id};

  dec::DecodeStart start{setup, {}, {}};
  start.setup.progressStep =
      options.progress_step != 0 ? options.progress_step : progressStepFor(options.pacing, setup.facts.totalSamples);
  if (ring) {
    start.ringName = ring->name();
    start.ring = dec::RingGeometry{ring->ring().slotCount(), ring->ring().slotPayloadBytes()};
  }
  if (auto sent = send(dec::DecoderCommand{job_id, std::move(start)}); !sent) {
    const host::RequestError& e = sent.error();
    if (e.kind == host::RequestFailure::Rejected) {
      return std::unexpected(JobError{JobFailure::Refused, e.message, 0});
    }
    if (e.kind == host::RequestFailure::Closed) {
      return fail(JobFailure::PluginGone, "decoder plugin ended before job started: " + e.message);
    }
    // WHY: No answer to DECODE means the job may have been accepted: cancel it, so no job is left running.
    (void)send(dec::DecoderCommand{job_id, dec::CancelJob{}});
    return fail(JobFailure::Failed, "decoder plugin did not answer DECODE: " + e.message);
  }

  // WHY: Item feed and ring feed run on separate threads: the plugin may wait on either while holding the other.
  StopFlag stop{false};
  std::thread feeder;
  if (ring) {
    feeder = std::thread([&] {
      if (!options_.placement.asksNothing()) {
        if (auto placed = cpu::applyToThisThread(options_.placement); !placed) {
          log(name() + ": feed thread: " + placed.error());
        }
      }
      std::expected<void, FeedError> fed = {};
      try {
        fed = feed(*ring, stop);
      } catch (const std::bad_alloc&) {
        fed = std::unexpected(FeedError{FeedFailure::OutOfMemory, "out of memory feeding job"});
      } catch (const std::exception& e) {
        fed = std::unexpected(FeedError{FeedFailure::Failed, std::string("feed failed: ") + e.what()});
      }
      if (fed) {
        if (auto ended = ring->end(); !ended) fed = std::unexpected(FeedError{FeedFailure::Failed, ended.error().message});
      } else if (fed.error().kind == FeedFailure::Failed || fed.error().kind == FeedFailure::OutOfMemory) {
        // WHY: Ring stores ASCII only: BulkRing::markFailed() turns other bytes into '?'.
        (void)ring->fail(display_text::asciiText(fed.error().message));
      } else if (fed.error().kind == FeedFailure::Stopped) {
        (void)ring->fail("host stopped decode job");
      }
      {
        std::lock_guard lock(mutex_);
        job.feed = std::move(fed);
        job.delivered = ring->nextSample();
        job.feed_done = true;
        if (options.on_job_event) options.on_job_event(JobEvent::FeedDone);
      }
      cv_.notify_all();
    });
  } else {
    job.feed_done = true;
  }
  struct Join {
    StopFlag& stop;
    std::thread& thread;
    ~Join() {
      stop.store(true);
      if (thread.joinable()) thread.join();
    }
  } join{stop, feeder};

  const auto cancelled = [&options] { return options.cancel != nullptr && options.cancel->load(); };
  const auto feedFailed = [&job](FeedFailure kind) { return job.feed_done && !job.feed && job.feed.error().kind == kind; };
  std::optional<std::string> stale;
  std::optional<std::string> item_failure;
  bool item_failure_in_transit = false;
  bool stopping = false;
  std::chrono::steady_clock::time_point cancel_sent;
  bool ended_by_host = false;
  const uint64_t stale_after = options.timing.stale_after_ms;
  std::unique_lock lock(mutex_);

  std::vector<dec::Annotation> collected;
  std::map<std::string, std::vector<dec::StreamItem>> collected_items;
  std::vector<Job::Arrived> taking;
  std::vector<std::pair<std::string, std::vector<dec::StreamItem>>> taking_items;
  uint64_t taken = 0;
  uint64_t items_taken = 0;
  const auto handPending = [&] {
    if (job.pending.empty() && job.pending_items.empty()) return;
    taking.swap(job.pending);
    taking_items.swap(job.pending_items);
    lock.unlock();
    std::optional<std::string> out_of_memory;
    std::exception_ptr threw;
    for (const Job::Arrived& arrived : taking) {
      const std::vector<dec::Annotation>& batch = arrived.annotations;
      try {
        if (!batch.empty()) {
          if (!arrived.byte_values.empty()) options.on_byte_values(arrived.byte_values);
          if (on_annotations) {
            on_annotations(std::span<const dec::Annotation>(batch));
          } else {
            if (options.before_allocate) {
              options.before_allocate(AllocationSite::Annotations,
                                      (collected.size() + batch.size()) * sizeof(dec::Annotation));
            }
            collected.insert(collected.end(), batch.begin(), batch.end());
          }
          taken += batch.size();
        }
      } catch (const std::bad_alloc&) {
        if (on_annotations) {
          out_of_memory = "out of memory taking its annotations (" + std::to_string(taken) + " taken, " +
                          std::to_string(batch.size()) + " more arriving); rest were discarded";
        } else {
          out_of_memory = "out of memory holding its annotations (" + std::to_string(collected.size()) + " held, " +
                          std::to_string(batch.size()) + " more arriving); they were discarded";
          std::vector<dec::Annotation>().swap(collected);
        }
        break;
      } catch (...) {
        threw = std::current_exception();
        break;
      }
      // WHY: Watermark is sent after its batch, never before: it promises what comes after that batch.
      if (arrived.settled == 0 || !on_settled) continue;
      try {
        on_settled(arrived.settled);
      } catch (const std::bad_alloc&) {
        out_of_memory = "out of memory taking its watermark (" + std::to_string(taken) + " annotations taken)";
        break;
      } catch (...) {
        threw = std::current_exception();
        break;
      }
    }
    for (const auto& [output, batch] : taking_items) {
      if (out_of_memory || threw) break;
      try {
        if (on_items) {
          on_items(output, std::span<const dec::StreamItem>(batch));
        } else {
          std::vector<dec::StreamItem>& to = collected_items[output];
          if (options.before_allocate) {
            options.before_allocate(AllocationSite::Items, (to.size() + batch.size()) * sizeof(dec::StreamItem));
          }
          to.insert(to.end(), batch.begin(), batch.end());
        }
        items_taken += batch.size();
      } catch (const std::bad_alloc&) {
        if (on_items) {
          out_of_memory = "out of memory taking its items (" + std::to_string(items_taken) + " taken, " +
                          std::to_string(batch.size()) + " more arriving); rest were discarded";
        } else {
          out_of_memory = "out of memory holding its items (" + std::to_string(items_taken) + " held, " +
                          std::to_string(batch.size()) + " more arriving); they were discarded";
          std::map<std::string, std::vector<dec::StreamItem>>().swap(collected_items);
        }
        break;
      } catch (...) {
        threw = std::current_exception();
        break;
      }
    }
    taking.clear();
    taking_items.clear();
    lock.lock();
    if (out_of_memory && !job.out_of_memory) job.out_of_memory = std::move(out_of_memory);
    if (threw && !job.handler_threw) job.handler_threw = threw;
    if (job.out_of_memory || job.handler_threw) {
      job.pending.clear();
      job.pending_items.clear();
    }
  };

  const auto answer = [&](const std::string& input) {
    lock.unlock();
    if (options.before_item_feed) options.before_item_feed(input);
    if (cancelled()) {
      lock.lock();
      return;
    }
    std::optional<std::string> refused_here;
    std::optional<std::string> not_taken;
    bool in_transit = false;
    std::optional<std::string> out_of_memory;
    try {
      auto batch = feeds.at(input).next();
      if (!batch) {
        refused_here = std::move(batch.error());
      } else if (auto sent = send(dec::DecoderCommand{job_id, std::move(*batch)}); !sent) {
        const host::RequestError& e = sent.error();
        if (e.kind == host::RequestFailure::Rejected || e.kind == host::RequestFailure::Refused) {
          not_taken = "decoder plugin refused items of input '" + input + "': " + e.message;
        } else if (e.kind != host::RequestFailure::Closed) {
          not_taken = "decoder plugin did not take items of input '" + input + "': " + e.message;
          in_transit = true;
        }
      }
    } catch (const std::bad_alloc&) {
      out_of_memory = "out of memory reading items of input '" + input + "'";
    }
    lock.lock();
    if (refused_here && !job.undecodable) job.undecodable = std::move(refused_here);
    if (not_taken && !item_failure) {
      item_failure = std::move(not_taken);
      item_failure_in_transit = in_transit;
    }
    if (out_of_memory && !job.out_of_memory) job.out_of_memory = std::move(out_of_memory);
  };

  for (;;) {
    handPending();
    if (job.ended || closed_) break;
    if (!stopping) {
      if (ring && job.feed_done && job.feed && ring->ring().consumerState() == br::ConsumerState::Reading) {
        const uint64_t now = ring->ring().nowMs();
        const uint64_t beat = ring->ring().consumerHeartbeatMs();
        if (now > beat && now - beat > stale_after) {
          stale = "decoder plugin stopped reading its samples (no heartbeat for " + std::to_string(now - beat) + " ms)";
        }
      }
      if (feedFailed(FeedFailure::Failed) || feedFailed(FeedFailure::OutOfMemory) || cancelled() || job.out_of_memory ||
          job.handler_threw || job.undecodable || stale || item_failure) {
        stopping = true;
        stop.store(true);
        lock.unlock();
        (void)send(dec::DecoderCommand{job_id, dec::CancelJob{}});
        lock.lock();
        cancel_sent = std::chrono::steady_clock::now();
        continue;
      }
      if (!job.wanted.empty()) {
        const std::string input = std::move(job.wanted.front());
        job.wanted.pop_front();
        answer(input);
        continue;
      }
    } else if (std::chrono::steady_clock::now() - cancel_sent > options.cancel_grace) {
      ended_by_host = true;
      lock.unlock();
      connection_->close(std::chrono::milliseconds(0));
      lock.lock();
      continue;
    }
    if (job.pending.empty() && job.pending_items.empty()) cv_.wait_for(lock, kWatchInterval);
  }
  lock.unlock();
  // WHY: Stop and join the feed before reading the outcome: from here every fact is in.
  stop.store(true);
  if (feeder.joinable()) feeder.join();
  lock.lock();
  handPending();
  if (job.handler_threw) {
    const std::exception_ptr threw = job.handler_threw;
    lock.unlock();
    std::rethrow_exception(threw);
  }
  const bool ended = job.ended;
  const bool ended_unprompted = job.ended && !stopping;
  const std::optional<std::string> closed = closed_;
  lock.unlock();
  const uint64_t delivered = job.delivered;
  uint64_t items_fed = 0;
  for (const auto& [name, f] : feeds) items_fed += f.fed();

  // WHY: Reasons are decided once, after the job, from every fact: simultaneous ones resolve the same way each time.
  const auto hostStopped = [&](JobFailure kind, std::string_view message) {
    std::string text = prefixed(decoder, message);
    if (ended_by_host) {
      text += "; decoder plugin did not end job within " + std::to_string(options.cancel_grace.count()) +
              " ms of CANCEL and was ended";
    }
    return std::unexpected(JobError{kind, std::move(text), delivered});
  };
  // WHY: Samples that could not be delivered are reported ahead of a cancel: data integrity outranks user action.
  if (feedFailed(FeedFailure::Failed)) return hostStopped(JobFailure::Failed, job.feed.error().message);
  if (stale) return hostStopped(JobFailure::Failed, *stale);
  // WHY: A failure the plugin reports on its own outranks a cancel; one after CANCEL or a failed ring answers the stop.
  const auto notWhole = [&]() -> std::optional<std::string> {
    if (job.out_of_memory || !ended || job.end.status != dec::DecodeStatus::completed) return std::nullopt;
    if (ring && setup.facts.totalIsBound && job.end.samplesRead != delivered) {
      return "decoder plugin says it read " + std::to_string(job.end.samplesRead) + " samples, but host delivered " +
             std::to_string(delivered);
    }
    if (job.received != job.end.annotationCount) {
      return "decoder plugin says it sent " + std::to_string(job.end.annotationCount) + " annotations, but " +
             std::to_string(job.received) + " arrived: annotations were lost";
    }
    if (job.items_received != job.end.itemCount) {
      return "decoder plugin says it sent " + std::to_string(job.end.itemCount) + " items, but " +
             std::to_string(job.items_received) + " arrived: items were lost";
    }
    for (const auto& [name, f] : feeds) {
      if (!f.ended()) {
        return "decoder plugin reported job completed before host had fed every item of input '" + name + "' (" +
               std::to_string(f.fed()) + " fed)";
      }
    }
    if (inputs != nullptr && job.end.itemsRead != items_fed) {
      return "decoder plugin says it read " + std::to_string(job.end.itemsRead) + " items, but host fed " +
             std::to_string(items_fed) + ": items were lost";
    }
    return std::nullopt;
  };
  const auto pluginFailed = [&]() {
    return std::unexpected(
        JobError{JobFailure::Failed,
                 job.end.error.empty() ? prefixed(decoder, "decoder plugin failed job without reason") : job.end.error,
                 delivered});
  };
  if (ended_unprompted && job.end.status == dec::DecodeStatus::failure) return pluginFailed();
  if (job.undecodable) return hostStopped(JobFailure::Failed, *job.undecodable);
  // WHY: A feed refused by a plugin that then went by itself is reported as plugin gone, not as the failed write.
  if (item_failure && !(item_failure_in_transit && !ended && closed && !ended_by_host)) {
    if (ended && job.end.status == dec::DecodeStatus::failure) return pluginFailed();
    return hostStopped(JobFailure::Failed, *item_failure);
  }
  if (ended_unprompted) {
    if (const auto lost = notWhole()) return hostStopped(JobFailure::Failed, *lost);
  }
  // WHY: A cancel wins even when the job completed in the same instant.
  if (cancelled()) return hostStopped(JobFailure::Cancelled, "cancelled");
  if (job.out_of_memory) return hostStopped(JobFailure::OutOfMemory, *job.out_of_memory);
  if (feedFailed(FeedFailure::OutOfMemory)) return hostStopped(JobFailure::OutOfMemory, job.feed.error().message);
  if (!ended) {
    return fail(JobFailure::PluginGone, "decoder plugin ended during job: " + closed.value_or("closed"), delivered);
  }
  switch (job.end.status) {
    case dec::DecodeStatus::completed: {
      if (!job.feed) {
        return fail(JobFailure::Failed,
                    "decoder plugin reported job completed before host had delivered every sample (" +
                        std::to_string(delivered) + " delivered)",
                    delivered);
      }
      if (const auto lost = notWhole()) return hostStopped(JobFailure::Failed, *lost);
      JobResult result;
      result.annotations = std::move(collected);
      result.items = std::move(collected_items);
      result.samples_read = job.end.samplesRead;
      result.samples_delivered = delivered;
      result.items_read = job.end.itemsRead;
      result.items_fed = items_fed;
      return result;
    }
    case dec::DecodeStatus::failure:
      return pluginFailed();
    case dec::DecodeStatus::cancelled:
      return fail(JobFailure::Failed, "decoder plugin cancelled job, which host did not ask for", delivered);
    case dec::DecodeStatus::unspecified:
    default:
      return fail(JobFailure::Failed,
                  "decoder plugin ended job with status this host does not know (" +
                      std::to_string(static_cast<unsigned>(job.end.status)) + ")",
                  delivered);
  }
}

}

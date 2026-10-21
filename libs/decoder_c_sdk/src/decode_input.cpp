/*
 * SPDX-License-Identifier: MIT
 */

#include "decode_input.h"

#include <algorithm>
#include <cstring>
#include <exception>
#include <iterator>
#include <limits>
#include <new>
#include <string_view>
#include <utility>

#include "bitoscope_decoder_testing.h"
#include "display_text.h"
#include "stream.h"

namespace pr = bitoscope::plugin_ring;
namespace decode = bitoscope::decode;
namespace sdk = bitoscope::plugin_c_sdk;

namespace {

bool bitOf(const uint8_t* plane, uint64_t i) { return ((plane[i / 8] >> (i % 8)) & 1u) != 0; }

std::string describe(const bitoscope_annotation& a) {
  return "annotation [" + std::to_string(a.begin) + ", " + std::to_string(a.end) + ") in row " +
         std::to_string(a.row) + ", class " + std::to_string(a.cls) + ", value " + std::to_string(a.value);
}

std::string hostStreamFailed(uint64_t at_sample, uint64_t lost_samples, std::string_view reason) {
  return "host's stream failed at sample " + std::to_string(at_sample) + " (" + std::to_string(lost_samples) +
         " samples lost): " + bitoscope::display_text::displayText(reason);
}

}


struct bitoscope_decode_input::Items {
  struct Stream {
    sdk::ItemInputSpec spec;
    std::deque<sdk::ItemBatchIn> held;
    size_t pos = 0;
    bool requested = false;
    bool ended = false;
  };
  std::unique_ptr<sdk::ItemSource> source;
  std::vector<Stream> streams;
  uint64_t read = 0;
};


std::unique_ptr<bitoscope_decode_input> bitoscope_decode_input::fromMemory(
    const uint8_t* const* planes, uint32_t num_channels, uint64_t num_samples,
    std::span<const bitoscope_memory_record> records) {
  std::unique_ptr<bitoscope_decode_input> in(new bitoscope_decode_input());
  in->channels_ = num_channels;
  in->ended_ = true;
  const auto refuse = [&in](std::string why) {
    in->fail("in-memory input cannot be made: " + std::move(why));
    return std::move(in);
  };
  if (num_channels == 0) return refuse("it has no channels");
  if (planes == nullptr) return refuse("no planes were given");
  for (uint32_t c = 0; c < num_channels; ++c) {
    if (planes[c] == nullptr && num_samples != 0) return refuse("channel " + std::to_string(c) + " has no plane");
  }
  const bitoscope_memory_record whole{num_samples, 0};
  if (records.empty() && num_samples != 0) records = std::span<const bitoscope_memory_record>(&whole, 1);
  uint64_t total = 0;
  for (size_t r = 0; r < records.size(); ++r) {
    if (records[r].num_samples == 0) return refuse("record " + std::to_string(r) + " has no samples");
    if (records[r].num_samples > num_samples - total) {
      return refuse("its records hold more than " + std::to_string(num_samples) + " samples");
    }
    total += records[r].num_samples;
  }
  if (total != num_samples) {
    return refuse("its records hold " + std::to_string(total) + " samples, not " + std::to_string(num_samples) +
                  " samples of planes");
  }

  uint64_t first = 0;
  for (size_t r = 0; r < records.size(); ++r) {
    const uint64_t n = records[r].num_samples;
    Span span;
    span.view.first_sample = first;
    span.view.num_samples = n;
    span.view.released_mark = &in->mark_;
    if (records[r].skip != 0) {
      std::vector<uint64_t> levels((num_channels + 63) / 64, 0);
      for (uint32_t c = 0; c < num_channels; ++c) {
        const bool level = bitOf(planes[c], first);
        for (uint64_t i = 1; i < n; ++i) {
          if (bitOf(planes[c], first + i) != level) {
            return refuse("skip record " + std::to_string(r) + " (samples " + std::to_string(first) + " to " +
                          std::to_string(first + n - 1) + ") is not flat: channel " + std::to_string(c) +
                          " changes at sample " + std::to_string(first + i));
          }
        }
        if (level) levels[c / 64] |= uint64_t{1} << (c % 64);
      }
      in->storage_.push_back(std::move(levels));
      span.view.levels = reinterpret_cast<const uint8_t*>(in->storage_.back().data());
    } else {
      // WHY: Bits past the record's last sample are the OPPOSITE of it, so a read past the count cannot pass unseen.
      const uint64_t words = (n + 63) / 64;
      std::vector<uint64_t> data(static_cast<size_t>(words * num_channels), 0);
      for (uint32_t c = 0; c < num_channels; ++c) {
        uint64_t* plane = data.data() + c * words;
        for (uint64_t i = 0; i < n; ++i) {
          if (bitOf(planes[c], first + i)) plane[i / 64] |= uint64_t{1} << (i % 64);
        }
        if (n % 64 != 0 && !bitOf(planes[c], first + n - 1)) plane[words - 1] |= ~uint64_t{0} << (n % 64);
      }
      in->storage_.push_back(std::move(data));
      span.view.planes = reinterpret_cast<const uint8_t*>(in->storage_.back().data());
      span.view.plane_stride = words * 8;
    }
    in->window_.push_back(span);
    first += n;
  }
  in->arrived_ = num_samples;
  return in;
}

std::unique_ptr<bitoscope_decode_input> bitoscope_decode_input::fromRing(pr::DecodeRingReader reader) {
  std::unique_ptr<bitoscope_decode_input> in(new bitoscope_decode_input());
  in->channels_ = reader.numChannels();
  in->reader_.emplace(std::move(reader));
  return in;
}

std::unique_ptr<bitoscope_decode_input> bitoscope_decode_input::fromItems(uint64_t total_samples,
                                                                         std::vector<sdk::ItemInputSpec> inputs,
                                                                         std::unique_ptr<sdk::ItemSource> source) {
  std::unique_ptr<bitoscope_decode_input> in(new bitoscope_decode_input());
  in->samples_ = false;
  in->arrived_ = total_samples;
  in->ended_ = true;
  in->items_ = std::make_unique<Items>();
  in->items_->source = std::move(source);
  for (sdk::ItemInputSpec& spec : inputs) in->items_->streams.push_back({std::move(spec), {}, 0, false, false});
  return in;
}

std::expected<void, std::string> bitoscope_decode_input::addItems(std::vector<sdk::ItemInputSpec> inputs,
                                                                  std::unique_ptr<sdk::ItemSource> source) {
  if (!samples_) return std::unexpected(std::string("input holds no samples: its items are all it reads"));
  if (items_) return std::unexpected(std::string("input has its items already"));
  auto items = std::make_unique<Items>();
  items->source = std::move(source);
  for (sdk::ItemInputSpec& spec : inputs) items->streams.push_back({std::move(spec), {}, 0, false, false});
  items_ = std::move(items);
  return {};
}

int bitoscope_decode_input::nextItem(uint32_t input, bitoscope_stream_item* item) {
  if (failed_) return BITOSCOPE_ERROR;
  if (!items_) {
    fail("input " + std::to_string(input) + " was asked for, but this job reads samples, not stream items");
    return BITOSCOPE_ERROR;
  }
  Items& items = *items_;
  if (input >= items.streams.size()) {
    fail("input " + std::to_string(input) + " was asked for, but decoder declares " +
         std::to_string(items.streams.size()) + (items.streams.size() == 1 ? " input" : " inputs"));
    return BITOSCOPE_ERROR;
  }
  if (item == nullptr) {
    fail("bitoscope_next_item() was given no item to fill");
    return BITOSCOPE_ERROR;
  }
  Items::Stream& stream = items.streams[input];
  if (!stream.spec.bound) {
    fail("input '" + stream.spec.name + "' is not bound in this job");
    return BITOSCOPE_ERROR;
  }
  tick();
  if (failed_) return BITOSCOPE_ERROR;

  const auto ask = [&]() {
    auto asked = items.source->request(input);
    if (!asked) {
      fail(std::move(asked.error()));
      return false;
    }
    stream.requested = true;
    return true;
  };
  try {
    for (;;) {
      if (stream.ended) return BITOSCOPE_NONE;
      if (!stream.held.empty()) {
        sdk::ItemBatchIn& front = stream.held.front();
        if (stream.pos < front.items.size()) {
          if (stream.pos == 0 && !front.last && !stream.requested && stream.held.size() == 1 && !ask()) {
            return BITOSCOPE_ERROR;
          }
          *item = front.items[stream.pos++];
          ++items.read;
          return BITOSCOPE_OK;
        }
        const bool last = front.last;
        stream.held.pop_front();
        stream.pos = 0;
        if (last) {
          stream.ended = true;
          stream.held.clear();
          return BITOSCOPE_NONE;
        }
        continue;
      }
      if (!stream.requested && !ask()) return BITOSCOPE_ERROR;
      for (;;) {
        if (cancelled_.load(std::memory_order_relaxed)) {
          fail("decode job was cancelled while waiting for items of input '" + stream.spec.name + "'");
          return BITOSCOPE_ERROR;
        }
        // WHY: Keep the ring heartbeat while waiting for items: the host's feed may wait for ring room and see it stale.
        if (reader_) reader_->heartbeat();
        auto got = items.source->await(input, sdk::kInputWaitSliceMs);
        if (!got) {
          fail(std::move(got.error()));
          return BITOSCOPE_ERROR;
        }
        if (*got) {
          stream.held.push_back(std::move(**got));
          stream.requested = false;
          break;
        }
      }
    }
  } catch (const std::bad_alloc&) {
    fail("out of memory for items of input '" + stream.spec.name + "'");
    return BITOSCOPE_ERROR;
  }
}

uint64_t bitoscope_decode_input::itemsRead() const { return items_ ? items_->read : 0; }

bitoscope_decode_input::~bitoscope_decode_input() = default;


void bitoscope_decode_input::fail(std::string reason) {
  if (!failed_) {
    failed_ = true;
    error_ = std::move(reason);
  }
}

void bitoscope_decode_input::failIntegrity(std::string reason) {
  if (!failed_) integrity_ = true;
  fail(std::move(reason));
}

void bitoscope_decode_input::failThrown() noexcept {
  if (failed_) return;
  failed_ = true;
  try {
    error_ = "decode job failed: out of memory, or function SDK called threw";
  } catch (...) {
  }
}

void bitoscope_decode_input::beat() {
  countdown_ = sdk::kHeartbeatEvery;
  if (cancelled_.load(std::memory_order_relaxed)) {
    fail("decode job was cancelled");
    return;
  }
  if (reader_) reader_->heartbeat();
}

bool bitoscope_decode_input::checkChannel(uint32_t channel) {
  if (channel < channels_) return true;
  const std::string asked = "channel " + std::to_string(channel) + " was asked for, but job binds ";
  if (channels_ == 0) {
    fail(asked + "no channel");
  } else {
    fail(asked + std::to_string(channels_) +
         (channels_ == 1 ? " channel (channel 0)" : " channels (0 to " + std::to_string(channels_ - 1) + ")"));
  }
  return false;
}


const bitoscope_span* bitoscope_decode_input::spanAt(uint64_t sample) {
  if (failed_) return nullptr;
  if (!samples_) {
    fail("sample " + std::to_string(sample) + " was asked for, but this job reads stream items, not samples");
    return nullptr;
  }
  if (sample < mark_) {
    failLookBack(sample);
    return nullptr;
  }
  while (sample >= arrived_) {
    if (ended_ || !fetch(sample)) return nullptr;
  }
  uint64_t index = (std::max)(hint_, window_base_);
  if (index - window_base_ >= window_.size() || sample < window_[index - window_base_].view.first_sample) {
    const auto it = std::upper_bound(window_.begin(), window_.end(), sample,
                                     [](uint64_t s, const Span& span) { return s < span.view.first_sample; });
    index = window_base_ + static_cast<uint64_t>(it - window_.begin()) - (it == window_.begin() ? 0 : 1);
  } else {
    while (index - window_base_ + 1 < window_.size() && sample >= window_[index - window_base_].end()) ++index;
  }
  const Span& span = window_[index - window_base_];
  if (sample < span.view.first_sample || sample >= span.end()) {
    failIntegrity("sample " + std::to_string(sample) + " is inside samples that arrived (to " + std::to_string(arrived_) +
         ") but in no record input holds: input is broken");
    return nullptr;
  }
  hint_ = index;
  return &span.view;
}

bool bitoscope_decode_input::fetch(uint64_t wanted) {
  if (!reader_) {
    ended_ = true;
    return false;
  }
  if (reader_->ring().produced() == reader_->ring().readingSlot()) ++waits_;
  for (;;) {
    if (cancelled_.load(std::memory_order_relaxed)) {
      fail("decode job was cancelled while waiting for sample " + std::to_string(wanted));
      return false;
    }
    const pr::DecodeRead read = reader_->take(reader_->ring().nowMs() + sdk::kInputWaitSliceMs);
    switch (read.status) {
      case pr::DecodeReadStatus::Record:
        hold(read.record);
        return true;
      case pr::DecodeReadStatus::Pending:
        continue;
      case pr::DecodeReadStatus::Ended:
        ended_ = true;
        return false;
      case pr::DecodeReadStatus::Failed:
        failIntegrity("sample " + std::to_string(wanted) + " never came: " +
                      hostStreamFailed(read.failed.at_sample, read.failed.lost_samples, read.failed.reason));
        return false;
      case pr::DecodeReadStatus::Invalid:
        failIntegrity("host's stream is broken: " + read.error);
        return false;
      case pr::DecodeReadStatus::WindowFull:
        letGoOfOlderHalf(wanted);
        if (failed_) return false;
        continue;
    }
  }
}

void bitoscope_decode_input::letGoOfOlderHalf(uint64_t wanted) {
  let_go_for_ = wanted;
  const size_t keep = window_.size() / 2;
  mark_ = keep == 0 ? arrived_ : window_[window_.size() - keep].view.first_sample;
  trim();
}

void bitoscope_decode_input::failLookBack(uint64_t sample) {
  if (sample < released_by_decoder_) {
    fail("sample " + std::to_string(sample) + " was asked for, but it is before released mark " +
         std::to_string(released_by_decoder_) + ": decoder said it would not look back past it (bitoscope_release_before)");
    return;
  }
  fail("sample " + std::to_string(sample) + " was asked for, but input no longer holds it: ring's window of " +
       std::to_string(reader_ ? reader_->slotCount() : 0) + " slots was full, so it let go of samples before " +
       std::to_string(mark_) + " to read on to sample " + std::to_string(let_go_for_) +
       "; to look back this far, decoder declares larger window (look-ahead)");
}

void bitoscope_decode_input::hold(const pr::DecodeRecord& r) {
  Span span;
  span.view.first_sample = r.first_sample;
  span.view.num_samples = r.num_samples;
  span.view.released_mark = &mark_;
  span.slot = r.slot;
  if (r.kind == pr::DecodeRecordKind::Planes) {
    span.view.planes = r.payload.data();
    span.view.plane_stride = r.plane_stride;
  } else {
    span.view.levels = r.payload.data();
  }
  window_.push_back(span);
  arrived_ = r.first_sample + r.num_samples;
  trim();
}

void bitoscope_decode_input::trim() {
  while (!window_.empty() && window_.front().end() <= mark_) {
    window_.pop_front();
    ++window_base_;
  }
  if (!reader_) return;
  const uint64_t keep = window_.empty() ? reader_->ring().readingSlot() : window_.front().slot;
  if (auto released = reader_->releaseSlotsBefore(keep); !released) fail(released.error());
}

void bitoscope_decode_input::releaseBefore(uint64_t sample) {
  if (failed_) return;
  released_by_decoder_ = (std::max)(released_by_decoder_, sample);
  if (sample <= mark_) return;
  mark_ = sample;
  trim();
}

void bitoscope_decode_input::takeArrived() {
  if (!reader_ || ended_ || failed_) return;
  for (;;) {
    const pr::DecodeRead read = reader_->take(reader_->ring().nowMs());
    switch (read.status) {
      case pr::DecodeReadStatus::Record:
        hold(read.record);
        continue;
      case pr::DecodeReadStatus::Ended:
        ended_ = true;
        return;
      case pr::DecodeReadStatus::Failed:
        failIntegrity(hostStreamFailed(read.failed.at_sample, read.failed.lost_samples, read.failed.reason));
        return;
      case pr::DecodeReadStatus::Invalid:
        failIntegrity("host's stream is broken: " + read.error);
        return;
      case pr::DecodeReadStatus::Pending:
      case pr::DecodeReadStatus::WindowFull:
        return;
    }
  }
}

bool bitoscope_decode_input::reaches(uint64_t samples) {
  releaseBefore((std::numeric_limits<uint64_t>::max)());
  while (arrived_ < samples && !ended_ && !failed_) {
    if (!fetch(samples - 1)) break;
  }
  return arrived_ >= samples;
}

bool bitoscope_decode_input::drain() {
  if (samples_) {
    releaseBefore((std::numeric_limits<uint64_t>::max)());
    while (!ended_ && !failed_) {
      if (!fetch(arrived_)) break;
    }
    if (!ended_ || failed_) return false;
  }
  if (items_) {
    for (uint32_t i = 0; i < items_->streams.size(); ++i) {
      if (!items_->streams[i].spec.bound) continue;
      bitoscope_stream_item item{};
      int got = BITOSCOPE_OK;
      while ((got = nextItem(i, &item)) == BITOSCOPE_OK) {
      }
      if (got != BITOSCOPE_NONE) return false;
    }
  }
  return !failed_;
}



struct bitoscope_annotations::Items final : decode::StreamSink {
  Items(uint64_t samples, std::vector<decode::GuardedOutput> outputs, DeliverItems deliver_to, size_t batch_size)
      : pending(outputs.size()),
        collected(outputs.size()),
        deliver(std::move(deliver_to)),
        batch(batch_size == 0 ? 1 : batch_size),
        guard(*this, samples, std::move(outputs)) {}

  void add(uint32_t output, std::span<const decode::StreamItem> items) override {
    for (const decode::StreamItem& i : items) pending[output].push_back({i.begin, i.end, i.value, i.kind, i.flags});
  }

  std::vector<std::vector<bitoscope_stream_item>> pending;
  std::vector<std::vector<bitoscope_stream_item>> collected;
  DeliverItems deliver;
  size_t batch;
  bool anyPut = false;
  decode::GuardedStreamSink guard;  // WHY: guard is declared last: it forwards to this object, so it must be destroyed first.
};

bitoscope_annotations::bitoscope_annotations(bitoscope_decode_input* in, Deliver deliver, size_t batch)
    : in_(in), deliver_(std::move(deliver)), batch_(batch == 0 ? 1 : batch) {}

bitoscope_annotations::~bitoscope_annotations() = default;

std::expected<void, std::string> bitoscope_annotations::openOutputs(std::span<const bitoscope_job_output> outputs,
                                                                    uint64_t samples, DeliverItems deliver,
                                                                    size_t batch) {
  if (items_ && items_->anyPut) return std::unexpected(std::string("outputs are set before first item"));
  std::vector<decode::GuardedOutput> guarded;
  guarded.reserve(outputs.size());
  for (const bitoscope_job_output& o : outputs) {
    decode::StreamType type = decode::StreamType::Words;
    if (o.type == BITOSCOPE_STREAM_WORDS) {
      type = decode::StreamType::Words;
    } else if (o.type == BITOSCOPE_STREAM_SPI_TRANSFERS) {
      type = decode::StreamType::SpiTransfers;
    } else {
      return std::unexpected("output '" + std::string(o.name != nullptr ? o.name : "") + "' has stream type " +
                             std::to_string(o.type) +
                             ", not BITOSCOPE_STREAM_WORDS or BITOSCOPE_STREAM_SPI_TRANSFERS");
    }
    guarded.push_back({o.name != nullptr ? o.name : "", type, o.width, o.wanted != 0});
  }
  items_ = std::make_unique<Items>(samples, std::move(guarded), std::move(deliver), batch);
  return {};
}

int bitoscope_annotations::put(uint32_t output, const bitoscope_stream_item& item) {
  in_->tick();
  if (in_->failed()) return BITOSCOPE_ERROR;
  try {
    if (finished_) {
      in_->fail("stream item [" + std::to_string(item.begin) + ", " + std::to_string(item.end) + ") of output " +
                std::to_string(output) + " was put after job finished");
      return BITOSCOPE_ERROR;
    }
    if (!items_) items_ = std::make_unique<Items>(0, std::vector<decode::GuardedOutput>{}, DeliverItems{}, 1);
    Items& items = *items_;
    items.anyPut = true;
    const decode::StreamItem checked{item.begin, item.end, item.value, item.kind, item.flags};
    items.guard.add(output, std::span<const decode::StreamItem>(&checked, 1));
    if (!items.guard.error().empty()) {
      in_->fail(items.guard.error());
      return BITOSCOPE_ERROR;
    }
    if (items.pending[output].size() >= items.batch && !handOnItems(output)) return BITOSCOPE_ERROR;
  } catch (const std::bad_alloc&) {
    in_->fail("out of memory for stream items");
    return BITOSCOPE_ERROR;
  }
  return BITOSCOPE_OK;
}

std::span<const bitoscope_stream_item> bitoscope_annotations::collectedItems(uint32_t output) const {
  if (!items_ || output >= items_->collected.size()) return {};
  return items_->collected[output];
}

bool bitoscope_annotations::handOnItems(uint32_t output) {
  Items& items = *items_;
  std::vector<bitoscope_stream_item>& pending = items.pending[output];
  if (pending.empty()) return true;
  if (items.deliver) {
    auto sent = items.deliver(output, pending);
    if (!sent) {
      in_->fail(sent.error());
      return false;
    }
  } else {
    items.collected[output].insert(items.collected[output].end(), pending.begin(), pending.end());
  }
  pending.clear();
  return true;
}

int bitoscope_annotations::emit(uint64_t begin, uint64_t end, uint32_t row, uint32_t cls, uint64_t value) {
  in_->tick();
  if (in_->failed()) return BITOSCOPE_ERROR;
  return admit(bitoscope_annotation{begin, end, row, cls, value});
}

int bitoscope_annotations::emitBytes(uint64_t begin, uint64_t end, uint32_t row, uint32_t cls,
                                     std::span<const uint8_t> bytes) {
  in_->tick();
  if (in_->failed()) return BITOSCOPE_ERROR;
  const bitoscope_annotation a{begin, end, row, cls, 0};
  if (bytes.empty()) {
    in_->fail("annotation [" + std::to_string(begin) + ", " + std::to_string(end) + ") in row " +
              std::to_string(row) + ", class " + std::to_string(cls) +
              " has no bytes: bytes value needs at least one byte");
    return BITOSCOPE_ERROR;
  }
  try {
    byte_values_.emplace_back(bytes.begin(), bytes.end());
    byte_owners_.push_back(handed_ + pending_.size());
  } catch (const std::bad_alloc&) {
    in_->fail("out of memory for annotations");
    return BITOSCOPE_ERROR;
  }
  return admit(a);
}

std::span<const uint8_t> bitoscope_annotations::collectedBytes(uint64_t reference) const {
  if (reference >= collected_bytes_.size()) return {};
  return collected_bytes_[reference];
}

int bitoscope_annotations::admit(const bitoscope_annotation& a) {
  const uint64_t begin = a.begin;
  const uint64_t end = a.end;
  try {
    if (finished_) {
      in_->fail(describe(a) + " was emitted after job finished");
      return BITOSCOPE_ERROR;
    }
    if (end == begin) {
      in_->fail(describe(a) + " is empty: it ends where it begins");
      return BITOSCOPE_ERROR;
    }
    if (end < begin) {
      in_->fail(describe(a) + " ends before it begins");
      return BITOSCOPE_ERROR;
    }
    if (in_->ended() && end > in_->arrived()) {
      in_->fail(describe(a) + " ends past capture's " + std::to_string(in_->arrived()) + " samples");
      return BITOSCOPE_ERROR;
    }
    pending_.push_back(a);
    while (checked_ < pending_.size() && pending_[checked_].end <= in_->arrived()) ++checked_;
    const bool due = releaseWaiting() && strideDue();
    if ((checked_ >= batch_ || due) && !handOn(checked_)) return BITOSCOPE_ERROR;
  } catch (const std::bad_alloc&) {
    in_->fail("out of memory for annotations");
    return BITOSCOPE_ERROR;
  }
  return BITOSCOPE_OK;
}

int bitoscope_annotations::settle(uint64_t sample) {
  ++settle_calls_;
  if (in_->failed()) return BITOSCOPE_ERROR;
  if (sample == given_) return BITOSCOPE_OK;
  const bool atEnd = in_->ended() && sample == in_->arrived();
  if (!atEnd && sample < settleDue()) return BITOSCOPE_OK;
  try {
    const uint64_t emitted = handed_ + pending_.size();
    if (!deliver_) settles_.push_back({sample, emitted});
    if (sample < given_) return BITOSCOPE_OK;
    given_ = sample;
    if (checked_ != pending_.size()) {
      waiting_ = sample;
      waiting_at_ = emitted;
      return BITOSCOPE_OK;
    }
    ready_ = sample;
    waiting_ = 0;
    if (strideDue() && !handOn(checked_)) return BITOSCOPE_ERROR;
  } catch (const std::bad_alloc&) {
    in_->fail("out of memory for annotations");
    return BITOSCOPE_ERROR;
  }
  return BITOSCOPE_OK;
}

uint64_t bitoscope_annotations::settleDue() const {
  const uint64_t step = in_->effectiveProgressStep();
  return step > UINT64_MAX - given_ ? UINT64_MAX : given_ + step;
}

int bitoscope_annotations::markEnd(uint64_t end) {
  if (in_->failed()) return BITOSCOPE_ERROR;
  if (end <= passed_) return BITOSCOPE_OK;
  try {
    if (!deliver_ && end > given_) settles_.push_back({end, handed_ + pending_.size()});
    given_ = (std::max)(given_, end);
    ready_ = end;
    waiting_ = 0;
    if (!handOn(pending_.size())) return BITOSCOPE_ERROR;
  } catch (const std::bad_alloc&) {
    in_->fail("out of memory for annotations");
    return BITOSCOPE_ERROR;
  }
  return BITOSCOPE_OK;
}

bool bitoscope_annotations::releaseWaiting() {
  if (waiting_ == 0 || waiting_at_ > handed_ + checked_) return false;
  ready_ = waiting_;
  waiting_ = 0;
  return true;
}

bool bitoscope_annotations::strideDue() const {
  return ready_ - passed_ >= in_->watermarkStride();
}

int bitoscope_annotations::finish() {
  if (in_->failed()) return BITOSCOPE_ERROR;
  try {
    if (!finished_) {
      finished_ = true;
      uint64_t need = 0;
      for (size_t i = checked_; i < pending_.size(); ++i) need = (std::max)(need, pending_[i].end);
      if (need > in_->arrived() && !in_->reaches(need)) {
        if (in_->failed()) return BITOSCOPE_ERROR;
        for (size_t i = checked_; i < pending_.size(); ++i) {
          if (pending_[i].end > in_->arrived()) {
            in_->fail(describe(pending_[i]) + " ends past capture's " + std::to_string(in_->arrived()) +
                      " samples");
            return BITOSCOPE_ERROR;
          }
        }
      }
      checked_ = pending_.size();
      (void)releaseWaiting();
      if ((!pending_.empty() || ready_ != passed_) && !handOn(pending_.size())) return BITOSCOPE_ERROR;
      if (items_) {
        for (uint32_t o = 0; o < items_->pending.size(); ++o) {
          if (!handOnItems(o)) return BITOSCOPE_ERROR;
        }
      }
    }
  } catch (const std::bad_alloc&) {
    in_->fail("out of memory for annotations and stream items");
    return BITOSCOPE_ERROR;
  }
  return in_->failed() ? BITOSCOPE_ERROR : BITOSCOPE_OK;
}

bool bitoscope_annotations::handOn(size_t n) {
  size_t withBytes = 0;
  while (withBytes < byte_owners_.size() && byte_owners_[withBytes] < handed_ + n) ++withBytes;
  for (size_t k = 0; k < withBytes; ++k) pending_[byte_owners_[k] - handed_].value = k;
  const std::span<const bitoscope_annotation> batch(pending_.data(), n);
  if (check_) {
    if (auto why = check_(batch, withBytes)) {
      in_->fail(std::move(*why));
      return false;
    }
  }
  if (withBytes != 0) {
    if (!deliver_) {
      for (size_t k = 0; k < withBytes; ++k) pending_[byte_owners_[k] - handed_].value += collected_bytes_.size();
      collected_bytes_.insert(collected_bytes_.end(), std::make_move_iterator(byte_values_.begin()),
                              std::make_move_iterator(byte_values_.begin() + static_cast<std::ptrdiff_t>(withBytes)));
    } else if (!deliver_bytes_) {
      in_->fail("annotations with bytes values cannot be sent: nothing takes their bytes");
      return false;
    } else if (auto sent = deliver_bytes_(std::span<const std::vector<uint8_t>>(byte_values_.data(), withBytes));
               !sent) {
      in_->fail(sent.error());
      return false;
    }
  }
  if (deliver_) {
    auto sent = deliver_(batch, ready_ != passed_ ? ready_ : 0);
    if (!sent) {
      in_->fail(sent.error());
      return false;
    }
  } else {
    collected_.append(batch);
  }
  passed_ = ready_;
  handed_ += n;
  ++batches_;
  pending_.erase(pending_.begin(), pending_.begin() + static_cast<std::ptrdiff_t>(n));
  byte_values_.erase(byte_values_.begin(), byte_values_.begin() + static_cast<std::ptrdiff_t>(withBytes));
  byte_owners_.erase(byte_owners_.begin(), byte_owners_.begin() + static_cast<std::ptrdiff_t>(withBytes));
  checked_ -= n;
  return true;
}

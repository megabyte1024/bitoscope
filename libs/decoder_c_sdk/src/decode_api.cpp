/*
 * SPDX-License-Identifier: MIT
 */

#include <cstdint>
#include <vector>
#include <utility>
#include <optional>
#include <expected>
#include <algorithm>
#include <array>
#include <exception>
#include <memory>
#include <new>
#include <span>
#include <string>

#if defined(_MSC_VER) && defined(_M_X64)
#include <intrin.h>
#endif

#include "bitoscope_decoder.h"
#include "bitoscope_decoder_cpp.h"
#include "bitoscope_decoder_layers.h"
#include "bitoscope_decoder_testing.h"
#include "decode_input.h"

namespace detail = bitoscope::plugin::detail;
namespace plugin = bitoscope::plugin;

namespace {

constexpr size_t kMaxWalked = 32;

int noneOrError(const bitoscope_decode_input* in) { return in->failed() ? BITOSCOPE_ERROR : BITOSCOPE_NONE; }

struct SpanGetter {
  bitoscope_decode_input* in;
  void noteWords(uint64_t words) const { in->noteScanned(words); }
  bool operator()(uint64_t sample, bitoscope_span& out) const {
    const bitoscope_span* s = in->spanAt(sample);
    if (s == nullptr) return false;
    out = *s;
    return true;
  }
};

int thrown(bitoscope_decode_input* in) noexcept {
  in->failThrown();
  return BITOSCOPE_ERROR;
}

bool validKind(bitoscope_decode_input* in, int kind, const char* what) {
  if (kind == BITOSCOPE_EDGE_RISING || kind == BITOSCOPE_EDGE_FALLING || kind == BITOSCOPE_EDGE_ANY) return true;
  in->fail(std::string(what) + ": edge kind " + std::to_string(kind) +
           " is not BITOSCOPE_EDGE_RISING, BITOSCOPE_EDGE_FALLING or BITOSCOPE_EDGE_ANY");
  return false;
}

bool atMost64(bitoscope_decode_input* in, uint32_t n, const char* what) {
  if (n <= 64) return true;
  in->fail(std::string(what) + ": " + std::to_string(n) + " bits asked for; at most 64 fit value");
  return false;
}

bool mulDiv(uint64_t a, uint64_t b, uint64_t c, uint64_t* q) {
  if (c == 0) return false;
#if defined(_MSC_VER) && defined(_M_X64)
  uint64_t hi = 0;
  const uint64_t lo = _umul128(a, b, &hi);
  if (hi >= c) return false;
  uint64_t rem = 0;
  *q = _udiv128(hi, lo, c, &rem);
  return true;
#elif defined(__SIZEOF_INT128__)
  __extension__ typedef unsigned __int128 Uint128;
  const Uint128 p = static_cast<Uint128>(a) * b / c;
  if (p > UINT64_MAX) return false;
  *q = static_cast<uint64_t>(p);
  return true;
#else
  if (a != 0 && b > UINT64_MAX / a) return false;
  *q = a * b / c;
  return true;
#endif
}

class MemoryItems final : public bitoscope::plugin_c_sdk::ItemSource {
 public:
  explicit MemoryItems(std::vector<std::vector<bitoscope_stream_item>> items)
      : items_(std::move(items)), at_(items_.size(), 0), arrived_(items_.size()) {}

  std::expected<void, std::string> request(uint32_t input) override {
    std::vector<bitoscope_stream_item>& all = items_[input];
    const size_t n = (std::min)(bitoscope::plugin_c_sdk::kMemoryItemBatch, all.size() - at_[input]);
    bitoscope::plugin_c_sdk::ItemBatchIn batch;
    batch.items.assign(all.begin() + static_cast<std::ptrdiff_t>(at_[input]),
                       all.begin() + static_cast<std::ptrdiff_t>(at_[input] + n));
    at_[input] += n;
    batch.last = at_[input] == all.size();
    arrived_[input] = std::move(batch);
    return {};
  }
  std::expected<std::optional<bitoscope::plugin_c_sdk::ItemBatchIn>, std::string> await(uint32_t input,
                                                                                         uint64_t) override {
    return std::exchange(arrived_[input], std::nullopt);
  }

 private:
  std::vector<std::vector<bitoscope_stream_item>> items_;
  std::vector<size_t> at_;
  std::vector<std::optional<bitoscope::plugin_c_sdk::ItemBatchIn>> arrived_;
};

}

std::unique_ptr<bitoscope::plugin_c_sdk::ItemSource> bitoscope::plugin_c_sdk::memoryItems(
    std::vector<std::vector<bitoscope_stream_item>> items) {
  return std::make_unique<MemoryItems>(std::move(items));
}

extern "C" {

void bitoscope_test_count_scans(bitoscope_decode_input* in, uint64_t* counter) {
  if (in != nullptr) in->countScans(counter);
}

uint64_t* bitoscope_test_scan_counter(const bitoscope_decode_input* in) {
  return in == nullptr ? nullptr : in->scanCounter();
}

uint32_t bitoscope_input_channels(const bitoscope_decode_input* in) { return in == nullptr ? 0 : in->channels(); }

int bitoscope_level(bitoscope_decode_input* in, uint32_t channel, uint64_t sample, int* level) {
  if (in == nullptr) return BITOSCOPE_ERROR;
  try {
    in->tick();
    if (level == nullptr) {
      in->fail("bitoscope_level: no place given for level");
      return BITOSCOPE_ERROR;
    }
    if (!in->checkChannel(channel)) return BITOSCOPE_ERROR;
    const bitoscope_span* s = in->spanAt(sample);
    if (s == nullptr) return noneOrError(in);
    *level = detail::spanLevel(*s, channel, sample - s->first_sample) ? 1 : 0;
    return BITOSCOPE_OK;
  } catch (...) {
    return thrown(in);
  }
}

int bitoscope_next_edge(bitoscope_decode_input* in, uint32_t channel, uint64_t from, int kind, uint64_t* at) {
  if (in == nullptr || at == nullptr) return BITOSCOPE_ERROR;
  try {
    in->tick();
    if (!in->checkChannel(channel) || !validKind(in, kind, "bitoscope_next_edge")) return BITOSCOPE_ERROR;
    SpanGetter get{in};
    const int r = detail::nextEdgeAcross(get, channel, from, kind, UINT64_MAX, at);
    return r == BITOSCOPE_OK ? r : noneOrError(in);
  } catch (...) {
    return thrown(in);
  }
}

int bitoscope_next_change2(bitoscope_decode_input* in, uint32_t channel_a, uint32_t channel_b, uint64_t from,
                           uint64_t* at, uint32_t* which) {
  if (in == nullptr || at == nullptr || which == nullptr) return BITOSCOPE_ERROR;
  try {
    in->tick();
    if (!in->checkChannel(channel_a) || !in->checkChannel(channel_b)) return BITOSCOPE_ERROR;
    SpanGetter get{in};
    const int r = detail::nextChange2Across(get, channel_a, channel_b, from, UINT64_MAX, at, which);
    return r == BITOSCOPE_OK ? r : noneOrError(in);
  } catch (...) {
    return thrown(in);
  }
}

int bitoscope_next_edge_limit(bitoscope_decode_input* in, uint32_t channel, uint64_t from, int kind,
                              uint64_t limit, uint64_t* at) {
  if (in == nullptr || at == nullptr) return BITOSCOPE_ERROR;
  try {
    in->tick();
    if (!in->checkChannel(channel) || !validKind(in, kind, "bitoscope_next_edge_limit")) return BITOSCOPE_ERROR;
    SpanGetter get{in};
    const int r = detail::nextEdgeAcross(get, channel, from, kind, limit, at);
    return r == BITOSCOPE_OK ? r : noneOrError(in);
  } catch (...) {
    return thrown(in);
  }
}

int bitoscope_next_change2_limit(bitoscope_decode_input* in, uint32_t channel_a, uint32_t channel_b,
                                 uint64_t from, uint64_t limit, uint64_t* at, uint32_t* which) {
  if (in == nullptr || at == nullptr || which == nullptr) return BITOSCOPE_ERROR;
  try {
    in->tick();
    if (!in->checkChannel(channel_a) || !in->checkChannel(channel_b)) return BITOSCOPE_ERROR;
    SpanGetter get{in};
    const int r = detail::nextChange2Across(get, channel_a, channel_b, from, limit, at, which);
    return r == BITOSCOPE_OK ? r : noneOrError(in);
  } catch (...) {
    return thrown(in);
  }
}

int bitoscope_for_each_change(bitoscope_decode_input* in, const bitoscope_watched_line* watched, size_t num_watched,
                              const uint32_t* sampled, size_t num_sampled, bitoscope_change_fn on_change, void* user) {
  if (in == nullptr) return BITOSCOPE_ERROR;
  try {
    in->tick();
    constexpr const char* name = "bitoscope_for_each_change";
    if (num_watched < 1 || num_watched > kMaxWalked || num_sampled > kMaxWalked) {
      in->fail(std::string(name) + ": " + std::to_string(num_watched) + " lines watched and " +
               std::to_string(num_sampled) + " sampled; 1 to 32 watched and at most 32 sampled are walked");
      return BITOSCOPE_ERROR;
    }
    if (watched == nullptr || (num_sampled != 0 && sampled == nullptr) || on_change == nullptr) {
      in->fail(std::string(name) + ": no lines, sampled channels or callback were given");
      return BITOSCOPE_ERROR;
    }
    std::array<plugin::WatchedLine, kMaxWalked> lines{};
    std::array<uint32_t, kMaxWalked> data{};
    for (size_t i = 0; i < num_watched; ++i) {
      if (!validKind(in, watched[i].edge, name)) return BITOSCOPE_ERROR;
      lines[i] = {watched[i].channel, static_cast<plugin::Edge>(watched[i].edge)};
    }
    std::copy_n(sampled, num_sampled, data.begin());
    int stopped = BITOSCOPE_OK;
    detail::CApiWalk env{in};
    const auto end = detail::walkChanges<kMaxWalked, kMaxWalked, true>(
        env, lines, num_watched, data, num_sampled, 0, 0, [](uint32_t) { return true; },
        [&](const plugin::LineChange<kMaxWalked, kMaxWalked>& c) {
          bitoscope_line_change change{c.at(), 0, 0, 0, 0};
          for (size_t i = 0; i < num_watched; ++i) {
            change.changed |= (c.changed(i) ? 1u : 0u) << i;
            change.before |= (c.before(i) ? 1u : 0u) << i;
            change.after |= (c.after(i) ? 1u : 0u) << i;
          }
          for (size_t m = 0; m < num_sampled; ++m) change.sampled |= (c.sampled(m) ? 1u : 0u) << m;
          stopped = on_change(user, &change);
          return stopped == BITOSCOPE_OK;
        });
    (void)end;
    if (stopped != BITOSCOPE_OK) return stopped;
    return in->failed() ? BITOSCOPE_ERROR : BITOSCOPE_OK;
  } catch (...) {
    return thrown(in);
  }
}

int bitoscope_bits(bitoscope_decode_input* in, uint32_t channel, uint64_t first, uint32_t n, uint64_t* value) {
  if (in == nullptr || value == nullptr) return BITOSCOPE_ERROR;
  try {
    in->tick();
    if (!in->checkChannel(channel) || !atMost64(in, n, "bitoscope_bits")) return BITOSCOPE_ERROR;
    SpanGetter get{in};
    const int r = detail::bitsAcross(get, channel, first, n, value);
    return r == BITOSCOPE_OK ? r : noneOrError(in);
  } catch (...) {
    return thrown(in);
  }
}

int bitoscope_sample_at(bitoscope_decode_input* in, uint32_t channel, uint64_t start, uint64_t num, uint64_t den,
                        uint64_t k0, uint32_t n, uint64_t* value) {
  if (in == nullptr || value == nullptr) return BITOSCOPE_ERROR;
  try {
    in->tick();
    if (!in->checkChannel(channel) || !atMost64(in, n, "bitoscope_sample_at")) return BITOSCOPE_ERROR;
    if (den == 0 || den > UINT64_MAX / 2) {
      in->fail("bitoscope_sample_at: " + std::to_string(den) + " is not usable divisor (samples per bit num / den)");
      return BITOSCOPE_ERROR;
    }
    uint64_t v = 0;
    for (uint32_t i = 0; i < n; ++i) {
      const uint64_t k = k0 + i;
      uint64_t offset = 0;
      if (k < k0 || k > (UINT64_MAX - 1) / 2 || !mulDiv(2 * k + 1, num, 2 * den, &offset) ||
          offset > UINT64_MAX - start) {
        in->fail("bitoscope_sample_at: centre of bit " + std::to_string(k) + " from sample " +
                 std::to_string(start) + " at " + std::to_string(num) + " / " + std::to_string(den) +
                 " samples per bit does not fit 64 bits");
        return BITOSCOPE_ERROR;
      }
      const uint64_t at = start + offset;
      const bitoscope_span* s = in->spanAt(at);
      if (s == nullptr) return noneOrError(in);
      if (detail::spanLevel(*s, channel, at - s->first_sample)) v |= uint64_t{1} << i;
    }
    *value = v;
    return BITOSCOPE_OK;
  } catch (...) {
    return thrown(in);
  }
}

int bitoscope_clocked(bitoscope_decode_input* in, uint32_t clock, uint32_t data, uint64_t from, int edge, uint32_t n,
                      uint64_t limit, uint64_t* value, uint64_t* end) {
  if (in == nullptr || value == nullptr || end == nullptr) return BITOSCOPE_ERROR;
  try {
    in->tick();
    if (!in->checkChannel(clock) || !in->checkChannel(data) || !validKind(in, edge, "bitoscope_clocked") ||
        !atMost64(in, n, "bitoscope_clocked")) {
      return BITOSCOPE_ERROR;
    }
    SpanGetter get{in};
    uint64_t v = 0;
    uint64_t last = from;
    uint32_t taken = 0;
    while (taken < n) {
      uint64_t at = 0;
      if (detail::nextEdgeAcross(get, clock, last, edge, limit, &at) != BITOSCOPE_OK) break;
      const bitoscope_span* s = in->spanAt(at);
      if (s == nullptr) break;
      if (detail::spanLevel(*s, data, at - s->first_sample)) v |= uint64_t{1} << taken;
      ++taken;
      last = at;
    }
    if (in->failed()) return BITOSCOPE_ERROR;
    *value = v;
    *end = last;
    return static_cast<int>(taken);
  } catch (...) {
    return thrown(in);
  }
}

uint64_t bitoscope_sample_count(bitoscope_decode_input* in, int* is_final) {
  if (is_final != nullptr) *is_final = 0;
  if (in == nullptr) return 0;
  try {
    in->tick();
    in->takeArrived();
  } catch (...) {
    (void)thrown(in);
  }
  if (in->failed()) return 0;
  if (is_final != nullptr) *is_final = in->ended() ? 1 : 0;
  return in->arrived();
}

int bitoscope_release_before(bitoscope_decode_input* in, uint64_t sample) {
  if (in == nullptr) return BITOSCOPE_ERROR;
  try {
    in->tick();
    in->releaseBefore(sample);
    return in->failed() ? BITOSCOPE_ERROR : BITOSCOPE_OK;
  } catch (...) {
    return thrown(in);
  }
}

int bitoscope_fail_decode(bitoscope_decode_input* in, const char* reason) {
  if (in == nullptr) return BITOSCOPE_ERROR;
  try {
    in->fail(reason == nullptr || reason[0] == '\0' ? std::string("decoder failed") : std::string(reason));
  } catch (...) {
    return BITOSCOPE_ERROR;
  }
  return BITOSCOPE_OK;
}

const char* bitoscope_decode_error(const bitoscope_decode_input* in) {
  if (in == nullptr || !in->failed()) return nullptr;
  return in->error().c_str();
}

int bitoscope_span_at(bitoscope_decode_input* in, uint64_t sample, bitoscope_span* span) {
  if (span != nullptr) *span = bitoscope_span{};
  if (in == nullptr || span == nullptr) return BITOSCOPE_ERROR;
  try {
    in->tick();
    const bitoscope_span* s = in->spanAt(sample);
    if (s == nullptr) return noneOrError(in);
    *span = *s;
    return BITOSCOPE_OK;
  } catch (...) {
    return thrown(in);
  }
}

bitoscope_emit_cursor* bitoscope_emit_room(bitoscope_annotations* out) {
  static bitoscope_emit_cursor none{};
  return out == nullptr ? &none : out->room();
}

int bitoscope_emit_commit(bitoscope_annotations* out) {
  if (out == nullptr) return BITOSCOPE_ERROR;
  try {
    return out->commitRoom();
  } catch (...) {
    return thrown(out->input());
  }
}

int bitoscope_emit(bitoscope_annotations* out, uint64_t begin, uint64_t end, uint32_t row, uint32_t cls,
                   uint64_t value) {
  if (out == nullptr) return BITOSCOPE_ERROR;
  try {
    return out->emit(begin, end, row, cls, value);
  } catch (...) {
    return thrown(out->input());
  }
}

int bitoscope_emit_bytes(bitoscope_annotations* out, uint64_t begin, uint64_t end, uint32_t row, uint32_t cls,
                         const uint8_t* bytes, size_t length) {
  if (out == nullptr) return BITOSCOPE_ERROR;
  try {
    return out->emitBytes(begin, end, row, cls, bytes == nullptr ? std::span<const uint8_t>() : std::span(bytes, length));
  } catch (...) {
    return thrown(out->input());
  }
}

const uint8_t* bitoscope_collected_bytes(const bitoscope_annotations* out, uint64_t reference, size_t* length) {
  if (length != nullptr) *length = 0;
  if (out == nullptr) return nullptr;
  const std::span<const uint8_t> bytes = out->collectedBytes(reference);
  if (bytes.empty()) return nullptr;
  if (length != nullptr) *length = bytes.size();
  return bytes.data();
}

int bitoscope_settle(bitoscope_annotations* out, uint64_t sample) {
  if (out == nullptr) return BITOSCOPE_ERROR;
  try {
    return out->settle(sample);
  } catch (...) {
    return thrown(out->input());
  }
}

uint64_t bitoscope_settle_due(const bitoscope_annotations* out) { return out == nullptr ? 0 : out->settleDue(); }

uint64_t bitoscope_progress_step(const bitoscope_annotations* out) {
  return out == nullptr ? 0 : out->input()->effectiveProgressStep();
}

int bitoscope_memory_set_progress_step(bitoscope_decode_input* in, uint64_t step) {
  if (in == nullptr) return BITOSCOPE_ERROR;
  in->setProgressStep(step);
  return BITOSCOPE_OK;
}

int bitoscope_put(bitoscope_annotations* out, uint32_t output, uint64_t begin, uint64_t end, uint32_t kind,
                  uint64_t value, uint32_t flags) {
  if (out == nullptr) return BITOSCOPE_ERROR;
  try {
    return out->put(output, bitoscope_stream_item{begin, end, value, kind, flags});
  } catch (...) {
    return thrown(out->input());
  }
}

bitoscope_decode_input* bitoscope_memory_input(const uint8_t* const* planes, uint32_t num_channels,
                                               uint64_t num_samples, const bitoscope_memory_record* records,
                                               size_t num_records) {
  try {
    const std::span<const bitoscope_memory_record> list =
        records == nullptr ? std::span<const bitoscope_memory_record>() : std::span(records, num_records);
    return bitoscope_decode_input::fromMemory(planes, num_channels, num_samples, list).release();
  } catch (...) {
    return nullptr;
  }
}

bitoscope_annotations* bitoscope_collect_annotations(bitoscope_decode_input* in) {
  if (in == nullptr) return nullptr;
  try {
    return new bitoscope_annotations(in);
  } catch (...) {
    return nullptr;
  }
}

int bitoscope_finish_annotations(bitoscope_annotations* out) {
  if (out == nullptr) return BITOSCOPE_ERROR;
  try {
    return out->finish();
  } catch (...) {
    return thrown(out->input());
  }
}

int bitoscope_collect_items(bitoscope_annotations* out, const bitoscope_job_output* outputs, size_t num_outputs) {
  if (out == nullptr || (outputs == nullptr && num_outputs != 0)) return BITOSCOPE_ERROR;
  bitoscope_decode_input* in = out->input();
  if (!in->ended()) return BITOSCOPE_ERROR;
  try {
    const std::span<const bitoscope_job_output> list =
        outputs == nullptr ? std::span<const bitoscope_job_output>() : std::span(outputs, num_outputs);
    return out->openOutputs(list, in->arrived()) ? BITOSCOPE_OK : BITOSCOPE_ERROR;
  } catch (...) {
    return BITOSCOPE_ERROR;
  }
}

size_t bitoscope_item_count(const bitoscope_annotations* out, uint32_t output) {
  return out == nullptr ? 0 : out->collectedItems(output).size();
}

const bitoscope_stream_item* bitoscope_item_at(const bitoscope_annotations* out, uint32_t output, size_t index) {
  if (out == nullptr) return nullptr;
  const std::span<const bitoscope_stream_item> items = out->collectedItems(output);
  return index < items.size() ? &items[index] : nullptr;
}

size_t bitoscope_annotation_count(const bitoscope_annotations* out) {
  return out == nullptr ? 0 : out->collected().size();
}

const bitoscope_annotation* bitoscope_annotation_at(const bitoscope_annotations* out, size_t index) {
  if (out == nullptr || index >= out->collected().size()) return nullptr;
  return &out->collected()[index];
}

size_t bitoscope_settled_count(const bitoscope_annotations* out) {
  return out == nullptr ? 0 : out->settles().size();
}

const bitoscope_settled* bitoscope_settled_at(const bitoscope_annotations* out, size_t index) {
  if (out == nullptr || index >= out->settles().size()) return nullptr;
  return &out->settles()[index];
}

void bitoscope_free_annotations(bitoscope_annotations* out) { delete out; }

void bitoscope_free_input(bitoscope_decode_input* in) { delete in; }

int bitoscope_next_item(bitoscope_decode_input* in, uint32_t input, bitoscope_stream_item* item) {
  if (in == nullptr) return BITOSCOPE_ERROR;
  return in->nextItem(input, item);
}

bitoscope_decode_input* bitoscope_memory_item_input(uint64_t total_samples, const bitoscope_memory_items* inputs,
                                                    size_t num_inputs) {
  namespace sdk = bitoscope::plugin_c_sdk;
  try {
    std::vector<sdk::ItemInputSpec> specs;
    std::vector<std::vector<bitoscope_stream_item>> items;
    std::string refused;
    if (inputs == nullptr && num_inputs != 0) {
      refused = "no inputs were given but count of " + std::to_string(num_inputs);
    }
    for (size_t i = 0; refused.empty() && i < num_inputs; ++i) {
      const bitoscope_memory_items& in = inputs[i];
      if (in.items == nullptr && in.num_items != 0) {
        refused = "input " + std::to_string(i) + " has no items but count of " + std::to_string(in.num_items);
        break;
      }
      specs.push_back({"input " + std::to_string(i), true});
      if (in.items == nullptr) {
        items.emplace_back();
      } else {
        items.emplace_back(in.items, in.items + in.num_items);
      }
    }
    auto input = bitoscope_decode_input::fromItems(total_samples, std::move(specs),
                                                   std::make_unique<MemoryItems>(std::move(items)));
    if (!refused.empty()) input->fail("in-memory item input cannot be made: " + refused);
    return input.release();
  } catch (const std::bad_alloc&) {
    return nullptr;
  }
}

int bitoscope_memory_add_items(bitoscope_decode_input* in, const bitoscope_memory_items* inputs, size_t num_inputs) {
  namespace sdk = bitoscope::plugin_c_sdk;
  if (in == nullptr) return BITOSCOPE_ERROR;
  try {
    const auto refuse = [in](const std::string& why) {
      in->fail("items cannot be added: " + why);
      return BITOSCOPE_ERROR;
    };
    if (in->readsRing()) return refuse("input is decode job's own, not bitoscope_memory_input()'s");
    if (inputs == nullptr && num_inputs != 0) {
      return refuse("no inputs were given but count of " + std::to_string(num_inputs));
    }
    std::vector<sdk::ItemInputSpec> specs;
    std::vector<std::vector<bitoscope_stream_item>> items;
    for (size_t i = 0; i < num_inputs; ++i) {
      const bitoscope_memory_items& one = inputs[i];
      if (one.items == nullptr && one.num_items != 0) {
        return refuse("input " + std::to_string(i) + " has no items but count of " + std::to_string(one.num_items));
      }
      specs.push_back({"input " + std::to_string(i), true});
      if (one.items == nullptr) {
        items.emplace_back();
      } else {
        items.emplace_back(one.items, one.items + one.num_items);
      }
    }
    auto added = in->addItems(std::move(specs), std::make_unique<MemoryItems>(std::move(items)));
    if (!added) return refuse(added.error());
    return BITOSCOPE_OK;
  } catch (...) {
    return thrown(in);
  }
}

}

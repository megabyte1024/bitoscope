/*
 * SPDX-License-Identifier: MIT
 */

#include "annotation_block.h"

#include <algorithm>
#include <array>

namespace bitoscope::waveform_view {

namespace {

unsigned bitWidth(std::uint64_t v) { return static_cast<unsigned>(std::bit_width(v)); }

std::size_t lebBytes(std::uint64_t v) {
  std::size_t n = 1;
  for (; v >= 0x80; v >>= 7) ++n;
  return n;
}

std::uint8_t* putLeb(std::uint8_t* out, std::uint64_t v) {
  while (v >= 0x80) {
    *out++ = static_cast<std::uint8_t>(v | 0x80);
    v >>= 7;
  }
  *out++ = static_cast<std::uint8_t>(v);
  return out;
}

std::uint64_t getLeb(const std::uint8_t*& p) {
  std::uint64_t v = 0;
  for (unsigned s = 0;; s += 7) {
    const std::uint8_t b = *p++;
    v |= static_cast<std::uint64_t>(b & 0x7F) << s;
    if (b < 0x80) return v;
  }
}

std::uint64_t load64(const std::uint8_t* p) {
  std::uint64_t v;
  std::memcpy(&v, p, 8);
  return v;
}

class BitWriter {
 public:
  explicit BitWriter(std::uint8_t* out) : out_(out) {}
  void put(std::uint64_t v, unsigned w) {
    if (w == 0) return;
    if (w < 64) v &= (std::uint64_t{1} << w) - 1;
    acc_ |= v << fill_;
    unsigned total = fill_ + w;
    if (total >= 64) {
      for (unsigned k = 0; k < 8; ++k) *out_++ = static_cast<std::uint8_t>(acc_ >> (8 * k));
      total -= 64;
      acc_ = fill_ == 0 ? 0 : v >> (64 - fill_);
    }
    while (total >= 8) {
      *out_++ = static_cast<std::uint8_t>(acc_);
      acc_ >>= 8;
      total -= 8;
    }
    fill_ = total;
  }
  std::uint8_t* align() {
    if (fill_ != 0) *out_++ = static_cast<std::uint8_t>(acc_);
    acc_ = 0;
    fill_ = 0;
    return out_;
  }

 private:
  std::uint8_t* out_;
  std::uint64_t acc_ = 0;
  unsigned fill_ = 0;
};

struct FieldPlan {
  bool present = false;
  std::uint64_t min = 0;
  unsigned width = 0;
  std::size_t bytes = 0;
};

FieldPlan planField(std::span<const std::uint64_t> entries, std::uint64_t usual) {
  FieldPlan plan;
  if (entries.empty()) return plan;
  std::uint64_t lo = entries[0];
  std::uint64_t hi = lo;
  for (const std::uint64_t e : entries) {
    lo = (std::min)(lo, e);
    hi = (std::max)(hi, e);
  }
  if (lo == hi && lo == usual) return plan;
  const auto bytesWith = [&](std::uint64_t min) {
    return 1 + lebBytes(min) + (entries.size() * bitWidth(hi - min) + 7) / 8;
  };
  plan.present = true;
  plan.min = bytesWith(0) <= bytesWith(lo) ? 0 : lo;
  plan.width = bitWidth(hi - plan.min);
  plan.bytes = bytesWith(plan.min);
  return plan;
}

struct BlockEntries {
  std::array<std::uint64_t, kBlockEntries> steps;
  std::array<std::uint64_t, kBlockEntries> lengths;
  std::span<const std::uint64_t> values;
  std::size_t count = 0;
  std::size_t lengthCount = 0;

  std::span<const std::uint64_t> stepSpan() const { return {steps.data(), count == 0 ? 0 : count - 1}; }
  std::span<const std::uint64_t> lengthSpan() const { return {lengths.data(), lengthCount}; }
};

BlockEntries entriesOf(std::span<const std::uint64_t> begins, std::span<const std::uint32_t> lengths,
                       std::span<const std::uint64_t> values) {
  BlockEntries e;
  e.count = begins.size();
  e.lengthCount = lengths.size();
  for (std::size_t t = 0; t + 1 < e.count; ++t) e.steps[t] = begins[t + 1] - begins[t];
  for (std::size_t t = 0; t < lengths.size(); ++t) e.lengths[t] = lengths[t];
  e.values = values;
  return e;
}

struct BlockPlan {
  std::array<FieldPlan, 3> fields;
  std::array<std::span<const std::uint64_t>, 3> entries;

  std::size_t bytes() const {
    std::size_t sum = 0;
    for (const FieldPlan& f : fields) sum += f.bytes;
    return sum == 0 ? 0 : 1 + sum;
  }
};

constexpr std::array<std::uint8_t, 3> kFieldFlags{kStepsPresent, kLengthsPresent, kValuesPresent};

BlockPlan planBlock(const BlockEntries& e, const UsualValues& usual) {
  BlockPlan plan;
  plan.entries = {e.stepSpan(), e.lengthSpan(), e.values};
  plan.fields = {planField(plan.entries[0], usual.step), planField(plan.entries[1], usual.length),
                 planField(plan.entries[2], usual.value)};
  return plan;
}

std::size_t pack(const BlockPlan& plan, std::uint8_t* out) {
  std::uint8_t flags = 0;
  for (std::size_t f = 0; f < 3; ++f) {
    if (plan.fields[f].present) flags = static_cast<std::uint8_t>(flags | kFieldFlags[f]);
  }
  if (flags == 0) return 0;
  std::uint8_t* p = out;
  *p++ = flags;
  for (const FieldPlan& field : plan.fields) {
    if (!field.present) continue;
    *p++ = static_cast<std::uint8_t>(field.width);
    p = putLeb(p, field.min);
  }
  BitWriter writer(p);
  for (std::size_t f = 0; f < 3; ++f) {
    const FieldPlan& field = plan.fields[f];
    if (!field.present) continue;
    for (const std::uint64_t v : plan.entries[f]) writer.put(v - field.min, field.width);
    p = writer.align();
  }
  return static_cast<std::size_t>(p - out);
}

}

std::uint64_t fieldEntry(const BlockField& field, std::size_t t) {
  return field.min + readBits(field.bits, t * field.width, field.width);
}

std::uint64_t usualStepOf(std::span<const std::uint64_t> firstBlock) {
  if (firstBlock.size() < 2) return 0;
  const std::uint64_t step = firstBlock[1] - firstBlock[0];
  for (std::size_t t = 1; t + 1 < firstBlock.size(); ++t) {
    if (firstBlock[t + 1] - firstBlock[t] != step) return 0;
  }
  return step;
}

std::uint32_t usualLengthOf(std::span<const std::uint32_t> firstBlock) {
  if (firstBlock.empty()) return 0;
  for (const std::uint32_t length : firstBlock) {
    if (length != firstBlock[0]) return 0;
  }
  return firstBlock[0];
}

std::uint64_t usualValueOf(std::span<const std::uint64_t> firstBlock) {
  if (firstBlock.empty()) return 0;
  for (const std::uint64_t value : firstBlock) {
    if (value != firstBlock[0]) return 0;
  }
  return firstBlock[0];
}

std::size_t blockPayloadBytes(std::span<const std::uint64_t> begins, std::span<const std::uint32_t> lengths,
                              std::span<const std::uint64_t> values, const UsualValues& usual) {
  return planBlock(entriesOf(begins, lengths, values), usual).bytes();
}

std::size_t packBlock(std::span<const std::uint64_t> begins, std::span<const std::uint32_t> lengths,
                      std::span<const std::uint64_t> values, const UsualValues& usual, std::uint8_t* out) {
  return pack(planBlock(entriesOf(begins, lengths, values), usual), out);
}

std::size_t blockPayloadBytes(std::span<const std::uint64_t> begins, std::span<const std::uint32_t> lengths,
                              const UsualValues& usual) {
  return planBlock(entriesOf(begins, lengths, {}), usual).bytes();
}

std::size_t packBlock(std::span<const std::uint64_t> begins, std::span<const std::uint32_t> lengths,
                      const UsualValues& usual, std::uint8_t* out) {
  return pack(planBlock(entriesOf(begins, lengths, {}), usual), out);
}

BlockFields describeBlock(const std::uint8_t* payload, std::size_t count, const UsualValues& usual) {
  BlockFields fields;
  fields.steps.min = usual.step;
  fields.lengths.min = usual.length;
  fields.values.min = usual.value;
  if (payload == nullptr) return fields;
  const std::uint8_t flags = payload[0];
  const std::uint8_t* p = payload + 1;
  const std::array<BlockField*, 3> order{&fields.steps, &fields.lengths, &fields.values};
  for (std::size_t f = 0; f < 3; ++f) {
    if ((flags & kFieldFlags[f]) == 0) continue;
    order[f]->width = *p++;
    order[f]->min = getLeb(p);
  }
  const std::array<std::size_t, 3> entries{count - 1, count, count};
  for (std::size_t f = 0; f < 3; ++f) {
    if ((flags & kFieldFlags[f]) == 0) continue;
    order[f]->bits = p;
    p += (entries[f] * order[f]->width + 7) / 8;
  }
  return fields;
}

std::size_t stepsBytes(std::span<const std::uint64_t> begins, std::uint64_t usualStep) {
  return planBlock(entriesOf(begins, {}, {}), {usualStep, 0, 0}).bytes();
}

std::size_t packSteps(std::span<const std::uint64_t> begins, std::uint64_t usualStep, std::uint8_t* out) {
  return pack(planBlock(entriesOf(begins, {}, {}), {usualStep, 0, 0}), out);
}

StepField describeSteps(const std::uint8_t* payload) { return describeBlock(payload, kBlockEntries, {}).steps; }

void sumBegins(const StepField& steps, std::uint64_t* begins, std::size_t from, std::size_t to) {
  std::size_t t = from - 1;
  const std::size_t fields = to - 1;
  std::uint64_t b = begins[t];
  const std::uint64_t min = steps.min;
  const unsigned width = steps.width;
  if (width == 0) {
    while (t < fields) {
      b += min;
      begins[++t] = b;
    }
    return;
  }
  if (width <= 57) {
    // WHY: Own loop for summing: one load serves several narrow fields, the general read reloads per field.
    const std::uint64_t mask = (std::uint64_t{1} << width) - 1;
    const std::size_t perLoad = 57 / width;
    std::size_t bit = t * width;
    while (t < fields) {
      std::uint64_t word = load64(steps.bits + (bit >> 3)) >> (bit & 7);
      const std::size_t take = (std::min)(perLoad, fields - t);
      for (std::size_t f = 0; f < take; ++f) {
        b += min + (word & mask);
        word >>= width;
        begins[++t] = b;
      }
      bit += take * width;
    }
    return;
  }
  for (; t < fields; ++t) {
    b += min + readBits(steps.bits, t * width, width);
    begins[t + 1] = b;
  }
}

std::uint8_t coveredWidthOf(bool lengthsField, std::uint32_t usualLength, bool regular, std::uint64_t total) {
  if (!lengthsField && usualLength < kLengthMarker) return 0;
  return regular && total <= kNarrowEntryMost ? 4 : 8;
}

BlockTableForms blockTableForms(std::uint64_t largestDistance, std::uint64_t payloadBytes) {
  BlockTableForms forms;
  forms.beginWidth = largestDistance <= kNarrowEntryMost ? 4 : 8;
  forms.offsetWidth = payloadBytes == 0 ? 0 : payloadBytes <= kNarrowEntryMost ? 4 : 8;
  return forms;
}

std::uint64_t beginInBlock(const StepField& steps, std::uint64_t base, std::size_t j) {
  if (steps.width == 0) return base + j * steps.min;
  std::array<std::uint64_t, kBlockEntries> begins;
  begins[0] = base;
  if (j > 0) sumBegins(steps, begins.data(), 1, j + 1);
  return begins[j];
}

}

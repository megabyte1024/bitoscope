/*
 * SPDX-License-Identifier: MIT
 */

#include "native_i2c.h"

#include <array>
#include <cstdint>

namespace bitoscope::decoders_lowlevel {

using plugin::AnnotationRole;
using plugin::DecoderDeclaration;
using plugin::RowRule;
using plugin::ValueFormat;

DecoderDeclaration NativeI2c::declaration() const {
  DecoderDeclaration d;
  d.name = "i2c";
  d.version = "0.1.0";
  d.channels = {{.name = "scl", .description = "clock", .display = "SCL"},
                {.name = "sda", .description = "data", .display = "SDA"}};
  // WHY: address_format option is unsupported: address always shown shifted (7-bit), reference default.
  d.unsupportedOptions = {"address_format"};
  d.options = {
      {.name = "show_bits",
       .description = "one annotation per data bit",
       .defaultValue = "yes",
       .allowedValues = {"yes", "no"},
       .display = "Show bits"},
  };
  d.classes = {
      {.id = kClassAddressWrite,
       .name = "Address write",
       .format = ValueFormat::address7,
       .role = AnnotationRole::address},
      {.id = kClassAddressRead,
       .name = "Address read",
       .format = ValueFormat::address7,
       .role = AnnotationRole::address},
      {.id = kClassDataWrite,
       .name = "Data write",
       .format = ValueFormat::hex,
       .hexDigits = 2,
       .role = AnnotationRole::data},
      {.id = kClassDataRead,
       .name = "Data read",
       .format = ValueFormat::hex,
       .hexDigits = 2,
       .role = AnnotationRole::data},
      {.id = kClassAck, .name = "ACK", .role = AnnotationRole::ok},
      {.id = kClassNack, .name = "NACK", .role = AnnotationRole::error},
      {.id = kClassStart, .name = "Start", .role = AnnotationRole::start},
      {.id = kClassStartRepeat, .name = "Start repeat", .role = AnnotationRole::start},
      {.id = kClassStop, .name = "Stop", .role = AnnotationRole::stop},
      {.id = kClassIncompleteByte, .name = "Incomplete byte", .role = AnnotationRole::error},
      {.id = kClassBit, .format = ValueFormat::decimal, .role = AnnotationRole::bit},
  };
  d.rows = {
      {kRowBits, "Bits"}, {kRowBytes, "Bytes"}, {kRowAck, "ACK"}, {kRowMarkers, "Markers"}, {kRowErrors, "Errors"}};
  d.rowSources = {{.row = kRowBits,
                   .cls = kClassBit,
                   .sourceRow = kRowBytes,
                   .sourceClasses = {kClassAddressWrite, kClassAddressRead, kClassDataWrite, kClassDataRead},
                   .enableOption = "show_bits",
                   .rule = RowRule::clockEdges,
                   .edges = {.clockChannel = "scl", .dataChannel = "sda", .count = 8}}};
  return d;
}

namespace {


// WHY: Byte and ACK annotations stay pending until next SCL fall: eager search needs unbounded look-ahead.
class I2cJob {
 public:
  explicit I2cJob(plugin::Annotations& out) : out_(out) {}

  void change(uint64_t at, bool scl_was, bool sda_was, bool scl_now, bool sda_now) {
    if (pending_ && scl_was && !scl_now) resolve(at);

    if (scl_now && scl_was) {
      if (sda_was && !sda_now) {
        const uint32_t cls = inTransfer_ ? NativeI2c::kClassStartRepeat : NativeI2c::kClassStart;
        inTransfer_ = true;
        addressNext_ = true;
        bits_ = 0;
        value_ = 0;
        out_.emit(at, at + 1, NativeI2c::kRowMarkers, cls, 0);
      } else if (!sda_was && sda_now) {
        inTransfer_ = false;
        bits_ = 0;
        out_.emit(at, at + 1, NativeI2c::kRowMarkers, NativeI2c::kClassStop, 0);
      }
      return;
    }
    if (!scl_now || scl_was || !inTransfer_) return;
    if (bits_ == 0) byteBegin_ = at;
    if (bits_ < 8) {
      value_ = (value_ << 1) | (sda_now ? 1u : 0u);
      if (++bits_ < 8) return;
      if (addressNext_) {
        reading_ = (value_ & 1u) != 0;
        addressNext_ = false;
        setPending(byteBegin_, NativeI2c::kRowBytes,
                   reading_ ? NativeI2c::kClassAddressRead : NativeI2c::kClassAddressWrite, value_ >> 1);
      } else {
        setPending(byteBegin_, NativeI2c::kRowBytes,
                   reading_ ? NativeI2c::kClassDataRead : NativeI2c::kClassDataWrite, value_);
      }
      return;
    }
    bits_ = 0;
    value_ = 0;
    setPending(at, NativeI2c::kRowAck, sda_now ? NativeI2c::kClassNack : NativeI2c::kClassAck, 0);
  }

  void finish(uint64_t end) {
    if (pending_) resolve(end);
    if (inTransfer_ && bits_ > 0 && bits_ < 8) {
      out_.emit(byteBegin_, end, NativeI2c::kRowErrors, NativeI2c::kClassIncompleteByte, 0);
    }
  }

  uint64_t oldestOpen() const {
    if (pending_) return pendingBegin_;
    if (inTransfer_ && bits_ > 0 && bits_ < 8) return byteBegin_;
    return UINT64_MAX;
  }

 private:
  void setPending(uint64_t begin, uint32_t row, uint32_t cls, uint64_t value) {
    pending_ = true;
    pendingBegin_ = begin;
    pendingRow_ = row;
    pendingClass_ = cls;
    pendingValue_ = value;
  }

  void resolve(uint64_t end) {
    pending_ = false;
    out_.emit(pendingBegin_, end, pendingRow_, pendingClass_, pendingValue_);
  }

  plugin::Annotations& out_;

  bool inTransfer_ = false;
  bool addressNext_ = false;
  bool reading_ = false;
  uint32_t bits_ = 0;
  uint32_t value_ = 0;
  uint64_t byteBegin_ = 0;

  bool pending_ = false;
  uint64_t pendingBegin_ = 0;
  uint32_t pendingRow_ = 0;
  uint32_t pendingClass_ = 0;
  uint64_t pendingValue_ = 0;
};

}

std::expected<void, std::string> NativeI2c::decode(const plugin::DecodeJob& job, plugin::DecodeInput& input,
                                                   plugin::Annotations& out) {
  const auto scl = job.channel("scl");
  const auto sda = job.channel("sda");
  if (!scl || !sda) return std::unexpected("I\xC2\xB2" "C needs both SCL and SDA");
  I2cJob i2c(out);
  auto oldestOpen = [&i2c] { return i2c.oldestOpen(); };
  input.onProgress(oldestOpen);

  const std::array<plugin::WatchedLine, 2> lines{{{*scl, plugin::Edge::any}, {*sda, plugin::Edge::any}}};
  const auto end = plugin::forEachChange(
      input, lines, std::array<uint32_t, 0>{}, [](uint32_t) { return true; },
      [&i2c](const auto& c) {
        i2c.change(c.at(), c.before(0), c.before(1), c.after(0), c.after(1));
        return true;
      });
  if (end) i2c.finish(*end);
  return {};
}

}

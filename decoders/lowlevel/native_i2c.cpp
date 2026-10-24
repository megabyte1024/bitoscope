/*
 * SPDX-License-Identifier: MIT
 */

#include "native_i2c.h"

#include <array>
#include <cstdint>

namespace bitoscope::decoders_lowlevel {

using plugin::AnnotationClass;
using plugin::DecoderDeclaration;
using plugin::OptionType;
using plugin::ValueFormat;

DecoderDeclaration NativeI2c::declaration() const {
  DecoderDeclaration d;
  d.name = "i2c";
  d.version = "0.1.0";
  d.channels = {{"scl", "clock", true, "SCL"}, {"sda", "data", true, "SDA"}};
  // WHY: address_format option is unsupported: address always shown shifted (7-bit), reference default.
  d.unsupportedOptions = {"address_format"};
  d.options = {
      {"show_bits", "one annotation per data bit", OptionType::string, "yes", {"yes", "no"}, std::nullopt,
       std::nullopt, {}, "Show bits"},
  };
  d.classes = {
      AnnotationClass{kClassAddressWrite, "Address write", ValueFormat::address7, 0, "", plugin::AnnotationRole::address},
      AnnotationClass{kClassAddressRead, "Address read", ValueFormat::address7, 0, "", plugin::AnnotationRole::address},
      AnnotationClass{kClassDataWrite, "Data write", ValueFormat::hex, 2, "", plugin::AnnotationRole::data},
      AnnotationClass{kClassDataRead, "Data read", ValueFormat::hex, 2, "", plugin::AnnotationRole::data},
      AnnotationClass{kClassAck, "ACK", ValueFormat::none, 0, "", plugin::AnnotationRole::ok},
      AnnotationClass{kClassNack, "NACK", ValueFormat::none, 0, "", plugin::AnnotationRole::error},
      AnnotationClass{kClassStart, "Start", ValueFormat::none, 0, "", plugin::AnnotationRole::start},
      AnnotationClass{kClassStartRepeat, "Start repeat", ValueFormat::none, 0, "", plugin::AnnotationRole::start},
      AnnotationClass{kClassStop, "Stop", ValueFormat::none, 0, "", plugin::AnnotationRole::stop},
      AnnotationClass{kClassIncompleteByte, "Incomplete byte", ValueFormat::none, 0, "", plugin::AnnotationRole::error},
      AnnotationClass{kClassBit, "", ValueFormat::decimal, 0, "", plugin::AnnotationRole::bit},
  };
  d.rows = {
      {kRowBits, "Bits"}, {kRowBytes, "Bytes"}, {kRowAck, "ACK"}, {kRowMarkers, "Markers"}, {kRowErrors, "Errors"}};
  plugin::RowSource bits{kRowBits,
                         kClassBit,
                         kRowBytes,
                         {kClassAddressWrite, kClassAddressRead, kClassDataWrite, kClassDataRead},
                         "show_bits",
                         plugin::RowRule::clockEdges};
  bits.edges = {"scl", "sda", "", 8};
  d.rowSources = {bits};
  return d;
}

namespace {


// WHY: Byte and ACK annotations stay pending until next SCL fall: eager search needs unbounded look-ahead.
class I2cJob {
 public:
  explicit I2cJob(plugin::Annotations& out) : out_(out) {}

  bool change(uint64_t at, bool scl_was, bool sda_was, bool scl_now, bool sda_now) {
    return apply(at, scl_was, sda_was, scl_now, sda_now);
  }

  bool finish(uint64_t end) {
    if (pending_ && !resolve(end)) return false;
    return !(inTransfer_ && bits_ > 0 && bits_ < 8) ||
           out_.emit(byteBegin_, end, NativeI2c::kRowErrors, NativeI2c::kClassIncompleteByte, 0);
  }

  uint64_t oldestOpen() const {
    if (pending_) return pendingBegin_;
    if (inTransfer_ && bits_ > 0 && bits_ < 8) return byteBegin_;
    return UINT64_MAX;
  }

 private:

  bool apply(uint64_t at, bool scl_was, bool sda_was, bool scl_now, bool sda_now) {
    if (pending_ && scl_was && !scl_now && !resolve(at)) return false;

    if (scl_now && scl_was) {
      if (sda_was && !sda_now) {
        const uint32_t cls = inTransfer_ ? NativeI2c::kClassStartRepeat : NativeI2c::kClassStart;
        inTransfer_ = true;
        addressNext_ = true;
        bits_ = 0;
        value_ = 0;
        return out_.emit(at, at + 1, NativeI2c::kRowMarkers, cls, 0);
      }
      if (!sda_was && sda_now) {
        inTransfer_ = false;
        bits_ = 0;
        return out_.emit(at, at + 1, NativeI2c::kRowMarkers, NativeI2c::kClassStop, 0);
      }
      return true;
    }
    if (!scl_now || scl_was || !inTransfer_) return true;
    if (bits_ == 0) byteBegin_ = at;
    if (bits_ < 8) {
      value_ = (value_ << 1) | (sda_now ? 1u : 0u);
      if (++bits_ < 8) return true;
      if (addressNext_) {
        reading_ = (value_ & 1u) != 0;
        addressNext_ = false;
        setPending(byteBegin_, NativeI2c::kRowBytes,
                   reading_ ? NativeI2c::kClassAddressRead : NativeI2c::kClassAddressWrite, value_ >> 1);
      } else {
        setPending(byteBegin_, NativeI2c::kRowBytes,
                   reading_ ? NativeI2c::kClassDataRead : NativeI2c::kClassDataWrite, value_);
      }
      return true;
    }
    bits_ = 0;
    value_ = 0;
    setPending(at, NativeI2c::kRowAck, sda_now ? NativeI2c::kClassNack : NativeI2c::kClassAck, 0);
    return true;
  }

  void setPending(uint64_t begin, uint32_t row, uint32_t cls, uint64_t value) {
    pending_ = true;
    pendingBegin_ = begin;
    pendingRow_ = row;
    pendingClass_ = cls;
    pendingValue_ = value;
  }

  bool resolve(uint64_t end) {
    pending_ = false;
    return out_.emit(pendingBegin_, end, pendingRow_, pendingClass_, pendingValue_);
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
      [&i2c](const auto& c) { return i2c.change(c.at(), c.before(0), c.before(1), c.after(0), c.after(1)); });
  if (end) (void)i2c.finish(*end);
  return {};
}

}

/*
 * SPDX-License-Identifier: MIT
 */

#include "native_uart.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <format>
#include <string_view>
#include <tuple>
#include <utility>

#include "output_writer.h"

namespace bitoscope::decoders_lowlevel {

using plugin::AnnotationClass;
using plugin::DecoderDeclaration;
using plugin::OptionType;
using plugin::StreamType;
using plugin::ValueFormat;

DecoderDeclaration NativeUart::declaration() const {
  DecoderDeclaration d;
  d.name = "uart";
  d.version = "0.1.0";
  d.channels = {{"rx", "receive line", false, "RX"}, {"tx", "transmit line", false, "TX"}};
  d.options = {
      {"baudrate", "bits per second", OptionType::integer, "115200", {}, 1, 4'000'000'000, {}, "Baud rate"},
      {"data_bits", "data bits per frame", OptionType::integer, "8", {}, 5, 9, {}, "Data bits"},
      {"parity", "parity bit", OptionType::string, "none", {"none", "odd", "even"}, std::nullopt, std::nullopt,
       {"zero", "one", "ignore"}, "Parity"},
      {"stop_bits", "stop bits", OptionType::string, "1", {"1", "1.5", "2"}, std::nullopt, std::nullopt,
       {"0", "0.5"}, "Stop bits"},
      {"bit_order", "order of data bits", OptionType::string, "lsb-first", {"lsb-first", "msb-first"},
       std::nullopt, std::nullopt, {}, "Bit order"},
      {"invert_rx", "invert receive line", OptionType::string, "no", {"no", "yes"}, std::nullopt, std::nullopt, {},
       "Invert RX"},
      {"invert_tx", "invert transmit line", OptionType::string, "no", {"no", "yes"}, std::nullopt, std::nullopt, {},
       "Invert TX"},
      {"show_bits", "one annotation per data bit", OptionType::string, "yes", {"yes", "no"}, std::nullopt,
       std::nullopt, {}, "Show bits"},
  };
  d.unsupportedOptions = {"format",          "sample_point",  "rx_packet_delim",
                          "tx_packet_delim", "rx_packet_len", "tx_packet_len"};
  d.classes = {
      AnnotationClass{kClassData, "", ValueFormat::hex, 0, "data_bits", plugin::AnnotationRole::data},
      AnnotationClass{kClassStart, "Start", ValueFormat::none, 0, "", plugin::AnnotationRole::start},
      AnnotationClass{kClassStop, "Stop", ValueFormat::none, 0, "", plugin::AnnotationRole::stop},
      AnnotationClass{kClassFramingError, "Framing error", ValueFormat::none, 0, "", plugin::AnnotationRole::error},
      AnnotationClass{kClassParityError, "Parity error", ValueFormat::none, 0, "", plugin::AnnotationRole::error},
      AnnotationClass{kClassFrameCutOff, "Frame cut off", ValueFormat::none, 0, "", plugin::AnnotationRole::error},
      AnnotationClass{kClassBit, "", ValueFormat::decimal, 0, "", plugin::AnnotationRole::bit},
  };
  d.rows = {{kRowRxBits, "RX bits"}, {kRowRx, "RX"}, {kRowTxBits, "TX bits"}, {kRowTx, "TX"}};
  plugin::RowSource rxBits{kRowRxBits, kClassBit, kRowRx, {kClassData}, "show_bits", plugin::RowRule::bitGrid};
  rxBits.grid = {"baudrate", 1, "data_bits", "bit_order"};
  plugin::RowSource txBits = rxBits;
  txBits.row = kRowTxBits;
  txBits.sourceRow = kRowTx;
  d.rowSources = {rxBits, txBits};
  d.outputs = {{"rx", "received words", StreamType::words, 0, "data_bits", "rx", "", "uart"},
               {"tx", "transmitted words", StreamType::words, 0, "data_bits", "tx", "", "uart"}};
  return d;
}

std::expected<void, std::string> NativeUart::check(const plugin::DecodeJob& job) const {
  if (!job.channel("rx") && !job.channel("tx")) return std::unexpected("UART needs at least one of RX and TX");
  if (job.sampleRateMilliHz == 0) return std::unexpected("capture has no sample rate");
  // WHY: Below two samples per bit sample points of adjacent bits can coincide.
  const uint64_t baud = static_cast<uint64_t>(job.optionInt("baudrate").value_or(0));
  if (job.sampleRateMilliHz < 2 * 1000 * baud) {
    return std::unexpected(
        std::format("sample rate {} Hz is too low for {} baud: at least twice baud rate is needed",
                    job.sampleRateMilliHz / 1000, baud));
  }
  return {};
}

namespace {

constexpr uint32_t kMaxFrameBits = 13;

// WHY: 1.5 stop bits: only middle of first stop bit is sampled, next frame may start where period ends.
struct Framing {
  uint32_t dataBits = 8;
  bool parity = false;
  bool odd = false;
  uint32_t stopHalves = 2;
  bool msbFirst = false;
  uint32_t stopSamples = 1;
  std::array<uint64_t, kMaxFrameBits + 1> begin{};
  std::array<uint64_t, kMaxFrameBits> middle{};
  uint64_t end = 0;
  uint64_t halfBitBeforeEnd = 0;
};

Framing framingOf(const plugin::DecodeJob& job) {
  Framing f;
  const uint64_t baud = static_cast<uint64_t>(job.optionInt("baudrate").value_or(1));
  const uint64_t rate = job.sampleRateMilliHz;
  f.dataBits = static_cast<uint32_t>(job.optionInt("data_bits").value_or(8));
  const std::string_view parity = job.option("parity");
  f.parity = parity != "none";
  f.odd = parity == "odd";
  const std::string_view stop = job.option("stop_bits");
  f.stopHalves = stop == "1.5" ? 3 : stop.starts_with('2') ? 4 : 2;
  f.stopSamples = f.stopHalves / 2;
  f.msbFirst = job.option("bit_order") == "msb-first";
  const uint64_t halves = 2 * (1 + f.dataBits + (f.parity ? 1 : 0)) + f.stopHalves;
  for (uint64_t k = 0; k <= kMaxFrameBits; ++k) f.begin[k] = k * rate / (baud * 1000);
  for (uint64_t k = 0; k < kMaxFrameBits; ++k) f.middle[k] = (2 * k + 1) * rate / (2 * baud * 1000);
  f.end = halves * rate / (2 * baud * 1000);
  f.halfBitBeforeEnd = (halves - 1) * rate / (2 * baud * 1000);
  return f;
}

struct Frame {
  uint64_t start = 0;
  uint64_t stopEnd = 0;
  uint64_t value = 0;
  bool framingError = false;
  bool parityError = false;
  bool stopError = false;
  bool cutOff = false;
};

enum class Phase : uint8_t { awaitIdle, seek, awaitStart, frame };

struct Line {
  uint32_t row = 0;
  bool invert = false;
  plugin::StreamWriter words;
  bool idle = true;
  Phase phase = Phase::seek;
  uint32_t point = 0;
  uint64_t next = 0;
  uint64_t lastEdge = 0;
  uint32_t ones = 0;
  Frame frame;
};

template <size_t W>
class UartWalk {
 public:
  UartWalk(const Framing& f, plugin::Annotations& out, std::array<Line, W>& lines) : f_(f), out_(out), lines_(lines) {
    uint32_t n = 0;
    for (uint32_t k = 0; k < 1 + f.dataBits + (f.parity ? 1u : 0u) + f.stopSamples; ++k) points_[n++] = f.middle[k];
    parityPoint_ = f.parity ? f.dataBits + 1 : 0;
    stopPoint_ = f.dataBits + 1 + (f.parity ? 1u : 0u);
    lastPoint_ = n;
    points_[n] = f.end - 1;
  }

  void start(uint32_t levels) {
    for (size_t i = 0; i < W; ++i) {
      Line& line = lines_[i];
      line.idle = (((levels >> i) & 1u) != 0) != line.invert;
      if (!line.idle) awaitIdle(line);
    }
  }

  template <typename Change>
  bool change(const Change& c) {
    const uint64_t at = c.at();
    for (Line& line : lines_) {
      if (!advance(line, at)) return false;
    }
    for (size_t i = 0; i < W; ++i) {
      if (!c.changed(i)) continue;
      Line& line = lines_[i];
      line.idle = c.after(i) != line.invert;
      line.lastEdge = at;
      if (line.phase == Phase::awaitIdle && line.idle) {
        line.phase = Phase::awaitStart;
      } else if (line.phase == Phase::awaitStart && !line.idle) {
        begin(line, at);
      }
    }
    return true;
  }

  void finish(uint64_t samples) {
    for (Line& line : lines_) {
      if (!advance(line, samples)) return;
      if (line.phase != Phase::frame) continue;
      if (heldLowSinceFrameStart(line)) {
        awaitIdle(line);
        continue;
      }
      line.frame.cutOff = line.frame.start + f_.halfBitBeforeEnd >= samples;
      line.frame.stopEnd = (std::min)(line.frame.start + f_.end, samples);
      if (!completed(line)) return;
    }
  }

  uint64_t oldestOpen() const {
    uint64_t mark = UINT64_MAX;
    for (const Line& line : lines_) {
      if (line.phase == Phase::frame) {
        mark = (std::min)(mark, line.frame.start);
      } else if (line.phase == Phase::seek) {
        mark = (std::min)(mark, line.next);
      }
    }
    return mark;
  }

 private:
  static bool heldLowSinceFrameStart(const Line& line) { return line.lastEdge <= line.frame.start; }

  static void awaitIdle(Line& line) {
    line.phase = Phase::awaitIdle;
    line.next = UINT64_MAX;
  }

  void begin(Line& line, uint64_t at) {
    line.phase = Phase::frame;
    line.point = 0;
    line.next = at + points_[0];
    line.ones = 0;
    line.frame = Frame{};
    line.frame.start = at;
  }

  bool advance(Line& line, uint64_t until) {
    while (line.next < until) {
      if (line.phase == Phase::seek) {
        if (line.idle) {
          line.phase = Phase::awaitStart;
          line.next = UINT64_MAX;
          return true;
        }
        begin(line, line.next);
        continue;
      }
      const uint32_t k = line.point;
      const bool mark = line.idle;
      Frame& frame = line.frame;
      if (k == lastPoint_) {
        if (heldLowSinceFrameStart(line)) {
          awaitIdle(line);
          return true;
        }
        frame.stopEnd = frame.start + f_.end;
        if (!completed(line)) return false;
        continue;
      }
      if (k == 0) {
        frame.framingError = mark;
      } else if (k <= f_.dataBits) {
        line.ones += mark ? 1u : 0u;
        frame.value = f_.msbFirst ? (frame.value << 1) | (mark ? 1u : 0u)
                                  : frame.value | (uint64_t{mark ? 1u : 0u} << (k - 1));
      } else if (k == parityPoint_) {
        frame.parityError = ((line.ones + (mark ? 1u : 0u)) % 2 == 1) != f_.odd;
      } else if (!mark) {
        frame.stopError = true;
      }
      line.point = k + 1;
      line.next = frame.start + points_[k + 1];
    }
    return true;
  }

  bool completed(Line& line) {
    line.phase = Phase::seek;
    line.next = line.frame.stopEnd;
    return emit(line, line.frame);
  }

  bool emit(const Line& line, const Frame& frame) {
    const uint64_t s = frame.start;
    if (frame.cutOff) {
      return out_.emit(s, frame.stopEnd, line.row, NativeUart::kClassFrameCutOff, 0) &&
             (!line.words || line.words.put(s, frame.stopEnd, plugin::kItemBoundary, 0, plugin::kItemDamaged));
    }
    const uint64_t dataEnd = s + f_.begin[1 + f_.dataBits];
    if (!out_.emit(s, s + f_.begin[1], line.row,
                   frame.framingError ? NativeUart::kClassFramingError : NativeUart::kClassStart, 0) ||
        !out_.emit(s + f_.begin[1], dataEnd, line.row, NativeUart::kClassData, frame.value)) {
      return false;
    }
    if (frame.parityError && !out_.emit(dataEnd, s + f_.begin[2 + f_.dataBits], line.row,
                                         NativeUart::kClassParityError, 0)) {
      return false;
    }
    if (!out_.emit(s + f_.begin[stopPoint_], frame.stopEnd, line.row,
                   frame.stopError ? NativeUart::kClassFramingError : NativeUart::kClassStop, 0)) {
      return false;
    }
    const bool damaged = frame.framingError || frame.parityError || frame.stopError;
    return !line.words ||
           line.words.put(s + f_.begin[1], dataEnd, plugin::kItemData, frame.value, damaged ? plugin::kItemDamaged : 0u);
  }

  const Framing& f_;
  plugin::Annotations& out_;
  std::array<Line, W>& lines_;
  std::array<uint64_t, kMaxFrameBits + 1> points_{};
  uint32_t parityPoint_ = 0;
  uint32_t stopPoint_ = 0;
  uint32_t lastPoint_ = 0;
};

template <size_t W>
void walk(const Framing& f, plugin::DecodeInput& input, plugin::Annotations& out, std::array<Line, W>& lines,
          const std::array<plugin::WatchedLine, W>& watched) {
  UartWalk<W> uart(f, out, lines);
  auto oldestOpen = [&uart] { return uart.oldestOpen(); };
  input.onProgress(oldestOpen);
  const auto end = plugin::forEachChange(
      input, watched, std::array<uint32_t, 0>{},
      [&uart](uint32_t levels) {
        uart.start(levels);
        return true;
      },
      [&uart](const auto& c) { return uart.change(c); });
  if (end) uart.finish(*end);
}

}

std::expected<void, std::string> NativeUart::decode(const plugin::DecodeJob& job, plugin::DecodeInput& input,
                                                    plugin::Annotations& out) {
  const Framing f = framingOf(job);
  std::array<Line, 2> lines{};
  std::array<plugin::WatchedLine, 2> watched{};
  size_t count = 0;
  for (const auto& [name, row, invert] :
       {std::tuple{"rx", kRowRx, "invert_rx"}, std::tuple{"tx", kRowTx, "invert_tx"}}) {
    const auto channel = job.channel(name);
    if (!channel) continue;
    auto words = decoders_common::outputWriter(job, out, name);
    if (!words) return std::unexpected(words.error());
    lines[count].row = row;
    lines[count].invert = job.option(invert) == "yes";
    lines[count].words = *words;
    watched[count++] = {*channel, plugin::Edge::any};
  }
  if (count == 0) return std::unexpected("UART needs at least one of RX and TX");
  if (count == 2) {
    walk<2>(f, input, out, lines, watched);
  } else {
    std::array<Line, 1> one{std::move(lines[0])};
    walk<1>(f, input, out, one, std::array<plugin::WatchedLine, 1>{watched[0]});
  }
  return {};
}

}

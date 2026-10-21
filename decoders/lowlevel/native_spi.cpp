/*
 * SPDX-License-Identifier: MIT
 */

#include "native_spi.h"

#include <array>
#include <cstdint>
#include <optional>
#include <utility>

#include "output_writer.h"

namespace bitoscope::decoders_lowlevel {

using plugin::AnnotationClass;
using plugin::DecoderDeclaration;
using plugin::OptionType;
using plugin::StreamType;
using plugin::StreamWriter;
using plugin::ValueFormat;

DecoderDeclaration NativeSpi::declaration() const {
  DecoderDeclaration d;
  d.name = "spi";
  d.version = "1.0.0";
  d.channels = {{"clk", "clock", true, "CLK"},
                {"miso", "master in, slave out", false, "MISO"},
                {"mosi", "master out, slave in", false, "MOSI"},
                {"cs", "chip select", false, "CS"}};
  d.options = {
      {"cs_polarity", "chip select polarity", OptionType::string, "active-low", {"active-low", "active-high"},
       std::nullopt, std::nullopt, {}, "CS polarity"},
      {"cpol", "clock polarity", OptionType::integer, "0", {}, 0, 1, {}, "CPOL"},
      {"cpha", "clock phase", OptionType::integer, "0", {}, 0, 1, {}, "CPHA"},
      {"bitorder", "order of bits in word", OptionType::string, "msb-first", {"msb-first", "lsb-first"},
       std::nullopt, std::nullopt, {}, "Bit order"},
      {"wordsize", "bits per word", OptionType::integer, "8", {}, 1, 32, {}, "Word size"},
      {"show_bits", "one annotation per data bit", OptionType::string, "yes", {"yes", "no"}, std::nullopt,
       std::nullopt, {}, "Show bits"},
  };
  d.classes = {
      AnnotationClass{kClassWord, "", ValueFormat::hex, 0, "wordsize", plugin::AnnotationRole::data},
      AnnotationClass{kClassIncompleteWord, "Incomplete word", ValueFormat::nOfM, 0, "", plugin::AnnotationRole::error},
      AnnotationClass{kClassBit, "", ValueFormat::decimal, 0, "", plugin::AnnotationRole::bit},
      AnnotationClass{kClassLineTransfer, "Line transfer", ValueFormat::decimal, 0, "", plugin::AnnotationRole::control},
  };
  d.rows = {{kRowMosiBits, "MOSI bits"}, {kRowMosi, "MOSI"},
            {kRowMosiTransfer, "MOSI transfer"}, {kRowMisoBits, "MISO bits"},
            {kRowMiso, "MISO"}, {kRowMisoTransfer, "MISO transfer"},
            {kRowErrors, "Errors"}};
  plugin::RowSource mosiBits{kRowMosiBits, kClassBit, kRowMosi, {kClassWord}, "show_bits", plugin::RowRule::clockEdges};
  mosiBits.edges = {"clk", "mosi", "wordsize", 0};
  plugin::RowSource misoBits = mosiBits;
  misoBits.row = kRowMisoBits;
  misoBits.sourceRow = kRowMiso;
  misoBits.edges.dataChannel = "miso";
  d.rowSources = {mosiBits,
                  {kRowMosiTransfer, kClassLineTransfer, kRowMosi, {kClassWord}, "", plugin::RowRule::wordList},
                  misoBits,
                  {kRowMisoTransfer, kClassLineTransfer, kRowMiso, {kClassWord}, "", plugin::RowRule::wordList}};
  // WHY: Only transfers carries SPI: mosi and miso are one line's words without CS framing, owner rule.
  d.outputs = {{"mosi", "MOSI words, boundary at each CS release", StreamType::words, 0, "wordsize", "mosi"},
               {"miso", "MISO words, boundary at each CS release", StreamType::words, 0, "wordsize", "miso"},
               {"transfers", "CS-framed MOSI/MISO word pairs", StreamType::spiTransfers, 0, "wordsize", "cs", "",
                "spi"}};
  return d;
}

std::expected<void, std::string> NativeSpi::check(const plugin::DecodeJob& job) const {
  if (!job.channel("mosi") && !job.channel("miso")) return std::unexpected("SPI needs at least one of MISO and MOSI");
  return {};
}

namespace {


struct SpiOutputs {
  StreamWriter mosi;
  StreamWriter miso;
  StreamWriter transfers;
};

class SpiJob {
 public:
  SpiJob(plugin::Annotations& out, const SpiOutputs& outputs, bool has_cs, bool has_mosi, bool has_miso,
         bool active_level, bool msb_first, uint32_t word_size)
      : out_(out),
        outputs_(outputs),
        hasCs_(has_cs),
        hasMosi_(has_mosi),
        hasMiso_(has_miso),
        activeLevel_(active_level),
        msbFirst_(msb_first),
        wordSize_(word_size),
        noLine_((has_mosi ? 0u : plugin::kItemNoMosi) | (has_miso ? 0u : plugin::kItemNoMiso)) {}

  bool active() const { return active_; }

  uint64_t oldestOpen() const {
    if (lineWords_ > 0) return lineFirst_;
    if (bits_ > 0) return first_;
    return UINT64_MAX;
  }

  bool start(bool active) {
    active_ = active;
    return !(active_ && hasCs_ && outputs_.transfers) ||
           outputs_.transfers.put(0, 1, plugin::kItemBegin, 0, plugin::kItemDamaged);
  }

  bool clock(uint64_t at, uint64_t mosi, uint64_t miso) {
    if (!hasCs_) {
      if (hasLastEdge_) {
        const uint64_t gap = at - lastEdge_;
        if (lastGap_ > 0 && gap > uint64_t{wordSize_} * lastGap_ && !endLine()) return false;
        lastGap_ = gap;
      }
      hasLastEdge_ = true;
      lastEdge_ = at;
    }
    if (bits_ == 0) first_ = at;
    if (msbFirst_) {
      mosi_ = (mosi_ << 1) | mosi;
      miso_ = (miso_ << 1) | miso;
    } else {
      mosi_ |= mosi << bits_;
      miso_ |= miso << bits_;
    }
    if (++bits_ < wordSize_) return true;
    const uint64_t end = at + 1;
    const bool ok = (!hasMosi_ || out_.emit(first_, end, NativeSpi::kRowMosi, NativeSpi::kClassWord, mosi_)) &&
                    (!hasMiso_ || out_.emit(first_, end, NativeSpi::kRowMiso, NativeSpi::kClassWord, miso_)) &&
                    (!outputs_.mosi || outputs_.mosi.put(first_, end, plugin::kItemData, mosi_)) &&
                    (!outputs_.miso || outputs_.miso.put(first_, end, plugin::kItemData, miso_)) &&
                    (!outputs_.transfers ||
                     outputs_.transfers.put(first_, end, plugin::kItemData, mosi_ | (miso_ << 32), noLine_));
    if (lineWords_++ == 0) lineFirst_ = first_;
    lineEnd_ = end;
    bits_ = 0;
    mosi_ = 0;
    miso_ = 0;
    if (!ok) return false;
    if (hasCs_ || lineWords_ < NativeSpi::kMaxPieceWords) return true;
    return endLine();
  }

  bool chipSelect(uint64_t at, bool level) {
    const bool selected = level == activeLevel_;
    if (selected && !active_) {
      active_ = true;
      return !outputs_.transfers || outputs_.transfers.put(at, at + 1, plugin::kItemBegin);
    }
    if (!selected && active_) return endTransfer(at, false);
    return true;
  }

  bool endTransfer(uint64_t at, bool atEnd) {
    const bool partial = bits_ > 0;
    bool ok = true;
    if (partial) {
      const uint64_t n_of_m = uint64_t{bits_} | (uint64_t{wordSize_} << 32);
      ok = out_.emit(first_, at, NativeSpi::kRowErrors, NativeSpi::kClassIncompleteWord, n_of_m);
    }
    bits_ = 0;
    mosi_ = 0;
    miso_ = 0;
    ok = ok && endLine();
    active_ = false;
    if (!ok) return false;
    if (!atEnd) return boundary(at, at + 1, partial ? plugin::kItemDamaged : 0u);
    if (hasCs_ || partial) return boundary(at - 1, at, plugin::kItemDamaged);
    return true;
  }

 private:
  bool endLine() {
    const bool ok =
        lineWords_ == 0 ||
        ((!hasMosi_ ||
          out_.emit(lineFirst_, lineEnd_, NativeSpi::kRowMosiTransfer, NativeSpi::kClassLineTransfer, lineWords_)) &&
         (!hasMiso_ ||
          out_.emit(lineFirst_, lineEnd_, NativeSpi::kRowMisoTransfer, NativeSpi::kClassLineTransfer, lineWords_)));
    lineWords_ = 0;
    return ok;
  }

  bool boundary(uint64_t begin, uint64_t end, uint32_t flags) {
    return (!outputs_.mosi || outputs_.mosi.put(begin, end, plugin::kItemBoundary, 0, flags)) &&
           (!outputs_.miso || outputs_.miso.put(begin, end, plugin::kItemBoundary, 0, flags)) &&
           (!outputs_.transfers || outputs_.transfers.put(begin, end, plugin::kItemBoundary, 0, flags));
  }

  plugin::Annotations& out_;
  SpiOutputs outputs_;
  bool hasCs_;
  bool hasMosi_;
  bool hasMiso_;
  bool activeLevel_;
  bool msbFirst_;
  uint32_t wordSize_;
  uint32_t noLine_;

  bool active_ = false;
  uint32_t bits_ = 0;
  uint64_t mosi_ = 0;
  uint64_t miso_ = 0;
  uint64_t first_ = 0;

  uint64_t lineWords_ = 0;
  uint64_t lineFirst_ = 0;
  uint64_t lineEnd_ = 0;

  bool hasLastEdge_ = false;
  uint64_t lastEdge_ = 0;
  uint64_t lastGap_ = 0;
};

}

std::expected<void, std::string> NativeSpi::decode(const plugin::DecodeJob& job, plugin::DecodeInput& input,
                                                   plugin::Annotations& out) {
  const auto clk = job.channel("clk");
  if (!clk) return std::unexpected("SPI needs CLK channel");
  const auto mosi = job.channel("mosi");
  const auto miso = job.channel("miso");
  const auto cs = job.channel("cs");
  const bool cpol = job.optionInt("cpol").value_or(0) != 0;
  const bool cpha = job.optionInt("cpha").value_or(0) != 0;
  const int kind = cpol == cpha ? BITOSCOPE_EDGE_RISING : BITOSCOPE_EDGE_FALLING;
  const bool active_level = job.option("cs_polarity") == "active-high";
  SpiOutputs outputs;
  for (const auto& [name, writer] : {std::pair{"mosi", &outputs.mosi}, std::pair{"miso", &outputs.miso},
                                     std::pair{"transfers", &outputs.transfers}}) {
    auto w = decoders_common::outputWriter(job, out, name);
    if (!w) return std::unexpected(w.error());
    *writer = *w;
  }
  SpiJob spi(out, outputs, cs.has_value(), mosi.has_value(), miso.has_value(), active_level,
             job.option("bitorder") == "msb-first", static_cast<uint32_t>(job.optionInt("wordsize").value_or(8)));
  auto oldestOpen = [&spi] { return spi.oldestOpen(); };
  input.onProgress(oldestOpen);

  const plugin::Edge clockEdge = kind == BITOSCOPE_EDGE_RISING ? plugin::Edge::rising : plugin::Edge::falling;
  const std::array<uint32_t, 2> data{mosi.value_or(*clk), miso.value_or(*clk)};
  const auto clocked = [&](const auto& c) {
    return spi.clock(c.at(), mosi && c.sampled(0) ? 1u : 0u, miso && c.sampled(1) ? 1u : 0u);
  };
  std::optional<uint64_t> end;
  if (cs) {
    const std::array<plugin::WatchedLine, 2> lines{{{*clk, clockEdge}, {*cs, plugin::Edge::any}}};
    end = plugin::forEachChange(
        input, lines, data, [&](uint32_t levels) { return spi.start(((levels & 2u) != 0) == active_level); },
        [&](const auto& c) {
          // WHY: At one sample a chip select change is processed before a clock edge.
          if (c.changed(1) && !spi.chipSelect(c.at(), c.after(1))) return false;
          return !c.changed(0) || !spi.active() || clocked(c);
        });
  } else {
    const std::array<plugin::WatchedLine, 1> lines{{{*clk, clockEdge}}};
    end = plugin::forEachChange(
        input, lines, data, [&](uint32_t) { return spi.start(true); },
        [&](const auto& c) { return !spi.active() || clocked(c); });
  }
  if (end && spi.active() && !spi.endTransfer(*end, true)) return {};
  return {};
}

}

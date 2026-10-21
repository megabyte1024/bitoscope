/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <cstdint>
#include <expected>
#include <string>

#include "line_trace.h"

namespace bitoscope::sim {

struct I2cConfig {
  uint32_t scl_hz = 100'000;
};

class I2cBus {
 public:
  static std::expected<I2cBus, std::string> create(LineTrace& scl, LineTrace& sda,
                                                   uint64_t sample_rate_hz, SampleIndex start,
                                                   const I2cConfig& cfg = {}) {
    if (!scl.idleLevel() || !sda.idleLevel()) {
      return std::unexpected("I2C lines idle high (pulled up), but trace given idles low");
    }
    const auto ticks = Timebase::create(sample_rate_hz, 4ull * cfg.scl_hz);
    if (!ticks.has_value()) {
      return std::unexpected(ticks.error());
    }
    return I2cBus(scl, sda, *ticks, start);
  }

  void start() {
    sda_->set(at(0), false);
    scl_->set(at(1), false);
    tick_ += 2;
  }

  void repeatedStart() {
    sda_->set(at(0), true);
    scl_->set(at(1), true);
    sda_->set(at(2), false);
    scl_->set(at(3), false);
    tick_ += 4;
  }

  void stop() {
    sda_->set(at(0), false);
    scl_->set(at(1), true);
    sda_->set(at(2), true);
    tick_ += 4;
  }

  void writeByte(uint8_t value, bool slave_acks = true) {
    sendByte(value);
    bit(!slave_acks);
  }

  void readByte(uint8_t value, bool master_acks) {
    sendByte(value);
    bit(!master_acks);
  }

  SampleIndex endSample() const { return start_ + ticks_.at(tick_); }

 private:
  I2cBus(LineTrace& scl, LineTrace& sda, const Timebase& ticks, SampleIndex start)
      : scl_(&scl), sda_(&sda), ticks_(ticks), start_(start) {}

  SampleIndex at(uint64_t offset) const { return start_ + ticks_.at(tick_ + offset); }

  void bit(bool level) {
    sda_->set(at(0), level);
    scl_->set(at(1), true);
    scl_->set(at(3), false);
    tick_ += 4;
  }

  void sendByte(uint8_t value) {
    for (int b = 7; b >= 0; --b) {
      bit(((value >> b) & 1u) != 0);
    }
  }

  LineTrace* scl_;
  LineTrace* sda_;
  Timebase ticks_;
  SampleIndex start_;
  uint64_t tick_ = 0;
};

}

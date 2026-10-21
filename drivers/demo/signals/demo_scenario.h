/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <expected>
#include <string>
#include <string_view>
#include <vector>

#include "i2c.h"
#include "line_trace.h"
#include "spi.h"
#include "uart.h"

namespace bitoscope::sim {

inline constexpr uint32_t kDemoChannels = 32;
inline constexpr uint64_t kDemoFrameMicros = 4096;

namespace demo_channel {
inline constexpr uint32_t kUart0Tx = 0, kUart1Tx = 1, kUart2Tx = 2;
inline constexpr uint32_t kSpiACs = 3, kSpiASclk = 4, kSpiAMosi = 5, kSpiAMiso = 6;
inline constexpr uint32_t kSpiBCs = 7, kSpiBSclk = 8, kSpiBMosi = 9, kSpiBMiso = 10;
inline constexpr uint32_t kI2c0Scl = 11, kI2c0Sda = 12, kI2c1Scl = 13, kI2c1Sda = 14;
inline constexpr uint32_t kStrobe = 15;
inline constexpr uint32_t kCounterFirst = 16;
inline constexpr uint32_t kPwmFirst = 24;
inline constexpr uint32_t kDividerFirst = 28;
}

namespace demo {
inline constexpr std::string_view kUart0Text = "Hello, bitoscope!\r\n";
inline constexpr std::string_view kUart1Text = "temp=25.5C\r\n";
inline constexpr std::array<uint8_t, 6> kUart2Frame = {0xAA, 0x55, 0x01, 0x02, 0x03, 0xFF};
}

struct DemoFrame {
  uint64_t sample_rate_hz = 0;
  std::vector<Word> samples;
};

inline std::expected<DemoFrame, std::string> renderDemoFrame(uint64_t sample_rate_hz) {
  namespace ch = demo_channel;
  if (sample_rate_hz < 1'000'000) {
    return std::unexpected("sample rate " + std::to_string(sample_rate_hz) +
                            " Hz is too low for demo scenario: its SPI and I2C traffic needs at "
                            "least 1 MHz");
  }
  if (sample_rate_hz > 500'000'000) {
    return std::unexpected("sample rate " + std::to_string(sample_rate_hz) +
                            " Hz is above 500 MHz, maximum for demo scenario (device ceiling: 4 channels x 500 MHz)");
  }
  if (sample_rate_hz % 62'500 != 0) {
    return std::unexpected("sample rate must be multiple of 62500 Hz so 16 us counter step "
                            "and 32 us divider fall on whole samples, got " +
                            std::to_string(sample_rate_hz));
  }
  const uint64_t frame_samples = sample_rate_hz * kDemoFrameMicros / 1'000'000;
  const auto us = [&](uint64_t micros) -> SampleIndex { return sample_rate_hz * micros / 1'000'000; };

  std::vector<LineTrace> lines(16);
  for (const uint32_t idle_high : {ch::kUart0Tx, ch::kUart1Tx, ch::kUart2Tx, ch::kSpiACs,
                                    ch::kSpiBCs, ch::kI2c0Scl, ch::kI2c0Sda, ch::kI2c1Scl,
                                    ch::kI2c1Sda}) {
    lines[idle_high] = LineTrace(true);
  }
  lines[ch::kSpiBSclk] = LineTrace(true);

  SampleIndex last_end = 0;
  std::string error;
  const auto ok = [&](const std::expected<SampleIndex, std::string>& r) {
    if (!r.has_value()) {
      error = r.error();
      return false;
    }
    last_end = std::max(last_end, *r);
    return true;
  };
  const auto bytes = [](std::string_view s) { return std::vector<uint8_t>(s.begin(), s.end()); };

  const auto text0 = bytes(demo::kUart0Text);
  const auto text1 = bytes(demo::kUart1Text);
  if (!ok(encodeUart(lines[ch::kUart0Tx], sample_rate_hz, us(100), text0,
                     UartConfig{115200, 8, Parity::None, 1})) ||
      !ok(encodeUart(lines[ch::kUart1Tx], sample_rate_hz, us(200), text1,
                     UartConfig{57600, 8, Parity::Even, 1})) ||
      !ok(encodeUart(lines[ch::kUart2Tx], sample_rate_hz, us(2000), demo::kUart2Frame,
                     UartConfig{38400, 8, Parity::None, 2}))) {
    return std::unexpected(error);
  }

  SpiLines spi_a{lines[ch::kSpiACs], lines[ch::kSpiASclk], lines[ch::kSpiAMosi],
                 lines[ch::kSpiAMiso]};
  const SpiConfig cfg_a{250'000, 0};
  const std::array<uint8_t, 4> jedec_out = {0x9F, 0x00, 0x00, 0x00};
  const std::array<uint8_t, 4> jedec_in = {0x00, 0xEF, 0x40, 0x18};
  const std::array<uint8_t, 8> read_out = {0x03, 0x00, 0x00, 0x10, 0x00, 0x00, 0x00, 0x00};
  const std::array<uint8_t, 8> read_in = {0x00, 0x00, 0x00, 0x00, 'B', 'I', 'T', 'O'};
  if (!ok(encodeSpiTransaction(spi_a, sample_rate_hz, us(300), cfg_a, jedec_out, jedec_in)) ||
      !ok(encodeSpiTransaction(spi_a, sample_rate_hz, us(700), cfg_a, read_out, read_in))) {
    return std::unexpected(error);
  }

  SpiLines spi_b{lines[ch::kSpiBCs], lines[ch::kSpiBSclk], lines[ch::kSpiBMosi],
                 lines[ch::kSpiBMiso]};
  const SpiConfig cfg_b{125'000, 3};
  const std::array<uint8_t, 2> who_out = {0x8F, 0x00};
  const std::array<uint8_t, 2> who_in = {0x00, 0x33};
  const std::array<uint8_t, 7> burst_out = {0xE8, 0, 0, 0, 0, 0, 0};
  const std::array<uint8_t, 7> burst_in = {0x00, 0x10, 0x00, 0xF0, 0xFF, 0x00, 0x40};
  if (!ok(encodeSpiTransaction(spi_b, sample_rate_hz, us(400), cfg_b, who_out, who_in)) ||
      !ok(encodeSpiTransaction(spi_b, sample_rate_hz, us(1000), cfg_b, burst_out, burst_in))) {
    return std::unexpected(error);
  }

  auto i2c0 = I2cBus::create(lines[ch::kI2c0Scl], lines[ch::kI2c0Sda], sample_rate_hz, us(500));
  if (!i2c0.has_value()) {
    return std::unexpected(i2c0.error());
  }
  i2c0->start();
  i2c0->writeByte(0x48 << 1);
  i2c0->writeByte(0x00);
  i2c0->repeatedStart();
  i2c0->writeByte((0x48 << 1) | 1);
  constexpr bool masterAcks = true;
  constexpr bool masterNacks = false;
  i2c0->readByte(0x19, masterAcks);
  i2c0->readByte(0x80, masterNacks);
  i2c0->stop();
  last_end = std::max(last_end, i2c0->endSample());

  auto write_bus =
      I2cBus::create(lines[ch::kI2c1Scl], lines[ch::kI2c1Sda], sample_rate_hz, us(200));
  auto read_bus =
      I2cBus::create(lines[ch::kI2c1Scl], lines[ch::kI2c1Sda], sample_rate_hz, us(1500));
  if (!write_bus.has_value() || !read_bus.has_value()) {
    return std::unexpected(!write_bus.has_value() ? write_bus.error() : read_bus.error());
  }
  write_bus->start();
  write_bus->writeByte(0x50 << 1);
  write_bus->writeByte(0x00);
  for (const char c : std::string_view("BITO")) {
    write_bus->writeByte(static_cast<uint8_t>(c));
  }
  write_bus->stop();
  read_bus->start();
  read_bus->writeByte(0x50 << 1);
  read_bus->writeByte(0x00);
  read_bus->repeatedStart();
  read_bus->writeByte((0x50 << 1) | 1);
  read_bus->readByte('B', true);
  read_bus->readByte('I', true);
  read_bus->readByte('T', true);
  read_bus->readByte('O', false);
  read_bus->stop();
  last_end = std::max({last_end, write_bus->endSample(), read_bus->endSample()});

  const SampleIndex strobe_period = us(1024);
  for (uint64_t k = 0; k < 4; ++k) {
    lines[ch::kStrobe].set(k * strobe_period, true);
    lines[ch::kStrobe].set(k * strobe_period + strobe_period / 2, false);
  }

  if (last_end > frame_samples) {
    return std::unexpected("demo scenario does not fit in its frame at this sample rate");
  }

  DemoFrame frame;
  frame.sample_rate_hz = sample_rate_hz;
  frame.samples = renderLines(lines, frame_samples);

  const uint64_t counter_step = us(16);
  const uint64_t pwm_period = us(256);
  const std::array<unsigned, 4> pwm_eighths = {1, 2, 4, 6};
  const std::array<uint64_t, 4> divider_period = {us(32), us(64), us(128), us(512)};
  for (uint64_t t = 0; t < frame_samples; ++t) {
    Word w = frame.samples[static_cast<size_t>(t)];
    w |= static_cast<Word>((t / counter_step) % 256) << ch::kCounterFirst;
    for (unsigned k = 0; k < 4; ++k) {
      if ((t % pwm_period) < pwm_period * pwm_eighths[k] / 8) {
        w |= Word{1} << (ch::kPwmFirst + k);
      }
      if ((t % divider_period[k]) < divider_period[k] / 2) {
        w |= Word{1} << (ch::kDividerFirst + k);
      }
    }
    frame.samples[static_cast<size_t>(t)] = w;
  }
  return frame;
}

}

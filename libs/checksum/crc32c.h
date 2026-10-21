/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

// WHY: This is CRC-32C (Castagnoli, polynomial 0x1EDC6F41), NOT the IEEE CRC-32 that zlib and gzip use.

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>

#if defined(_MSC_VER) && defined(_M_X64)
#include <intrin.h>
#include <nmmintrin.h>
#define BITOSCOPE_CRC32C_HARDWARE 1
#define BITOSCOPE_CRC32C_HARDWARE_GCC_X86 0
#elif (defined(__GNUC__) || defined(__clang__)) && (defined(__x86_64__) || defined(_M_X64))
#include <nmmintrin.h>
#define BITOSCOPE_CRC32C_HARDWARE 1
#define BITOSCOPE_CRC32C_HARDWARE_GCC_X86 1
#else
#define BITOSCOPE_CRC32C_HARDWARE 0
#define BITOSCOPE_CRC32C_HARDWARE_GCC_X86 0
#endif

namespace bitoscope::host {

namespace crc32c_detail {

inline constexpr uint32_t kReflectedPolynomial = 0x82F63B78u;

using Tables = std::array<std::array<uint32_t, 256>, 8>;

constexpr Tables makeTables() {
  Tables tables{};
  for (uint32_t i = 0; i < 256; ++i) {
    uint32_t crc = i;
    for (int bit = 0; bit < 8; ++bit) {
      crc = (crc >> 1) ^ ((crc & 1u) != 0 ? kReflectedPolynomial : 0u);
    }
    tables[0][i] = crc;
  }
  for (size_t k = 1; k < 8; ++k) {
    for (size_t i = 0; i < 256; ++i) {
      const uint32_t previous = tables[k - 1][i];
      tables[k][i] = (previous >> 8) ^ tables[0][previous & 0xFFu];
    }
  }
  return tables;
}

inline constexpr Tables kTables = makeTables();

}

inline uint32_t crc32cByte(std::span<const uint8_t> data, uint32_t seed = 0) {
  uint32_t crc = ~seed;
  for (const uint8_t byte : data) {
    crc = (crc >> 8) ^ crc32c_detail::kTables[0][(crc ^ byte) & 0xFFu];
  }
  return ~crc;
}

inline uint32_t crc32cSlicing(std::span<const uint8_t> data, uint32_t seed = 0) {
  if constexpr (std::endian::native != std::endian::little) {
    return crc32cByte(data, seed);
  } else {
    const auto& t = crc32c_detail::kTables;
    uint32_t crc = ~seed;
    const uint8_t* p = data.data();
    size_t remaining = data.size();

    while (remaining >= 8) {
      uint32_t low = 0;
      uint32_t high = 0;
      std::memcpy(&low, p, 4);
      std::memcpy(&high, p + 4, 4);
      low ^= crc;
      crc = t[7][low & 0xFFu] ^ t[6][(low >> 8) & 0xFFu] ^ t[5][(low >> 16) & 0xFFu] ^
            t[4][low >> 24] ^ t[3][high & 0xFFu] ^ t[2][(high >> 8) & 0xFFu] ^
            t[1][(high >> 16) & 0xFFu] ^ t[0][high >> 24];
      p += 8;
      remaining -= 8;
    }
    while (remaining > 0) {
      crc = (crc >> 8) ^ t[0][(crc ^ *p) & 0xFFu];
      ++p;
      --remaining;
    }
    return ~crc;
  }
}

inline bool crc32cHardwareAvailable() {
#if BITOSCOPE_CRC32C_HARDWARE_GCC_X86
  static const bool available = __builtin_cpu_supports("sse4.2");
  return available;
#elif BITOSCOPE_CRC32C_HARDWARE
  static const bool available = [] {
    int registers[4] = {};
    __cpuid(registers, 1);
    return (registers[2] & (1 << 20)) != 0;
  }();
  return available;
#else
  return false;
#endif
}

#if BITOSCOPE_CRC32C_HARDWARE_GCC_X86
// WHY: SSE4.2 via function attribute, not -march: crc32cHardwareAvailable() must gate every call.
[[gnu::target("sse4.2")]] inline uint32_t crc32cHardwareGccX86(std::span<const uint8_t> data, uint32_t seed) {
  const uint8_t* p = data.data();
  size_t remaining = data.size();
  uint64_t crc = static_cast<uint32_t>(~seed);
  while (remaining >= 8) {
    uint64_t word = 0;
    std::memcpy(&word, p, 8);
    crc = _mm_crc32_u64(crc, word);
    p += 8;
    remaining -= 8;
  }
  uint32_t tail = static_cast<uint32_t>(crc);
  while (remaining > 0) {
    tail = _mm_crc32_u8(tail, *p);
    ++p;
    --remaining;
  }
  return ~tail;
}
#endif

inline uint32_t crc32cHardware(std::span<const uint8_t> data, uint32_t seed = 0) {
#if BITOSCOPE_CRC32C_HARDWARE_GCC_X86
  if (crc32cHardwareAvailable()) return crc32cHardwareGccX86(data, seed);
#elif BITOSCOPE_CRC32C_HARDWARE
  if (crc32cHardwareAvailable()) {
    const uint8_t* p = data.data();
    size_t remaining = data.size();
    uint64_t crc = static_cast<uint32_t>(~seed);
    while (remaining >= 8) {
      uint64_t word = 0;
      std::memcpy(&word, p, 8);
      crc = _mm_crc32_u64(crc, word);
      p += 8;
      remaining -= 8;
    }
    uint32_t tail = static_cast<uint32_t>(crc);
    while (remaining > 0) {
      tail = _mm_crc32_u8(tail, *p);
      ++p;
      --remaining;
    }
    return ~tail;
  }
#endif
  return crc32cSlicing(data, seed);
}

inline uint32_t crc32c(std::span<const uint8_t> data, uint32_t seed = 0) {
  return crc32cHardwareAvailable() ? crc32cHardware(data, seed) : crc32cSlicing(data, seed);
}

}

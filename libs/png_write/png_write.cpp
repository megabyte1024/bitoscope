/*
 * SPDX-License-Identifier: MIT
 */

#include "png_write.h"

#include <array>
#include <cstddef>

namespace bitoscope::png_write {

namespace {

void appendU32Be(std::vector<std::uint8_t>& out, std::uint32_t value) {
  out.push_back(static_cast<std::uint8_t>((value >> 24) & 0xFF));
  out.push_back(static_cast<std::uint8_t>((value >> 16) & 0xFF));
  out.push_back(static_cast<std::uint8_t>((value >> 8) & 0xFF));
  out.push_back(static_cast<std::uint8_t>(value & 0xFF));
}

void appendU16Le(std::vector<std::uint8_t>& out, std::uint16_t value) {
  out.push_back(static_cast<std::uint8_t>(value & 0xFF));
  out.push_back(static_cast<std::uint8_t>((value >> 8) & 0xFF));
}

// WHY: This is the IEEE CRC-32, not libs/checksum's CRC-32C: PNG mandates polynomial 0xEDB88320.
std::uint32_t crc32(const std::uint8_t* data, std::size_t size) {
  static const std::array<std::uint32_t, 256> table = [] {
    std::array<std::uint32_t, 256> t{};
    for (std::uint32_t n = 0; n < 256; ++n) {
      std::uint32_t c = n;
      for (int k = 0; k < 8; ++k) {
        c = (c & 1) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
      }
      t[n] = c;
    }
    return t;
  }();
  std::uint32_t crc = 0xFFFFFFFFu;
  for (std::size_t i = 0; i < size; ++i) {
    crc = table[(crc ^ data[i]) & 0xFF] ^ (crc >> 8);
  }
  return crc ^ 0xFFFFFFFFu;
}

std::uint32_t adler32(const std::uint8_t* data, std::size_t size) {
  std::uint32_t a = 1;
  std::uint32_t b = 0;
  const std::uint32_t kModulo = 65521;
  constexpr std::size_t kAdler32MaxBytesBeforeReduction = 5552;
  std::size_t i = 0;
  while (i < size) {
    const std::size_t chunk = (size - i < kAdler32MaxBytesBeforeReduction) ? (size - i) : kAdler32MaxBytesBeforeReduction;
    for (std::size_t j = 0; j < chunk; ++j) {
      a += data[i + j];
      b += a;
    }
    a %= kModulo;
    b %= kModulo;
    i += chunk;
  }
  return (b << 16) | a;
}

void appendChunk(std::vector<std::uint8_t>& out, const char type[4],
                 const std::vector<std::uint8_t>& data) {
  appendU32Be(out, static_cast<std::uint32_t>(data.size()));
  const std::size_t typeAt = out.size();
  out.push_back(static_cast<std::uint8_t>(type[0]));
  out.push_back(static_cast<std::uint8_t>(type[1]));
  out.push_back(static_cast<std::uint8_t>(type[2]));
  out.push_back(static_cast<std::uint8_t>(type[3]));
  out.insert(out.end(), data.begin(), data.end());
  out.push_back(0);
  out.push_back(0);
  out.push_back(0);
  out.push_back(0);
  const std::uint32_t crc = crc32(out.data() + typeAt, 4 + data.size());
  out[out.size() - 4] = static_cast<std::uint8_t>((crc >> 24) & 0xFF);
  out[out.size() - 3] = static_cast<std::uint8_t>((crc >> 16) & 0xFF);
  out[out.size() - 2] = static_cast<std::uint8_t>((crc >> 8) & 0xFF);
  out[out.size() - 1] = static_cast<std::uint8_t>(crc & 0xFF);
}

std::vector<std::uint8_t> storedDeflate(const std::vector<std::uint8_t>& raw) {
  std::vector<std::uint8_t> out;
  std::size_t offset = 0;
  const std::size_t maxBlock = 65535;
  do {
    const std::size_t remaining = raw.size() - offset;
    const std::size_t blockLen = remaining < maxBlock ? remaining : maxBlock;
    const bool isFinal = (offset + blockLen) >= raw.size();
    out.push_back(isFinal ? 1 : 0);
    appendU16Le(out, static_cast<std::uint16_t>(blockLen));
    appendU16Le(out, static_cast<std::uint16_t>(~static_cast<std::uint16_t>(blockLen)));
    out.insert(out.end(), raw.begin() + static_cast<std::ptrdiff_t>(offset),
               raw.begin() + static_cast<std::ptrdiff_t>(offset + blockLen));
    offset += blockLen;
  } while (offset < raw.size());
  return out;
}

std::vector<std::uint8_t> zlibWrap(const std::vector<std::uint8_t>& raw) {
  const std::vector<std::uint8_t> deflated = storedDeflate(raw);
  std::vector<std::uint8_t> out;
  out.reserve(deflated.size() + 6);
  out.push_back(0x78);
  out.push_back(0x01);
  out.insert(out.end(), deflated.begin(), deflated.end());
  appendU32Be(out, adler32(raw.data(), raw.size()));
  return out;
}

}

std::vector<std::uint8_t> encodePng(std::uint32_t width, std::uint32_t height,
                                    const std::vector<std::uint8_t>& pixels) {
  std::vector<std::uint8_t> file{0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A};

  std::vector<std::uint8_t> ihdr;
  appendU32Be(ihdr, width);
  appendU32Be(ihdr, height);
  ihdr.push_back(8);
  ihdr.push_back(6);
  ihdr.push_back(0);
  ihdr.push_back(0);
  ihdr.push_back(0);
  appendChunk(file, "IHDR", ihdr);

  const std::size_t rowBytes = static_cast<std::size_t>(width) * 4;
  std::vector<std::uint8_t> scanlines;
  scanlines.reserve((rowBytes + 1) * height);
  for (std::uint32_t y = 0; y < height; ++y) {
    scanlines.push_back(0);
    const std::uint8_t* row = pixels.data() + static_cast<std::size_t>(y) * rowBytes;
    scanlines.insert(scanlines.end(), row, row + rowBytes);
  }
  appendChunk(file, "IDAT", zlibWrap(scanlines));
  appendChunk(file, "IEND", {});
  return file;
}

}

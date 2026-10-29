/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

// WHY: Reports the OS 'available' figure, cache included: a free-list-only figure refuses nearly every Linux capture.

#include <algorithm>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <string>

#include "cgroup_memory_limit.h"
#include "memory_probe.h"

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#elif defined(__linux__)
#include <cstdio>
#include <cstring>
#include <utility>
#endif

namespace bitoscope::capture {

#if defined(_WIN32)

class WindowsMemoryProbe final : public MemoryProbe {
 public:
  std::expected<uint64_t, std::string> freePhysicalBytes() override {
    MEMORYSTATUSEX status{};
    status.dwLength = sizeof(status);
    if (!GlobalMemoryStatusEx(&status)) {
      return std::unexpected("GlobalMemoryStatusEx failed, error " +
                             std::to_string(GetLastError()));
    }
    return static_cast<uint64_t>(status.ullAvailPhys);
  }
  std::expected<uint64_t, std::string> totalPhysicalBytes() override {
    MEMORYSTATUSEX status{};
    status.dwLength = sizeof(status);
    if (!GlobalMemoryStatusEx(&status)) {
      return std::unexpected("GlobalMemoryStatusEx failed, error " +
                             std::to_string(GetLastError()));
    }
    return static_cast<uint64_t>(status.ullTotalPhys);
  }
};

using SystemMemoryProbe = WindowsMemoryProbe;

#elif defined(__linux__)

class LinuxMemoryProbe final : public MemoryProbe {
 public:
  explicit LinuxMemoryProbe(std::filesystem::path root = "/") : root_(std::move(root)) {}

  std::expected<uint64_t, std::string> freePhysicalBytes() override {
    auto available = readKiB("MemAvailable:");
    if (!available) return available;
    if (const auto limited = cgroupAvailableBytes(root_)) return (std::min)(*available, *limited);
    return available;
  }
  std::expected<uint64_t, std::string> totalPhysicalBytes() override {
    return readKiB("MemTotal:");
  }

 private:
  std::expected<uint64_t, std::string> readKiB(const char* key) const {
    std::FILE* file = std::fopen((root_ / "proc/meminfo").c_str(), "r");
    if (file == nullptr) return std::unexpected("cannot open /proc/meminfo");
    char line[256];
    std::expected<uint64_t, std::string> result =
        std::unexpected(std::string("no ") + key + " line in /proc/meminfo");
    const size_t key_length = std::strlen(key);
    while (std::fgets(line, sizeof(line), file) != nullptr) {
      if (std::strncmp(line, key, key_length) != 0) continue;
      unsigned long long kib = 0;
      if (std::sscanf(line + key_length, " %llu", &kib) == 1) {
        result = static_cast<uint64_t>(kib) * 1024;
      } else {
        result = std::unexpected(std::string("cannot parse ") + key + " line");
      }
      break;
    }
    std::fclose(file);
    return result;
  }

  std::filesystem::path root_;
};

using SystemMemoryProbe = LinuxMemoryProbe;

#else

class UnsupportedMemoryProbe final : public MemoryProbe {
 public:
  std::expected<uint64_t, std::string> freePhysicalBytes() override { return unsupported(); }
  std::expected<uint64_t, std::string> totalPhysicalBytes() override { return unsupported(); }

 private:
  static std::unexpected<std::string> unsupported() {
    return std::unexpected(
        "reading free RAM is not supported on this platform yet; pass --memory-budget and "
        "--reserve");
  }
};

using SystemMemoryProbe = UnsupportedMemoryProbe;

#endif

}

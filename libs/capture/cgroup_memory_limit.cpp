/*
 * SPDX-License-Identifier: MIT
 */

#include "cgroup_memory_limit.h"

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstdlib>
#include <fstream>
#include <string>
#include <string_view>

namespace bitoscope::capture {

namespace {

namespace fs = std::filesystem;

constexpr uint64_t kV1Unlimited = uint64_t{1} << 62;

struct Reading {
  enum class Kind { Unknown, Unlimited, Bytes };
  Kind kind = Kind::Unknown;
  uint64_t bytes = 0;
};

Reading readNumber(const fs::path& file, bool v1) {
  std::ifstream in(file);
  std::string text;
  if (!in || !std::getline(in, text)) return {};
  while (!text.empty() && (text.back() == ' ' || text.back() == '\t' || text.back() == '\r')) text.pop_back();
  if (!v1 && text == "max") return {Reading::Kind::Unlimited, 0};
  if (text.empty() || text.find_first_not_of("0123456789") != std::string::npos) return {};
  errno = 0;
  const unsigned long long value = std::strtoull(text.c_str(), nullptr, 10);
  if (errno != 0) return {};
  if (v1 && value >= kV1Unlimited) return {Reading::Kind::Unlimited, 0};
  return {Reading::Kind::Bytes, static_cast<uint64_t>(value)};
}

struct Controller {
  std::string_view base;
  std::array<std::string_view, 2> limits;
  std::string_view current;
  bool v1;
};

constexpr Controller kV2{"sys/fs/cgroup", {"memory.max", "memory.high"}, "memory.current", false};
constexpr Controller kV1{"sys/fs/cgroup/memory", {"memory.limit_in_bytes", ""}, "memory.usage_in_bytes", true};

std::optional<uint64_t> levelAvailable(const fs::path& dir, const Controller& controller) {
  std::optional<uint64_t> limit;
  for (const std::string_view name : controller.limits) {
    if (name.empty()) continue;
    const Reading reading = readNumber(dir / std::string(name), controller.v1);
    if (reading.kind == Reading::Kind::Bytes) limit = limit ? (std::min)(*limit, reading.bytes) : reading.bytes;
  }
  if (!limit) return std::nullopt;
  const Reading current = readNumber(dir / std::string(controller.current), controller.v1);
  if (current.kind != Reading::Kind::Bytes) return std::nullopt;
  return *limit > current.bytes ? *limit - current.bytes : 0;
}

std::optional<uint64_t> walkAncestors(const fs::path& root, const Controller& controller, std::string_view cgroup) {
  const fs::path relative = fs::path(std::string(cgroup)).lexically_normal().relative_path();
  const fs::path base = root / std::string(controller.base);
  std::optional<uint64_t> smallest;
  for (fs::path dir = base / relative;; dir = dir.parent_path()) {
    if (const auto available = levelAvailable(dir, controller)) {
      smallest = smallest ? (std::min)(*smallest, *available) : *available;
    }
    if (dir == base || dir == dir.parent_path()) break;
  }
  return smallest;
}

}

std::optional<uint64_t> cgroupAvailableBytes(const std::filesystem::path& root) {
  std::ifstream in(root / "proc/self/cgroup");
  std::optional<uint64_t> smallest;
  const auto take = [&smallest](std::optional<uint64_t> value) {
    if (value) smallest = smallest ? (std::min)(*smallest, *value) : *value;
  };
  for (std::string line; std::getline(in, line);) {
    const size_t first = line.find(':');
    const size_t second = first == std::string::npos ? first : line.find(':', first + 1);
    if (second == std::string::npos) continue;
    const std::string_view controllers = std::string_view(line).substr(first + 1, second - first - 1);
    const std::string_view path = std::string_view(line).substr(second + 1);
    if (path.empty() || path.front() != '/') continue;
    if (line.compare(0, first, "0") == 0 && controllers.empty()) {
      take(walkAncestors(root, kV2, path));
      continue;
    }
    for (size_t at = 0; at <= controllers.size();) {
      size_t end = controllers.find(',', at);
      if (end == std::string_view::npos) end = controllers.size();
      if (controllers.substr(at, end - at) == "memory") take(walkAncestors(root, kV1, path));
      at = end + 1;
    }
  }
  return smallest;
}

}

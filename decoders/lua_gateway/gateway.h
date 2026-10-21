/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <expected>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "bitoscope_decoder.h"
#include "embedded_script.h"

struct lua_State;

namespace bitoscope::lua_gateway {

inline constexpr uint64_t kMaxLuaSamples = (uint64_t{1} << 53) - 1;

class Gateway {
 public:
  static std::expected<std::unique_ptr<Gateway>, std::string> load(
      std::span<const EmbeddedScript> builtins, const std::optional<std::filesystem::path>& script_dir);

  ~Gateway();
  Gateway(const Gateway&) = delete;
  Gateway& operator=(const Gateway&) = delete;

  const std::vector<std::string>& messages() const;
  std::vector<std::string> decoderNames() const;

  static int list(void* user, bitoscope_decoders* decoders);

  struct Script;

 private:
  Gateway();
  std::vector<std::unique_ptr<Script>> scripts_;
  std::vector<std::string> messages_;
};

struct CommandLine {
  std::vector<std::string> plugin_args;
  std::optional<std::filesystem::path> script_dir;
};
std::expected<CommandLine, std::string> parseCommandLine(std::span<const std::string> args);

int runGateway(int argc, char** argv, std::span<const EmbeddedScript> builtins);

int refuseDeclaration(lua_State* L);

}

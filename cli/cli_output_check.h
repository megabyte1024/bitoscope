/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <expected>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>

#include "cli_args.h"

namespace bitoscope::cli {

namespace output_detail {

inline bool present(const std::filesystem::path& p) {
  std::error_code ec;
  const auto status = std::filesystem::symlink_status(p, ec);
  if (status.type() == std::filesystem::file_type::not_found) return false;
  return true;
}

}

inline std::expected<void, std::string> checkOutputTarget(const CliOptions& opts) {
  namespace fs = std::filesystem;
  if (opts.output_file.empty()) return {};

  const fs::path target = fs::path(opts.output_file);
  const std::string shown = "'" + opts.output_file + "'";
  std::error_code ec;

  // WHY: symlink_status: a dangling symlink is still something the writer must not replace.
  if (output_detail::present(target)) {
    return std::unexpected("output file " + shown +
                           " already exists (or cannot be inspected); there is no overwrite, "
                           "remove it or choose another name");
  }
  fs::path temp = target;
  temp += ".tmp";
  if (output_detail::present(temp)) {
    return std::unexpected("temporary file '" + temp.string() +
                           "' already exists (failed earlier save?); remove it first");
  }

  fs::path dir = target.parent_path();
  if (dir.empty()) dir = ".";
  if (!fs::is_directory(dir, ec)) {
    return std::unexpected("output directory '" + dir.string() +
                           "' does not exist or is not directory");
  }

  fs::path probe = target;
  probe += ".probe";
  if (output_detail::present(probe)) {
    return std::unexpected("probe file '" + probe.string() +
                           "' already exists; remove it first");
  }
  {
    std::ofstream file(probe, std::ios::binary);
    if (!file) {
      return std::unexpected("cannot create files in output directory '" + dir.string() + "'");
    }
  }
  fs::remove(probe, ec);
  return {};
}

}

/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <string>
#include <string_view>

namespace bitoscope::gui_format {

enum class VersionBuildKind {
  kTagged,
  kDev,
  kNoGit,
};

struct VersionBuildInfo {
  VersionBuildKind kind = VersionBuildKind::kNoGit;
  std::string commit;
  bool dirty = false;
};

std::string formatVersion(std::string_view version, const VersionBuildInfo& build);

std::string currentVersionString();

}

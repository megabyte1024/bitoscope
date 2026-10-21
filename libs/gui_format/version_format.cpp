/*
 * SPDX-License-Identifier: MIT
 */

#include "version_format.h"

#include <bitoscope_git_version.h>

namespace bitoscope::gui_format {

std::string formatVersion(std::string_view version, const VersionBuildInfo& build) {
  if (build.kind != VersionBuildKind::kDev) {
    return std::string(version);
  }
  std::string commit = build.commit;
  if (build.dirty) {
    commit += "-dirty";
  }
  return std::string(version) + " (" + commit + ")";
}

std::string currentVersionString() {
  VersionBuildInfo build;
  const std::string_view kind = BITOSCOPE_GIT_KIND;
  if (kind == "tagged") {
    build.kind = VersionBuildKind::kTagged;
  } else if (kind == "dev") {
    build.kind = VersionBuildKind::kDev;
  } else {
    build.kind = VersionBuildKind::kNoGit;
  }
  build.commit = BITOSCOPE_GIT_COMMIT;
  build.dirty = BITOSCOPE_GIT_DIRTY != 0;
  return formatVersion(BITOSCOPE_VERSION, build);
}

}

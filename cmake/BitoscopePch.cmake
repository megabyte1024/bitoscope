# SPDX-License-Identifier: MIT

# WHY: Only standard headers: project or third-party header change would rebuild PCH and all its users.
option(BITOSCOPE_ENABLE_PCH "Precompile standard headers for the heaviest targets (cmake/BitoscopePch.cmake)" ON)

set(BITOSCOPE_PCH_HEADERS
    <algorithm> <array> <cmath> <cstddef> <cstdint> <expected> <format> <limits> <map> <memory>
    <optional> <span> <string> <string_view> <utility> <vector>)

# WHY: C++ only: not for a target with C sources.
function(bitoscope_enable_pch target)
  if(BITOSCOPE_ENABLE_PCH)
    target_precompile_headers(${target} PRIVATE ${BITOSCOPE_PCH_HEADERS})
  endif()
endfunction()

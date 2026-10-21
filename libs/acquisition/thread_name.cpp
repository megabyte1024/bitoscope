/*
 * SPDX-License-Identifier: MIT
 */

#include "thread_name.h"

#include <string>

#if defined(__linux__)
#include <pthread.h>
#elif defined(_WIN32)
#include <windows.h>
#endif

namespace bitoscope::acquisition {

void nameCurrentThread([[maybe_unused]] std::string_view name) {
#if defined(__linux__)
  constexpr size_t kMaxNameChars = 15;
  const std::string shortened(name.substr(0, kMaxNameChars));
  pthread_setname_np(pthread_self(), shortened.c_str());
#elif defined(_WIN32)
  std::wstring wide(name.begin(), name.end());
  SetThreadDescription(GetCurrentThread(), wide.c_str());
#endif
}

}

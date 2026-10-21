/*
 * SPDX-License-Identifier: MIT
 */

#include "hdf5_lock.h"

namespace bitoscope::hdf5 {

std::recursive_mutex& libraryMutex() {
  static std::recursive_mutex mutex;
  return mutex;
}

}

/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

// WHY: HDF5 is built without thread safety: two threads inside it corrupt its tables; recursive, entry points nest.

#include <mutex>

namespace bitoscope::hdf5 {

std::recursive_mutex& libraryMutex();

}

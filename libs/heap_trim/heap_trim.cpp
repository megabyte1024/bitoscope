/*
 * SPDX-License-Identifier: MIT
 */

#include "heap_trim.h"

// WHY: __GLIBC__ is set only once a libc header is in: without this include the trim compiles to nothing.
#include <cstdlib>

#if defined(__GLIBC__)
#include <malloc.h>
#endif

namespace bitoscope::heap {

void giveFreeHeapBack() {
#if defined(__GLIBC__)
  malloc_trim(0);
#endif
}

}

/*
 * SPDX-License-Identifier: MIT
 */

#ifndef BITOSCOPE_DECODER_LAYERS_H
#define BITOSCOPE_DECODER_LAYERS_H

#include <stdint.h>

#include "bitoscope_decoder.h"

#ifdef __cplusplus
extern "C" {
#endif

uint64_t bitoscope_settle_due(const bitoscope_annotations* out);

uint64_t bitoscope_progress_step(const bitoscope_annotations* out);

#ifdef __cplusplus
}
#endif

#endif

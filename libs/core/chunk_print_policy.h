/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <chrono>
#include <cstdint>

namespace bitoscope::host {

inline constexpr uint64_t kChunkPrintInterval = 200;
inline constexpr std::chrono::milliseconds kChunkPrintTimeInterval{500};

inline bool shouldEmitChunkSummary(uint64_t chunks_since_last_summary,
                                    std::chrono::steady_clock::duration elapsed_since_last_summary,
                                    uint64_t chunk_interval = kChunkPrintInterval,
                                    std::chrono::steady_clock::duration time_interval =
                                        kChunkPrintTimeInterval) {
  return chunks_since_last_summary >= chunk_interval || elapsed_since_last_summary >= time_interval;
}

}

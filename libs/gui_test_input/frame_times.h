/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "frame_probe.h"
#include "observation.h"

namespace bitoscope::gui_test_input {

enum class FramePhase : std::uint8_t {
  SlotWait,
  AppBegin,
  NewFrame,
  Draw,
  Render,
  Acquire,
  Record,
  Submit,
  Present,
  Hook,
};

inline constexpr std::size_t kFramePhaseCount = 10;

std::string_view framePhaseName(FramePhase phase);

using PhaseDurations = std::array<std::uint32_t, kFramePhaseCount>;

using gui_frame_probe::DrawPart;
using gui_frame_probe::kDrawPartCount;
using DrawPartDurations = std::array<std::uint32_t, gui_frame_probe::kDrawPartCount>;

inline constexpr std::size_t kDrawPartDepth = 16;

struct FrameRecord {
  std::uint64_t frame = 0;
  std::uint64_t startNs = 0;
  std::uint64_t periodNs = 0;
  PhaseDurations phaseNs{};
  DrawPartDurations drawPartNs{};
  bool distorted = false;
};

struct FrameSelection {
  enum class Kind : std::uint8_t { SinceMark, Last, All };
  Kind kind = Kind::SinceMark;
  std::uint32_t count = 0;
  friend bool operator==(const FrameSelection&, const FrameSelection&) = default;
};

struct DurationStats {
  std::uint64_t minNs = 0;
  std::uint64_t medianNs = 0;
  std::uint64_t p95Ns = 0;
  std::uint64_t p99Ns = 0;
  std::uint64_t maxNs = 0;
  friend bool operator==(const DurationStats&, const DurationStats&) = default;
};

struct LongFrame {
  std::uint64_t frame = 0;
  std::uint64_t atNs = 0;
  std::uint64_t periodNs = 0;
  PhaseDurations phaseNs{};
  std::uint64_t otherNs = 0;
  DrawPartDurations drawPartNs{};
  std::uint64_t drawOtherNs = 0;
  friend bool operator==(const LongFrame&, const LongFrame&) = default;
};

struct FrameBreak {
  enum class Kind : std::uint8_t { Missing, Duplicated };
  Kind kind = Kind::Missing;
  std::uint64_t frame = 0;
  friend bool operator==(const FrameBreak&, const FrameBreak&) = default;
};

struct CaptureMark {
  std::uint64_t frame = 0;
  std::uint64_t atNs = 0;
  std::string session;
  std::string state;
  friend bool operator==(const CaptureMark&, const CaptureMark&) = default;
};

struct FrameTimesSummary {
  std::uint64_t records = 0;
  std::uint64_t counted = 0;
  std::uint64_t excluded = 0;
  std::uint64_t open = 0;
  bool truncated = false;
  std::uint64_t lost = 0;
  std::uint64_t firstFrame = 0;
  std::uint64_t lastFrame = 0;
  std::uint64_t spanNs = 0;
  DurationStats period;
  DurationStats work;
  std::array<DurationStats, kFramePhaseCount> phases{};
  std::array<DurationStats, kDrawPartCount> drawParts{};
  DurationStats drawOther;
  std::vector<LongFrame> longest;
  std::uint64_t breaks = 0;
  std::optional<FrameBreak> firstBreak;
  std::vector<CaptureMark> marks;
};

inline constexpr std::size_t kFrameTimesCapacity = 16384;
inline constexpr std::size_t kLongestFrames = 5;
inline constexpr std::size_t kCaptureMarkCapacity = 1024;

std::uint64_t nearestRank(std::size_t count, std::uint32_t percent);

class FrameTimes {
 public:
  explicit FrameTimes(std::size_t capacity = kFrameTimesCapacity);

  void beginFrame(std::uint64_t nowNs);
  void endPhase(FramePhase phase, std::uint64_t nowNs);
  void enterPart(DrawPart part, std::uint64_t nowNs);
  void leavePart(std::uint64_t nowNs);
  void noteCaptures(const std::vector<SessionCapture>& captures);
  void commitFrame(std::uint64_t frame, bool distorted);

  void mark();

  std::uint64_t committed() const { return committed_; }

  FrameTimesSummary summarize(FrameSelection selection) const;

 private:
  struct StoredMark {
    std::uint64_t sequence = 0;
    CaptureMark mark;
  };

  const FrameRecord& recordAt(std::uint64_t sequence) const;
  void stampPart(std::uint64_t nowNs);

  std::vector<FrameRecord> ring_;
  std::uint64_t committed_ = 0;
  std::uint64_t markSequence_ = 0;
  FrameRecord pending_;
  bool pendingOpen_ = false;
  std::uint64_t lastStampNs_ = 0;
  std::array<DrawPart, kDrawPartDepth> partStack_{};
  std::size_t partDepth_ = 0;
  std::uint64_t partStampNs_ = 0;
  std::vector<std::pair<std::string, std::string>> lastStates_;
  bool statesSeen_ = false;
  std::vector<CaptureMark> pendingMarks_;
  std::deque<StoredMark> marks_;
};

}

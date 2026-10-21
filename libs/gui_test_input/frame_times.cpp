/*
 * SPDX-License-Identifier: MIT
 */

#include "frame_times.h"

#include <algorithm>
#include <limits>

namespace bitoscope::gui_test_input {

namespace {

constexpr std::array<std::string_view, kFramePhaseCount> kPhaseNames{
  "slot-wait", "app-begin", "new-frame", "draw", "render", "acquire", "record", "submit", "present", "hook"};

constexpr std::uint64_t kDurationLimit = std::numeric_limits<std::uint32_t>::max();

void addSaturated(std::uint32_t& slot, std::uint64_t elapsed) {
  slot = static_cast<std::uint32_t>(std::min<std::uint64_t>(kDurationLimit, slot + elapsed));
}

std::uint64_t drawOtherOf(const FrameRecord& record) {
  std::uint64_t parts = 0;
  for (const std::uint32_t ns : record.drawPartNs) {
    parts += ns;
  }
  const std::uint64_t draw = record.phaseNs[static_cast<std::size_t>(FramePhase::Draw)];
  return draw > parts ? draw - parts : 0;
}

std::uint64_t workOf(const PhaseDurations& phases) {
  std::uint64_t sum = 0;
  for (const std::uint32_t ns : phases) {
    sum += ns;
  }
  return sum;
}

DurationStats statsOf(std::vector<std::uint64_t> values) {
  DurationStats stats;
  if (values.empty()) {
    return stats;
  }
  std::ranges::sort(values);
  const auto at = [&values](std::uint32_t percent) { return values[nearestRank(values.size(), percent) - 1]; };
  stats.minNs = values.front();
  stats.medianNs = at(50);
  stats.p95Ns = at(95);
  stats.p99Ns = at(99);
  stats.maxNs = values.back();
  return stats;
}

}

std::string_view framePhaseName(FramePhase phase) {
  const auto index = static_cast<std::size_t>(phase);
  return index < kPhaseNames.size() ? kPhaseNames[index] : std::string_view("unknown");
}

std::uint64_t nearestRank(std::size_t count, std::uint32_t percent) {
  if (count == 0) {
    return 0;
  }
  const std::uint64_t rank = (static_cast<std::uint64_t>(count) * percent + 99) / 100;
  return std::clamp<std::uint64_t>(rank, 1, count);
}

FrameTimes::FrameTimes(std::size_t capacity) : ring_(std::max<std::size_t>(capacity, 1)) {}

const FrameRecord& FrameTimes::recordAt(std::uint64_t sequence) const { return ring_[sequence % ring_.size()]; }

void FrameTimes::beginFrame(std::uint64_t nowNs) {
  if (committed_ > 0) {
    FrameRecord& previous = ring_[(committed_ - 1) % ring_.size()];
    if (previous.periodNs == 0) {
      previous.periodNs = nowNs > previous.startNs ? nowNs - previous.startNs : 1;
    }
  }
  pending_ = FrameRecord{};
  pending_.startNs = nowNs;
  pendingOpen_ = true;
  lastStampNs_ = nowNs;
  partDepth_ = 0;
  partStampNs_ = nowNs;
}

void FrameTimes::endPhase(FramePhase phase, std::uint64_t nowNs) {
  const std::uint64_t elapsed = nowNs > lastStampNs_ ? nowNs - lastStampNs_ : 0;
  addSaturated(pending_.phaseNs[static_cast<std::size_t>(phase)], elapsed);
  lastStampNs_ = std::max(lastStampNs_, nowNs);
}

void FrameTimes::stampPart(std::uint64_t nowNs) {
  if (partDepth_ > 0) {
    const DrawPart innermost = partStack_[std::min(partDepth_, kDrawPartDepth) - 1];
    addSaturated(pending_.drawPartNs[static_cast<std::size_t>(innermost)],
                 nowNs > partStampNs_ ? nowNs - partStampNs_ : 0);
  }
  partStampNs_ = std::max(partStampNs_, nowNs);
}

void FrameTimes::enterPart(DrawPart part, std::uint64_t nowNs) {
  stampPart(nowNs);
  if (partDepth_ < kDrawPartDepth) {
    partStack_[partDepth_] = part;
  }
  ++partDepth_;
}

void FrameTimes::leavePart(std::uint64_t nowNs) {
  if (partDepth_ == 0) {
    return;
  }
  stampPart(nowNs);
  --partDepth_;
}

void FrameTimes::noteCaptures(const std::vector<SessionCapture>& captures) {
  const std::uint64_t atNs = pendingOpen_ ? pending_.startNs : lastStampNs_;
  std::vector<std::pair<std::string, std::string>> states;
  states.reserve(captures.size());
  for (const SessionCapture& capture : captures) {
    states.emplace_back(capture.name, capture.state);
    if (!statesSeen_) {
      continue;
    }
    const auto before = std::ranges::find(lastStates_, capture.name,
                                          &std::pair<std::string, std::string>::first);
    if (before == lastStates_.end() || before->second != capture.state) {
      pendingMarks_.push_back(CaptureMark{0, atNs, capture.name, capture.state});
    }
  }
  lastStates_ = std::move(states);
  statesSeen_ = true;
}

void FrameTimes::commitFrame(std::uint64_t frame, bool distorted) {
  if (!pendingOpen_) {
    return;
  }
  pending_.frame = frame;
  pending_.distorted = distorted;
  ring_[committed_ % ring_.size()] = pending_;
  for (CaptureMark& mark : pendingMarks_) {
    mark.frame = frame;
    marks_.push_back(StoredMark{committed_, std::move(mark)});
    if (marks_.size() > kCaptureMarkCapacity) {
      marks_.pop_front();
    }
  }
  pendingMarks_.clear();
  ++committed_;
  pendingOpen_ = false;
}

void FrameTimes::mark() { markSequence_ = committed_ + (pendingOpen_ ? 1 : 0); }

FrameTimesSummary FrameTimes::summarize(FrameSelection selection) const {
  FrameTimesSummary summary;
  std::uint64_t begin = 0;
  switch (selection.kind) {
  case FrameSelection::Kind::SinceMark:
    begin = markSequence_;
    break;
  case FrameSelection::Kind::Last:
    begin = committed_ > selection.count ? committed_ - selection.count : 0;
    break;
  case FrameSelection::Kind::All:
    begin = 0;
    break;
  }
  const std::uint64_t held = committed_ > ring_.size() ? committed_ - ring_.size() : 0;
  if (begin < held) {
    summary.truncated = true;
    summary.lost = held - begin;
    begin = held;
  }
  const std::uint64_t end = committed_;
  if (begin >= end) {
    return summary;
  }
  summary.records = end - begin;
  const FrameRecord& first = recordAt(begin);
  const FrameRecord& final = recordAt(end - 1);
  summary.firstFrame = first.frame;
  summary.lastFrame = final.frame;
  summary.spanNs = final.startNs + final.periodNs - first.startNs;

  std::vector<std::uint64_t> periods;
  std::vector<std::uint64_t> works;
  std::array<std::vector<std::uint64_t>, kFramePhaseCount> phases;
  std::array<std::vector<std::uint64_t>, kDrawPartCount> parts;
  std::vector<std::uint64_t> drawOthers;
  std::vector<LongFrame> candidates;
  std::uint64_t previousFrame = 0;
  for (std::uint64_t sequence = begin; sequence < end; ++sequence) {
    const FrameRecord& record = recordAt(sequence);
    if (sequence > begin && record.frame != previousFrame + 1) {
      ++summary.breaks;
      if (!summary.firstBreak) {
        summary.firstBreak = record.frame <= previousFrame
                               ? FrameBreak{FrameBreak::Kind::Duplicated, record.frame}
                               : FrameBreak{FrameBreak::Kind::Missing, previousFrame + 1};
      }
    }
    previousFrame = record.frame;
    if (record.periodNs == 0) {
      ++summary.open;
      continue;
    }
    if (record.distorted) {
      ++summary.excluded;
      continue;
    }
    ++summary.counted;
    const std::uint64_t work = workOf(record.phaseNs);
    periods.push_back(record.periodNs);
    works.push_back(work);
    for (std::size_t phase = 0; phase < kFramePhaseCount; ++phase) {
      phases[phase].push_back(record.phaseNs[phase]);
    }
    for (std::size_t part = 0; part < kDrawPartCount; ++part) {
      parts[part].push_back(record.drawPartNs[part]);
    }
    const std::uint64_t drawOther = drawOtherOf(record);
    drawOthers.push_back(drawOther);
    candidates.push_back(LongFrame{record.frame, record.startNs - first.startNs, record.periodNs, record.phaseNs,
                                   record.periodNs > work ? record.periodNs - work : 0, record.drawPartNs,
                                   drawOther});
  }
  summary.period = statsOf(std::move(periods));
  summary.work = statsOf(std::move(works));
  for (std::size_t phase = 0; phase < kFramePhaseCount; ++phase) {
    summary.phases[phase] = statsOf(std::move(phases[phase]));
  }
  for (std::size_t part = 0; part < kDrawPartCount; ++part) {
    summary.drawParts[part] = statsOf(std::move(parts[part]));
  }
  summary.drawOther = statsOf(std::move(drawOthers));
  const std::size_t keep = std::min(kLongestFrames, candidates.size());
  std::ranges::partial_sort(candidates, candidates.begin() + static_cast<std::ptrdiff_t>(keep),
                            [](const LongFrame& a, const LongFrame& b) {
                              return a.periodNs != b.periodNs ? a.periodNs > b.periodNs : a.atNs < b.atNs;
                            });
  candidates.resize(keep);
  summary.longest = std::move(candidates);

  for (const StoredMark& stored : marks_) {
    if (stored.sequence >= begin && stored.sequence < end) {
      CaptureMark mark = stored.mark;
      mark.atNs = mark.atNs > first.startNs ? mark.atNs - first.startNs : 0;
      summary.marks.push_back(std::move(mark));
    }
  }
  return summary;
}

}

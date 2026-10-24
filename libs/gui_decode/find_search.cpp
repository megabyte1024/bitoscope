/*
 * SPDX-License-Identifier: MIT
 */

#include "find_search.h"

#include <algorithm>
#include <new>
#include <utility>

namespace bitoscope::gui_decode {

namespace {

const DecodedAnnotations* annotationsOf(const FindSource& source) {
  return source.shown != nullptr ? source.shown->annotations.get() : nullptr;
}

}

FindSearch::FindSearch(FindScanStep scan, std::function<void()> resultPosted, std::chrono::milliseconds waitShownAfter)
    : scan_(std::move(scan)), resultPosted_(std::move(resultPosted)), waitShownAfter_(waitShownAfter),
      parsed_(std::unexpected(FindPatternError::Empty)) {}

FindSearch::~FindSearch() {
  // WHY: Stop the job before joining: a scan watches only its job's stop token, not the thread's.
  jobStop_.request_stop();
  if (worker_.joinable()) {
    worker_.request_stop();
    worker_.join();
  }
}

void FindSearch::update(const FindInput& input) {
  if (input.text != text_ || input.mode != mode_) {
    text_ = input.text;
    mode_ = input.mode;
    parsed_ = parseFindPattern(text_, mode_);
  }
  if (!parsed_) {
    showNoSearch(parsed_.error(), input.now);
  } else {
    const bool caseSensitive = input.caseSensitive || mode_ == FindMode::Hex;
    // WHY: Submit a changed key before collecting: an outcome of the replaced key must be dropped, never shown.
    if (!wanted_ || !sameKey(caseSensitive, input.sources)) submit(caseSensitive, input.sources, input.now);
    collect(input.anchor);
  }
  if (kept_ && input.now - keptSince_ >= waitShownAfter_) kept_.reset();
}

void FindSearch::next() { stepCurrent(true); }

void FindSearch::prev() { stepCurrent(false); }

std::optional<FindMatch> FindSearch::takeReveal() { return std::exchange(reveal_, std::nullopt); }

bool FindSearch::sameKey(bool caseSensitive, std::span<const FindSource> sources) const {
  if (wanted_->caseSensitive != caseSensitive || wanted_->pattern != *parsed_) return false;
  std::size_t k = 0;
  for (const FindSource& source : sources) {
    const DecodedAnnotations* annotations = annotationsOf(source);
    if (annotations == nullptr) continue;
    if (k == wanted_->sources.size()) return false;
    const SourceKey& key = wanted_->sources[k++];
    if (key.decoder != source.decoder || key.serial != annotations->serial() || key.laneRank != source.laneRank ||
        !std::ranges::equal(key.rowRanks, source.rowRanks)) {
      return false;
    }
  }
  return k == wanted_->sources.size();
}

void FindSearch::keepShown(std::chrono::steady_clock::time_point now) {
  if (kept_) return;
  kept_ = Shown{state_, patternError_, std::move(matches_), current_};
  keptSince_ = now;
}

void FindSearch::submit(bool caseSensitive, std::span<const FindSource> sources,
                        std::chrono::steady_clock::time_point now) {
  keepShown(now);
  jobStop_.request_stop();
  jobStop_ = std::stop_source();
  if (!wanted_ || wanted_->caseSensitive != caseSensitive || wanted_->pattern != *parsed_) held_.reset();
  Key key{*parsed_, caseSensitive, {}};
  Job job{++generation_, *parsed_, caseSensitive, {}, jobStop_.get_token()};
  for (const FindSource& source : sources) {
    const DecodedAnnotations* annotations = annotationsOf(source);
    if (annotations == nullptr) continue;
    std::vector<std::uint32_t> rowRanks(source.rowRanks.begin(), source.rowRanks.end());
    key.sources.push_back({source.decoder, annotations->serial(), source.laneRank, rowRanks});
    job.sources.push_back(
      {source.decoder, source.laneRank, std::move(rowRanks), source.shown->annotations, byteRows(*source.shown)});
  }
  wanted_ = std::move(key);
  state_ = FindState::Searching;
  patternError_.reset();
  matches_ = {};
  current_.reset();
  reveal_.reset();
  {
    std::lock_guard lock(mutex_);
    job_ = std::move(job);
  }
  if (!worker_.joinable()) worker_ = std::jthread([this](std::stop_token stop) { work(std::move(stop)); });
  jobReady_.notify_one();
}

void FindSearch::showNoSearch(FindPatternError error, std::chrono::steady_clock::time_point now) {
  const bool halfTyped = error == FindPatternError::OddHexDigits;
  if (!halfTyped) {
    kept_.reset();
  } else if (state_ != FindState::PatternError || patternError_ != error) {
    keepShown(now);
  }
  if (wanted_) {
    jobStop_.request_stop();
    wanted_.reset();
  }
  state_ = error == FindPatternError::Empty ? FindState::NoText : FindState::PatternError;
  patternError_ = error == FindPatternError::Empty ? std::nullopt : std::optional(error);
  matches_ = {};
  current_.reset();
  held_.reset();
  reveal_.reset();
}

void FindSearch::collect(std::uint64_t anchor) {
  std::optional<Outcome> taken;
  {
    std::lock_guard lock(mutex_);
    taken = std::exchange(outcome_, std::nullopt);
  }
  if (!taken || taken->generation != generation_) return;
  kept_.reset();
  if (!taken->found) {
    state_ = taken->found.error();
    matches_ = {};
    current_.reset();
    held_.reset();
    return;
  }
  show(std::move(*taken->found), anchor);
}

void FindSearch::show(FindMatches found, std::uint64_t anchor) {
  matches_ = std::move(found);
  state_ = FindState::Ready;
  current_ = held_ ? sameOrNext(*held_) : matches_.firstFrom(anchor);
  if (!held_ && current_) reveal_ = matches_.at(*current_);
  held_ = current_ ? std::optional(matches_.at(*current_)) : std::nullopt;
}

std::optional<std::size_t> FindSearch::sameOrNext(const FindMatch& match) const {
  const std::optional<std::size_t> next = matches_.firstFrom(match.begin);
  if (!next) return std::nullopt;
  for (std::size_t i = *next; i < matches_.size() && matches_.at(i).begin == match.begin; ++i) {
    const FindMatch& candidate = matches_.at(i);
    if (candidate.end == match.end && candidate.decoder == match.decoder && candidate.row == match.row) return i;
  }
  return next;
}

void FindSearch::stepCurrent(bool forward) {
  if (!current_) return;
  current_ = matches_.step(*current_, forward);
  held_ = matches_.at(*current_);
  reveal_ = held_;
}

void FindSearch::work(std::stop_token threadStop) {
  while (true) {
    Job job;
    {
      std::unique_lock lock(mutex_);
      if (!jobReady_.wait(lock, threadStop, [this] { return job_.has_value(); })) return;
      job = std::move(*job_);
      job_.reset();
    }
    std::optional<Outcome> outcome = run(job);
    if (!outcome) continue;
    {
      std::lock_guard lock(mutex_);
      outcome_ = std::move(outcome);
    }
    // WHY: Wake outside the lock: a wake that waits for the UI thread would deadlock with update().
    if (resultPosted_) resultPosted_();
  }
}

std::optional<FindSearch::Outcome> FindSearch::run(const Job& job) const {
  try {
    std::vector<std::vector<ByteMatch>> found;
    found.reserve(job.sources.size());
    for (const JobSource& source : job.sources) {
      auto scanned = scan_(source.annotations->groups(), source.rows, job.pattern, job.caseSensitive, job.stop);
      if (!scanned) {
        if (scanned.error() == ByteSearchFailure::Stopped) return std::nullopt;
        return Outcome{job.generation, std::unexpected(FindState::OutOfMemory)};
      }
      found.push_back(std::move(*scanned));
    }
    std::vector<FindMatchSource> sources;
    sources.reserve(job.sources.size());
    for (std::size_t k = 0; k < job.sources.size(); ++k) {
      sources.push_back({job.sources[k].decoder, job.sources[k].laneRank, found[k], job.sources[k].rowRanks});
    }
    std::optional<FindMatches> built = FindMatches::build(sources, job.stop);
    if (!built) return std::nullopt;
    return Outcome{job.generation, std::move(*built)};
  } catch (const std::bad_alloc&) {
    return Outcome{job.generation, std::unexpected(FindState::OutOfMemory)};
  } catch (...) {
    return Outcome{job.generation, std::unexpected(FindState::Failed)};
  }
}

}

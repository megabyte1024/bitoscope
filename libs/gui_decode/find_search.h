/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <stop_token>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include "annotation_groups.h"
#include "annotation_store.h"
#include "byte_search.h"
#include "decoded_annotations.h"
#include "find_matches.h"
#include "find_pattern.h"

namespace bitoscope::gui_decode {

enum class FindState { NoText, PatternError, Searching, Ready, OutOfMemory, Failed };

inline constexpr std::chrono::milliseconds kFindWaitShownAfter{200};

struct FindSource {
  std::uint32_t decoder = 0;
  const ShownResult* shown = nullptr;
  std::uint32_t laneRank = 0;
  std::span<const std::uint32_t> rowRanks;
};

struct FindInput {
  std::string_view text;
  FindMode mode = FindMode::Ascii;
  bool caseSensitive = false;
  std::span<const FindSource> sources;
  std::uint64_t anchor = 0;
  std::chrono::steady_clock::time_point now{};
};

using FindScanStep = std::function<std::expected<std::vector<ByteMatch>, ByteSearchFailure>(
  const bitoscope::waveform_view::AnnotationGroups& groups, std::span<const ByteRow> rows,
  std::span<const std::uint8_t> pattern, bool caseSensitive, std::stop_token stop)>;

class FindSearch {
 public:
  explicit FindSearch(FindScanStep scan = findByteMatches, std::function<void()> resultPosted = {},
                      std::chrono::milliseconds waitShownAfter = kFindWaitShownAfter);
  ~FindSearch();
  FindSearch(const FindSearch&) = delete;
  FindSearch& operator=(const FindSearch&) = delete;

  void update(const FindInput& input);
  void next();
  void prev();

  FindState state() const { return kept_ ? kept_->state : state_; }
  std::optional<FindPatternError> patternError() const { return kept_ ? kept_->patternError : patternError_; }
  const FindMatches& matches() const { return kept_ ? kept_->matches : matches_; }
  std::optional<std::size_t> current() const { return kept_ ? kept_->current : current_; }
  std::optional<FindMatch> takeReveal();

 private:
  struct SourceKey {
    std::uint32_t decoder = 0;
    std::uint64_t serial = 0;
    std::uint32_t laneRank = 0;
    std::vector<std::uint32_t> rowRanks;
  };
  struct Key {
    std::vector<std::uint8_t> pattern;
    bool caseSensitive = true;
    std::vector<SourceKey> sources;
  };
  struct JobSource {
    std::uint32_t decoder = 0;
    std::uint32_t laneRank = 0;
    std::vector<std::uint32_t> rowRanks;
    std::shared_ptr<const DecodedAnnotations> annotations;
    std::vector<ByteRow> rows;
  };
  struct Job {
    std::uint64_t generation = 0;
    std::vector<std::uint8_t> pattern;
    bool caseSensitive = true;
    std::vector<JobSource> sources;
    std::stop_token stop;
  };
  struct Outcome {
    std::uint64_t generation = 0;
    std::expected<FindMatches, FindState> found;
  };
  struct Shown {
    FindState state = FindState::NoText;
    std::optional<FindPatternError> patternError;
    FindMatches matches;
    std::optional<std::size_t> current;
  };

  bool sameKey(bool caseSensitive, std::span<const FindSource> sources) const;
  void submit(bool caseSensitive, std::span<const FindSource> sources, std::chrono::steady_clock::time_point now);
  void keepShown(std::chrono::steady_clock::time_point now);
  void showNoSearch(FindPatternError error, std::chrono::steady_clock::time_point now);
  void collect(std::uint64_t anchor);
  void show(FindMatches found, std::uint64_t anchor);
  std::optional<std::size_t> sameOrNext(const FindMatch& match) const;
  void stepCurrent(bool forward);
  void work(std::stop_token threadStop);
  std::optional<Outcome> run(const Job& job) const;

  FindScanStep scan_;
  std::function<void()> resultPosted_;
  std::chrono::milliseconds waitShownAfter_;

  std::string text_;
  FindMode mode_ = FindMode::Ascii;
  std::expected<std::vector<std::uint8_t>, FindPatternError> parsed_;
  std::optional<Key> wanted_;
  std::uint64_t generation_ = 0;
  std::stop_source jobStop_;
  FindState state_ = FindState::NoText;
  std::optional<FindPatternError> patternError_;
  FindMatches matches_;
  std::optional<std::size_t> current_;
  std::optional<FindMatch> held_;
  std::optional<FindMatch> reveal_;
  std::optional<Shown> kept_;
  std::chrono::steady_clock::time_point keptSince_;

  std::mutex mutex_;
  std::condition_variable_any jobReady_;
  std::optional<Job> job_;
  std::optional<Outcome> outcome_;
  std::jthread worker_;
};

}

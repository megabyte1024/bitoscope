/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <cstdint>
#include <expected>
#include <format>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "capture_extent.h"
#include "decoder.h"
#include "raw_capture_buffer.h"
#include "read_samples.h"

namespace bitoscope::capture {

struct SavedChannel {
  uint32_t index = 0;
  std::string name;
};

struct SavedIntegerValue {
  int64_t value = 0;
  uint32_t digits = 0;
  std::string unit;
  bool operator==(const SavedIntegerValue&) const = default;
};

struct SavedEnumValue {
  std::optional<std::string> name;
  std::optional<int64_t> number;
  uint32_t digits = 0;
  std::string unit{};
  bool operator==(const SavedEnumValue&) const = default;
};

using SavedPropertyValue = std::variant<SavedIntegerValue, bool, SavedEnumValue>;

struct SavedProperty {
  std::string facet;
  std::string id;
  std::string name;
  std::string formatted;
  SavedPropertyValue value;
  bool operator==(const SavedProperty&) const = default;
};

struct SavedMarker {
  uint64_t id = 0;
  uint64_t sample = 0;
  std::string name;
  bool operator==(const SavedMarker&) const = default;
};

struct SavedCursorPair {
  uint64_t first = 0;
  uint64_t second = 0;
  bool shown = false;
  bool operator==(const SavedCursorPair&) const = default;
};

struct ViewState {
  std::vector<SavedMarker> markers;
  std::optional<SavedCursorPair> cursors;
  std::vector<std::string> channel_colors;
  std::optional<std::string> lane_order;
  std::vector<std::string> channel_names;
  bool operator==(const ViewState&) const = default;
};

enum class ShortReason { Memory, StoppedByUser, StoppedWithSession };

struct MemoryShortfall {
  uint64_t requested_samples = 0;
  uint64_t stored_samples = 0;
  std::optional<uint64_t> requested_post_trigger_samples;
  std::optional<uint64_t> stored_post_trigger_samples;
  ShortReason reason = ShortReason::Memory;
  std::optional<uint32_t> memory_full_device{};
};

struct DecoderVersions {
  std::string plugin_version;
  std::string decoder_version;
  bool operator==(const DecoderVersions&) const = default;
};

struct CaptureMetadata {
  std::vector<SavedChannel> channels;
  std::vector<std::pair<std::string, std::string>> decoder_setup;
  std::vector<DecoderVersions> decoder_versions;
  uint64_t capture_id = 0;
  std::optional<uint64_t> origin_sample_index;
  std::optional<uint64_t> trigger_sample_index;
  uint64_t requested_samples = 0;
  std::optional<uint64_t> requested_pre_trigger_samples;
  std::optional<uint64_t> requested_post_trigger_samples;
  std::optional<MemoryShortfall> memory_shortfall;
  CaptureExtent extent;
  std::string device;
  std::vector<SavedProperty> device_properties;
  std::optional<ViewState> view;
  std::vector<std::string> decoder_presentation;
};

class CaptureWriter {
 public:
  virtual ~CaptureWriter() = default;

  virtual std::expected<void, std::string> write(const RawCaptureBuffer& buffer,
                                                 const CaptureMetadata& metadata) = 0;
};

inline std::expected<void, std::string> checkSavable(const RawCaptureBuffer& buffer,
                                                     const CaptureMetadata& metadata) {
  if (!buffer.finalized()) {
    return std::unexpected("capture buffer is not finalized; saving starts after acquisition ends");
  }

  const CapturePlan& plan = buffer.plan();
  if (metadata.channels.size() != plan.channels()) {
    return std::unexpected(std::format("capture has {} channels, metadata names {} channel{}",
                                       plan.channels(), metadata.channels.size(),
                                       metadata.channels.size() == 1 ? "" : "s"));
  }
  for (size_t i = 0; i < metadata.channels.size(); ++i) {
    if (metadata.channels[i].name.empty()) {
      return std::unexpected(
          std::format("channel index {} has no name", metadata.channels[i].index));
    }
    for (size_t j = 0; j < i; ++j) {
      if (metadata.channels[j].index == metadata.channels[i].index) {
        return std::unexpected(
            std::format("channel index {} is named twice", metadata.channels[i].index));
      }
    }
  }

  const CaptureExtent& extent = metadata.extent;
  if (extent.count == 0) {
    return std::unexpected(
        "capture's extent was not set: CaptureMetadata::extent defaults to {0, 0}, and "
        "every capture must say which samples it holds");
  }
  if (extent.first_sample > (std::numeric_limits<uint64_t>::max)() - extent.count) {
    return std::unexpected(std::format(
        "capture's extent overflows: first_sample {} + count {} does not fit in 64 bits",
        extent.first_sample, extent.count));
  }
  const uint64_t extent_end = extent.first_sample + extent.count;
  const bool triggered = metadata.origin_sample_index.has_value();

  const uint64_t requested = metadata.requested_samples;
  if (requested == 0) {
    return std::unexpected(
        "capture's requested length was not set: CaptureMetadata::requested_samples defaults "
        "to 0, and every saved capture records what it asked for");
  }
  if (metadata.trigger_sample_index.has_value() != triggered) {
    return std::unexpected(
        "capture records only one of its origin and trigger sample indexes; triggered "
        "capture records both, untriggered one neither");
  }
  if (metadata.requested_pre_trigger_samples.has_value() != triggered ||
      metadata.requested_post_trigger_samples.has_value() != triggered) {
    return std::unexpected(std::format(
        "capture is {}, but its request {} requested pre- and post-trigger sample counts; "
        "triggered capture records both, untriggered one neither",
        triggered ? "triggered" : "untriggered", triggered ? "lacks" : "carries"));
  }
  const uint64_t pre = metadata.requested_pre_trigger_samples.value_or(0);
  const uint64_t post = metadata.requested_post_trigger_samples.value_or(0);
  if (triggered) {
    if (pre > (std::numeric_limits<uint64_t>::max)() - post || pre + post != requested) {
      return std::unexpected(std::format(
          "capture is recorded as asking for {} samples, but for {} before trigger and {} "
          "after it",
          requested, pre, post));
    }
    const uint64_t origin = *metadata.origin_sample_index;
    const uint64_t trigger = *metadata.trigger_sample_index;
    if (trigger < origin) {
      return std::unexpected(std::format(
          "trigger, at sample {}, is before capture's first sample, {}", trigger, origin));
    }
    if (trigger - origin != pre) {
      return std::unexpected(std::format(
          "trigger is at file sample {} (trigger_sample_index {} - origin_sample_index {}), but "
          "capture asked for {} samples before it",
          trigger - origin, trigger, origin, pre));
    }
  }

  if (buffer.sampleCount() > extent_end) {
    const std::span<const IndexRecord> records = buffer.indexRange(0, buffer.sampleCount());
    const bool inside_last_window =
        triggered && !records.empty() && records.back().first_sample_index < extent_end;
    if (!inside_last_window) {
      return std::unexpected(
          triggered ? std::format("buffer holds {} samples, but capture's extent ends at "
                                  "sample {}: nothing may follow triggered capture but rest "
                                  "of its last window, which begins at sample {}",
                                  buffer.sampleCount(), buffer.describeSample(extent_end),
                                  buffer.describeSample(records.empty() ? uint64_t{0}
                                                                        : records.back().first_sample_index))
                    : std::format("buffer holds {} samples, but capture's extent ends at "
                                  "sample {}: nothing may follow capture",
                                  buffer.sampleCount(), extent_end));
    }
  }

  if (!triggered && extent.first_sample != 0) {
    return std::unexpected(std::format(
        "capture's extent starts at sample {}, not 0, for untriggered capture",
        extent.first_sample));
  }
  if (triggered && extent.first_sample >= plan.samplesPerWindow()) {
    return std::unexpected(std::format(
        "capture's extent starts at sample {}, which is not less than {}-sample window "
        "triggered capture's front offset is always inside",
        buffer.describeSample(extent.first_sample), plan.samplesPerWindow()));
  }

  if (!triggered && requested != plan.totalSamples()) {
    return std::unexpected(std::format(
        "capture is recorded as having asked for {} samples, but its plan asked for {}",
        requested, plan.totalSamples()));
  }
  if (triggered && extent.first_sample > (std::numeric_limits<uint64_t>::max)() - requested) {
    return std::unexpected(std::format(
        "capture's extent overflows: first_sample {} + requested {} does not fit in 64 bits",
        extent.first_sample, requested));
  }
  if (triggered && extent.first_sample + requested > plan.totalSamples()) {
    return std::unexpected(std::format(
        "capture is recorded as having asked for {} samples from its front offset {}, "
        "which reaches past plan's {} samples",
        requested, extent.first_sample, plan.totalSamples()));
  }

  const uint64_t total = extent_end;
  if (const std::optional<MemoryShortfall>& s = metadata.memory_shortfall; s.has_value()) {
    if (s->requested_samples != requested) {
      return std::unexpected(std::format(
          "capture's shortfall is recorded as having asked for {} samples, but capture "
          "asked for {}",
          s->requested_samples, requested));
    }
    const bool stopped = s->reason != ShortReason::Memory;
    const bool with_session = s->reason == ShortReason::StoppedWithSession;
    if (with_session != s->memory_full_device.has_value()) {
      return std::unexpected(with_session
                                 ? std::string("capture is recorded as stopped with session, but names no "
                                               "device whose memory filled")
                                 : std::string("capture names device whose memory filled, but was not stopped "
                                               "with session"));
    }
    if (s->stored_samples == 0 || s->stored_samples > s->requested_samples ||
        (!stopped && s->stored_samples == s->requested_samples)) {
      return std::unexpected(std::format(
          "capture is recorded as holding {} of {} samples it asked for, which is not "
          "capture cut short by memory",
          s->stored_samples, s->requested_samples));
    }
    if (s->stored_samples != extent.count) {
      return std::unexpected(std::format(
          "capture is recorded as holding {} samples after running out of memory, but "
          "extent covers {}",
          s->stored_samples, extent.count));
    }
    if (s->requested_post_trigger_samples.has_value() !=
        s->stored_post_trigger_samples.has_value()) {
      return std::unexpected(
          "capture records only one of its requested and stored post-trigger sample counts; "
          "triggered capture records both, untriggered one neither");
    }
    if (triggered && !s->requested_post_trigger_samples.has_value()) {
      return std::unexpected(
          "capture is triggered and cut short by memory, but its shortfall does not record "
          "requested and stored post-trigger sample counts, which triggered short file must have");
    }
    if (!triggered && s->requested_post_trigger_samples.has_value()) {
      return std::unexpected(
          "capture is untriggered, but its shortfall records post-trigger sample counts; "
          "untriggered capture has no post-trigger range");
    }
    if (triggered) {
      const uint64_t requested_post = *s->requested_post_trigger_samples;
      const uint64_t stored_post = *s->stored_post_trigger_samples;
      if (requested_post != post) {
        return std::unexpected(std::format(
            "capture's shortfall is recorded as having asked for {} post-trigger samples, but "
            "capture asked for {}",
            requested_post, post));
      }
      if (stored_post > requested_post || (!stopped && stored_post == requested_post)) {
        return std::unexpected(std::format(
            "capture is recorded as holding {} of {} post-trigger samples it asked for, "
            "which is not post-trigger range cut short by memory",
            stored_post, requested_post));
      }
      if (extent.count < pre) {
        return std::unexpected(std::format(
            "capture holds {} samples, less than {} it asked for before trigger",
            extent.count, pre));
      }
      if (stored_post != extent.count - pre) {
        return std::unexpected(std::format(
            "capture is recorded as holding {} post-trigger samples, but its extent holds {} "
            "after trigger",
            stored_post, extent.count - pre));
      }
    }
  } else if (extent.count != requested) {
    return std::unexpected(
        extent.count < requested
            ? std::format("capture holds {} of {} samples it asked for, and records no "
                          "reason for being short: only capture whose memory ran out may be "
                          "saved short, and its metadata says so (memory_shortfall)",
                          extent.count, requested)
            : std::format("capture holds {} samples, more than {} it asked for",
                          extent.count, requested));
  }
  uint64_t expected_first = 0;
  for (const IndexRecord& r : buffer.indexRange(0, buffer.sampleCount())) {
    if (r.first_sample_index != expected_first) {
      return std::unexpected(std::format(
          "capture has hole at sample {}: {} samples are missing", expected_first,
          r.first_sample_index > expected_first ? r.first_sample_index - expected_first
                                                : expected_first - r.first_sample_index));
    }
    if (const uint64_t lost = r.gap_samples_before; lost != 0) {
      return std::unexpected(std::format(
          "window at sample {} reports {} samples lost before it", r.first_sample_index, lost));
    }
    expected_first = r.endSample();
  }
  if (expected_first < total) {
    return std::unexpected(std::format(
        "capture holds {} of {} samples; last {} are missing, from sample {}",
        expected_first, total, total - (std::min)(expected_first, total), expected_first));
  }

  if (!metadata.decoder_presentation.empty() &&
      metadata.decoder_presentation.size() != metadata.decoder_setup.size()) {
    return std::unexpected(std::format(
        "capture names {} decoder_setup entries but {} decoder presentations: one per entry, or none at all",
        metadata.decoder_setup.size(), metadata.decoder_presentation.size()));
  }
  if (!metadata.decoder_versions.empty() && metadata.decoder_versions.size() != metadata.decoder_setup.size()) {
    return std::unexpected(std::format(
        "capture names {} decoder_setup entries but {} decoder versions: one per entry, or none at all",
        metadata.decoder_setup.size(), metadata.decoder_versions.size()));
  }
  return {};
}

struct SavedCapture {
  uint64_t sample_rate_milli_hz = 0;
  uint64_t total_samples = 0;
  std::vector<SavedChannel> channels;
  std::vector<std::vector<uint8_t>> channel_bytes;
  std::vector<std::pair<std::string, std::string>> decoder_setup;
  std::vector<DecoderVersions> decoder_versions;
  uint64_t capture_id = 0;
  std::string device;
  std::vector<SavedProperty> device_properties;
  std::optional<uint64_t> origin_sample_index;
  std::optional<uint64_t> trigger_sample_index;
  uint64_t requested_samples = 0;
  std::optional<uint64_t> requested_pre_trigger_samples;
  std::optional<uint64_t> requested_post_trigger_samples;
  std::optional<MemoryShortfall> memory_shortfall;
};

class InMemoryCaptureWriter final : public CaptureWriter {
 public:
  std::expected<void, std::string> write(const RawCaptureBuffer& buffer,
                                         const CaptureMetadata& metadata) override {
    ++write_calls_;
    if (fail_next_) {
      std::string message = std::move(*fail_next_);
      fail_next_.reset();
      return std::unexpected(std::move(message));
    }
    if (auto ok = checkSavable(buffer, metadata); !ok) return std::unexpected(ok.error());

    const CapturePlan& plan = buffer.plan();
    SavedCapture out;
    out.sample_rate_milli_hz = plan.sampleRateMilliHz();
    out.total_samples = metadata.extent.count;
    out.channels = metadata.channels;
    out.decoder_setup = metadata.decoder_setup;
    out.decoder_versions = metadata.decoder_versions;
    out.capture_id = metadata.capture_id;
    out.device = metadata.device;
    out.device_properties = metadata.device_properties;
    out.origin_sample_index = metadata.origin_sample_index;
    out.trigger_sample_index = metadata.trigger_sample_index;
    out.requested_samples = metadata.requested_samples;
    out.requested_pre_trigger_samples = metadata.requested_pre_trigger_samples;
    out.requested_post_trigger_samples = metadata.requested_post_trigger_samples;
    out.memory_shortfall = metadata.memory_shortfall;
    const uint64_t stored_bytes = (out.total_samples + 7) / 8;
    try {
      out.channel_bytes.resize(plan.channels());
      for (uint32_t c = 0; c < plan.channels(); ++c) {
        out.channel_bytes[c].resize(static_cast<size_t>(stored_bytes));
        if (auto r = readSamples(buffer, c, metadata.extent.first_sample, metadata.extent.count,
                                 out.channel_bytes[c]);
            !r) {
          return std::unexpected(std::format("reading channel {}: {}", c, r.error()));
        }
      }
    } catch (const std::bad_alloc&) {
      return std::unexpected("out of memory copying capture");
    }
    saved_ = std::move(out);
    return {};
  }

  void failNext(std::string message) { fail_next_ = std::move(message); }
  const SavedCapture* saved() const { return saved_ ? &*saved_ : nullptr; }
  uint32_t writeCalls() const { return write_calls_; }

 private:
  std::optional<SavedCapture> saved_;
  std::optional<std::string> fail_next_;
  uint32_t write_calls_ = 0;
};

}

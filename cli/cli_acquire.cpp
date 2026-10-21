/*
 * SPDX-License-Identifier: MIT
 */

#include "cli_acquire.h"

#include <bit>
#include <format>
#include <string_view>
#include <utility>

#include "cli_output_check.h"
#include "cli_settings.h"
#include "cli_units.h"

namespace bitoscope::cli {

namespace acquire_detail {

std::string describeTriggeredPlan(uint32_t channels, uint64_t sample_rate_milli_hz, uint64_t pre, uint64_t post) {
  namespace pd = bitoscope::capture::plan_detail;
  const uint64_t total = pre + post;
  const uint64_t bytes_per_channel = total / 8 + (total % 8 != 0 ? 1 : 0);
  const uint64_t total_bytes = bytes_per_channel * channels;
  return std::format(
      "{} {}, {}, {} samples ({}): {} before trigger ({}), {} after it ({}), {} "
      "uncompressed",
      channels, channels == 1 ? "channel" : "channels", pd::formatRate(sample_rate_milli_hz),
      pd::withThousands(total), pd::formatDuration(total, sample_rate_milli_hz), pd::withThousands(pre),
      pd::formatDuration(pre, sample_rate_milli_hz), pd::withThousands(post),
      pd::formatDuration(post, sample_rate_milli_hz), pd::formatSize(total_bytes));
}

void report(std::ostream& out, const bitoscope::capture::CaptureSummary& summary,
            const bitoscope::capture::CapturePlan& plan, const bitoscope::capture::CaptureSession& session) {
  namespace pd = bitoscope::capture::plan_detail;
  out << "captured " << pd::withThousands(summary.stored_samples) << " of "
      << pd::withThousands(summary.requested_samples) << " samples ("
      << pd::formatDuration(summary.stored_samples, plan.sampleRateMilliHz()) << " of "
      << pd::formatDuration(summary.requested_samples, plan.sampleRateMilliHz()) << ")\n"
      << "capture id " << acquisition::formatCaptureId(summary.capture_id) << "\n";
  if (const auto origin = session.triggerOrigin(); origin.has_value()) {
    const auto index = session.triggerSampleIndex();
    out << "trigger fired at sample " << pd::withThousands(*index) << " (sample "
        << pd::withThousands(*index - *origin) << " of saved capture)\n";
  }
  if (summary.foreign_chunks != 0) {
    out << "note: " << summary.foreign_chunks
        << " chunk(s) of another capture were ignored (late chunk of earlier attempt or run)\n";
  }
  out << std::flush;
}

void reportShort(std::ostream& out, const bitoscope::capture::CaptureSummary& summary,
                 const bitoscope::capture::CapturePlan& plan, const bitoscope::capture::CaptureSession& session) {
  namespace pd = bitoscope::capture::plan_detail;
  out << acquisition::detail::describeEnd(bitoscope::capture::CaptureStatus::Full, 0, std::nullopt)
      << ": captured " << pd::withThousands(summary.stored_samples)
      << " of " << pd::withThousands(summary.requested_samples) << " samples ("
      << pd::formatDuration(summary.stored_samples, plan.sampleRateMilliHz()) << " of "
      << pd::formatDuration(summary.requested_samples, plan.sampleRateMilliHz())
      << "); what was captured is whole and is kept (--on-memory-full discard throws it away "
         "instead)\n";
  report(out, summary, plan, session);
}

}

namespace {

class CliAcquisitionEvents final : public acquisition::AcquisitionEvents {
 public:
  CliAcquisitionEvents(std::ostream& out, std::ostream* err, std::optional<uint32_t> status_columns,
                       uint64_t status_interval_ms, const bitoscope::host::pipe::PluginDevice& session)
      : out_(out),
        err_(err),
        status_columns_(status_columns),
        status_interval_ms_(status_interval_ms),
        session_(session) {}

  void planned([[maybe_unused]] uint32_t device, const bitoscope::capture::CapturePlan& plan,
               const std::optional<bitoscope::trigger::TriggerSpec>& trigger) override {
    // WHY: Print the file's own L split around the trigger: the plan total has an internal margin window nobody gets.
    out_ << (trigger.has_value() ? acquire_detail::describeTriggeredPlan(plan.channels(), plan.sampleRateMilliHz(),
                                                                         trigger->pre_trigger_samples,
                                                                         trigger->post_trigger_samples)
                                 : plan.describe())
         << std::endl;
  }
  void warning([[maybe_unused]] uint32_t device, const acquisition::AcquireWarning& warning) override {
    out_ << "warning: " << warning.text << std::endl;
  }
  void propertiesSet([[maybe_unused]] uint32_t device, std::string_view plugin_id,
                     const std::vector<ipc::props::PropertyValue>& values) override {
    out_ << "set on '" << plugin_id << "': " << describeAnalyzerSettings(values, session_) << std::endl;
  }
  void started([[maybe_unused]] uint32_t device, std::string_view plugin_id, [[maybe_unused]] uint64_t capture_id) override {
    out_ << "capture started on '" << plugin_id << "'" << std::endl;
    status_line_.emplace(err_, status_columns_, status_interval_ms_);
  }
  bool wantsProgress() const override { return status_line_.has_value() && status_line_->enabled(); }
  void progress([[maybe_unused]] uint32_t device, uint64_t now_ms, const acquisition::Progress& progress) override {
    if (status_line_ && status_line_->due(now_ms)) status_line_->draw(now_ms, formatStatusLine(progress));
  }
  void waitEnded([[maybe_unused]] uint32_t device) override {
    if (status_line_) status_line_->clear();
  }
  void ringPeak([[maybe_unused]] uint32_t device, uint64_t peak_slots_in_use, uint32_t slot_count) override {
    out_ << "ring: peak " << peak_slots_in_use << " of " << slot_count << " slots in use" << std::endl;
  }
  void stopped([[maybe_unused]] uint32_t device) override { out_ << "capture stopped" << std::endl; }
  void retrying([[maybe_unused]] uint32_t device, const acquisition::AcquireRetry& retry) override {
    if (err_ == nullptr) return;
    *err_ << "bitoscope_cli: start-up loss: " << retry.lost << "; capture id "
          << acquisition::formatCaptureId(retry.old_capture_id) << " -> "
          << acquisition::formatCaptureId(retry.new_capture_id) << "; retrying (attempt " << retry.attempt << " of "
          << retry.attempts_allowed << ")" << std::endl;
  }
  void completed([[maybe_unused]] uint32_t device, const bitoscope::capture::CaptureSummary& summary,
                 const bitoscope::capture::CapturePlan& plan,
                 const bitoscope::capture::CaptureSession& session,
                 const std::optional<bitoscope::capture::MemoryShortfall>& shortfall) override {
    if (shortfall.has_value()) {
      acquire_detail::reportShort(out_, summary, plan, session);
    } else {
      acquire_detail::report(out_, summary, plan, session);
    }
  }

 private:
  std::ostream& out_;
  std::ostream* err_;
  std::optional<uint32_t> status_columns_;
  uint64_t status_interval_ms_;
  const bitoscope::host::pipe::PluginDevice& session_;
  std::optional<StatusLine> status_line_;
};

}

std::expected<acquisition::AcquireResult, std::string> runAcquire(const CliOptions& opts,
                                                                  const bitoscope::host::pipe::PluginDevice& session,
                                                                  bitoscope::host::FacetTransport& transport,
                                                                  std::ostream& out, AcquireSeams& seams) {
  // WHY: Check output files before sending or allocating anything: a long capture that cannot be saved is worst.
  if (auto target = checkOutputTarget(opts); !target) return std::unexpected(target.error());

  auto la = acquisition::makeLaClient(session, transport, seams.min_la_version);
  if (!la) return std::unexpected(la.error());
  if (auto ceiling = checkRateCeiling(opts.sample_rate_milli_hz, opts.channel_mask, la->capabilities());
      !ceiling) {
    return std::unexpected(ceiling.error());
  }

  auto settings = readAnalyzerSettings(opts.configure, session);
  if (!settings) return std::unexpected(settings.error());

  acquisition::AcquireRequest request;
  request.channel_mask = opts.channel_mask;
  request.sample_rate_milli_hz = opts.sample_rate_milli_hz;
  request.total_samples = opts.total_samples;
  request.properties = std::move(*settings);
  request.memory_policy = opts.memory == MemoryStrategy::Whole ? bitoscope::capture::AllocationPolicy::Whole
                                                               : bitoscope::capture::AllocationPolicy::Chunked;
  request.memory_budget_bytes = opts.memory_budget_bytes;
  request.reserve_bytes = opts.reserve_bytes;
  request.pack_workers = opts.pack_workers;
  request.clearing_workers = opts.clearing_workers;
  request.ring_mib = opts.ring_mib;
  request.start_retries = opts.start_retries;
  request.keep_on_memory_full = opts.on_memory_full == OnMemoryFull::Keep;

  if (opts.trigger.has_value()) {
    if (opts.trigger->capture_matches.empty()) {
      return std::unexpected(
          "cannot arm trigger: no capture-relative channel matches - device-to-capture "
          "channel mapping was not run on this CliOptions (see TriggerOptions::capture_matches)");
    }
    const LengthRequest length_request{opts.total_samples, opts.time_ms};
    auto sizes = sizeTriggerRange(length_request, preTriggerRequest(*opts.trigger), opts.sample_rate_milli_hz,
                                  seams.samples_per_window);
    if (!sizes) return std::unexpected(sizes.error());
    if (sizes->note.has_value()) out << *sizes->note << std::endl;

    bitoscope::trigger::TriggerSpec spec;
    spec.matches = opts.trigger->capture_matches;
    spec.combine = opts.trigger->combine;
    spec.pre_trigger_samples = sizes->pre;
    spec.post_trigger_samples = sizes->post;
    spec.timeout_s = opts.trigger->timeout_s;
    request.trigger = std::move(spec);
  }

  std::optional<uint32_t> status_columns;
  if (!opts.quiet && seams.err != nullptr && seams.stderr_columns) status_columns = seams.stderr_columns();
  CliAcquisitionEvents events(out, seams.err, status_columns, seams.progress_interval_ms, session);
  return acquisition::acquire(request, session, transport, seams, events);
}

}

/*
 * SPDX-License-Identifier: MIT
 */

#include "cli_firmware.h"

#include <algorithm>
#include <filesystem>
#include <format>
#include <fstream>
#include <iterator>
#include <thread>
#include <tuple>
#include <variant>
#include <vector>

#include "capture_plan.h"
#include "cli_exit_codes.h"
#include "facet_access.h"
#include "firmware_update_pipe_client.h"

namespace bitoscope::cli {

namespace {

namespace fw = bitoscope::ipc::firmware;
namespace pd = bitoscope::capture::plan_detail;
using host::pipe::FirmwareConfirmations;
using host::pipe::FirmwareUpdateClient;

using namespace std::chrono_literals;

constexpr std::string_view kPrefix = "bitoscope_cli: ";

struct Outcome {
  std::optional<fw::Finished> finished;
  std::string error;
};

void sleepFor(const FirmwareSeams& seams, std::chrono::milliseconds duration) {
  if (seams.sleep) {
    seams.sleep(duration);
  } else {
    std::this_thread::sleep_for(duration);
  }
}

std::string percent(uint64_t sent, uint64_t total) {
  return std::to_string(total == 0 ? 0 : sent * 100 / total);
}

std::expected<std::vector<uint8_t>, std::string> readFile(const std::string& path) {
  std::ifstream in(path, std::ios::binary);
  if (!in) return std::unexpected(std::format("cannot read firmware file '{}'", path));
  std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  if (bytes.empty()) return std::unexpected(std::format("firmware file '{}' is empty", path));
  return bytes;
}

std::string hintFor(fw::Verdict verdict) {
  switch (verdict) {
    case fw::Verdict::modelUnknown:
      return "Name it with --model 16 or --model 32.";
    case fw::Verdict::unknownFile:
      return "To install it anyway, add --confirm-unknown-file.";
    case fw::Verdict::otherModel:
      return "To install it anyway, add --confirm-other-model.";
    case fw::Verdict::fits:
    case fw::Verdict::unreadable:
      break;
  }
  return {};
}

int failed(std::ostream& err, const std::string& message) {
  err << kPrefix << message << std::endl;
  return kExitFailed;
}

Outcome waitForFinish(FirmwareUpdateClient& client, FirmwareEvents& events, std::ostream& out,
                      const FirmwareSeams& seams) {
  std::chrono::milliseconds sinceReport{0};
  std::chrono::milliseconds sinceStatus{0};
  std::chrono::milliseconds confirmWaited{0};
  bool cancelSent = false;
  bool confirming = false;
  std::optional<std::tuple<fw::Phase, std::string, uint32_t>> shown;

  for (;;) {
    if (auto done = events.finished()) return Outcome{*done, {}};
    if (seams.driver_closed) {
      if (auto why = seams.driver_closed()) return Outcome{std::nullopt, "driver ended during update: " + *why};
    }

    if (const auto progress = events.progress()) {
      const auto key = std::make_tuple(progress->phase, progress->imageName, progress->attempt);
      if (!shown || *shown != key || sinceReport >= seams.report_every) {
        out << describeFirmwareProgress(*progress) << std::endl;
        shown = key;
        sinceReport = 0ms;
      }
    }

    if (seams.interrupted && seams.interrupted()) {
      if (confirming) {
        if (confirmWaited >= seams.confirm_window) {
          if (seams.clear_interrupt) seams.clear_interrupt();
          confirming = false;
          out << "update goes on" << std::endl;
        }
      } else if (!cancelSent) {
        const auto answer = client.cancel(false);
        if (!answer) {
          out << "update could not be stopped: " << answer.error() << std::endl;
          if (seams.clear_interrupt) seams.clear_interrupt();
        } else if (*answer == fw::CancelAnswer::accepted) {
          cancelSent = true;
          if (seams.clear_interrupt) seams.clear_interrupt();
          out << "stopping update at next safe point" << std::endl;
        } else if (*answer == fw::CancelAnswer::needsConfirmation) {
          confirming = true;
          confirmWaited = 0ms;
          out << "warning: update is inside FPGA image: analyzer cannot capture until update completes. "
                 "Press Ctrl+C again within 5 s to stop anyway"
              << std::endl;
        } else {
          if (seams.clear_interrupt) seams.clear_interrupt();
          out << "update is finishing: too late to stop it" << std::endl;
        }
      }
    }

    if (sinceStatus >= seams.status_every) {
      sinceStatus = 0ms;
      const auto status = client.status();
      if (status && !status->running && status->last && std::holds_alternative<fw::Finished>(*status->last)) {
        return Outcome{std::get<fw::Finished>(*status->last), {}};
      }
    }

    sleepFor(seams, seams.poll);
    sinceReport += seams.poll;
    sinceStatus += seams.poll;
    if (confirming) confirmWaited += seams.poll;
  }
}

}

void FirmwareEvents::onFacetEvent(const ipc::FacetEvent& event) {
  if (event.facet != fw::kFacetName) return;
  const auto decoded = fw::decodeEvent(event.payload);
  if (!decoded) return;
  std::lock_guard lock(mutex_);
  if (const auto* progress = std::get_if<fw::Progress>(&*decoded)) {
    progress_ = *progress;
  } else {
    finished_ = std::get<fw::Finished>(*decoded);
  }
}

std::optional<fw::Progress> FirmwareEvents::progress() const {
  std::lock_guard lock(mutex_);
  return progress_;
}

std::optional<fw::Finished> FirmwareEvents::finished() const {
  std::lock_guard lock(mutex_);
  return finished_;
}

std::string describeFirmwareProgress(const fw::Progress& progress) {
  const std::string attempt = std::format("attempt {} of {}", progress.attempt, progress.attempts);
  switch (progress.phase) {
    case fw::Phase::preparing:
      return std::format("update: preparing, {}", attempt);
    case fw::Phase::sending:
      return std::format("update: sending {} image: {}% ({} of {} bytes), {}{}", progress.imageName,
                         percent(progress.bytesSent, progress.bytesTotal), pd::withThousands(progress.bytesSent),
                         pd::withThousands(progress.bytesTotal), attempt,
                         progress.resends == 0 ? std::string()
                                               : std::format(", {} resend{}", progress.resends,
                                                             progress.resends == 1 ? "" : "s"));
    case fw::Phase::finishing:
      break;
  }
  return "update: finishing, analyzer restarts";
}

int runFlashFirmware(const CliOptions& opts, const host::pipe::DriverDevice& session, host::FacetTransport& transport,
                     FirmwareEvents& events, std::ostream& out, std::ostream& err, const FirmwareSeams& seams) {
  auto client = acquisition::makeFirmwareUpdateClient(session, transport);
  if (!client) return failed(err, client.error());
  auto file = readFile(opts.firmware_file);
  if (!file) return failed(err, file.error());

  const bool inBootloader = client->info().running == fw::Running::bootloader;
  std::string model;
  if (inBootloader) {
    if (!opts.firmware_model) {
      return failed(err, "analyzer is in its bootloader and its model is unknown there: name it with --model 16 or "
                         "--model 32");
    }
    model = "DL" + *opts.firmware_model;
  } else if (opts.firmware_model && "DL" + *opts.firmware_model != client->info().model) {
    return failed(err, std::format("--model {} differs from model of this analyzer ({}); --model is for analyzers "
                                   "in bootloader",
                                   *opts.firmware_model, client->info().model));
  }

  const std::string fileName = std::filesystem::path(opts.firmware_file).filename().string();
  out << std::format("uploading firmware file '{}' ({} bytes)", fileName, pd::withThousands(file->size())) << std::endl;
  const auto uploaded = client->upload(*file, fileName);
  if (!uploaded) return failed(err, uploaded.error());

  const FirmwareConfirmations confirmed{opts.confirm_unknown_file, opts.confirm_other_model};
  const auto checked = client->check(*uploaded, model, confirmed);
  if (!checked) return failed(err, checked.error());
  if (checked->verdict != fw::Verdict::fits) {
    const std::string hint = hintFor(checked->verdict);
    return failed(err, checked->message + (hint.empty() ? std::string() : " " + hint));
  }
  out << checked->message << std::endl;

  if (const auto started = client->start(*uploaded, model, confirmed); !started) return failed(err, started.error());
  out << "firmware update started" << std::endl;

  const Outcome result = waitForFinish(*client, events, out, seams);
  if (!result.finished) return failed(err, result.error);
  if (result.finished->outcome != fw::Outcome::done) return failed(err, result.finished->message);
  out << result.finished->message << std::endl;

  if (!seams.reopen || !seams.capture_check) return kExitComplete;
  out << std::format("update finished; waiting {} s for analyzer to start",
                     std::chrono::duration_cast<std::chrono::seconds>(seams.program_start_wait).count())
      << std::endl;
  sleepFor(seams, seams.program_start_wait);
  out << "checking that analyzer captures" << std::endl;
  const auto reopened = seams.reopen();
  if (!reopened) {
    return failed(err, "update finished but analyzer could not be opened again: " + reopened.error());
  }
  const auto checkedCapture = seams.capture_check(*reopened);
  if (!checkedCapture) {
    return failed(err, "update finished but capture check failed: " + checkedCapture.error() +
                           "; run update again to its end");
  }
  out << *checkedCapture << std::endl;
  return kExitComplete;
}

int runStartProgram(const host::pipe::DriverDevice& session, host::FacetTransport& transport, FirmwareEvents& events,
                    std::ostream& out, std::ostream& err, const FirmwareSeams& seams) {
  auto client = acquisition::makeFirmwareUpdateClient(session, transport);
  if (!client) return failed(err, client.error());
  if (const auto started = client->startProgram(); !started) return failed(err, started.error());
  const Outcome result = waitForFinish(*client, events, out, seams);
  if (!result.finished) return failed(err, result.error);
  if (result.finished->outcome != fw::Outcome::done) return failed(err, result.finished->message);
  out << result.finished->message << std::endl;
  return kExitComplete;
}

}

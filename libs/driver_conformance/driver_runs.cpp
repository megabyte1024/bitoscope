/*
 * SPDX-License-Identifier: MIT
 */

#include "driver_runs.h"

#include <algorithm>
#include <array>
#include <condition_variable>
#include <cstdint>
#include <iterator>
#include <memory>
#include <mutex>
#include <thread>
#include <utility>
#include <variant>

#include "ipc_frame.h"
#include "ipc_protocol_version.h"

namespace bitoscope::driver_conformance {

namespace {

using Clock = std::chrono::steady_clock;

constexpr std::size_t kMaxInterfaceBytes = 256;
constexpr uint64_t kHandshakeRequest = 1;

std::string millis(std::chrono::milliseconds wait) { return std::to_string(wait.count()) + " ms"; }

class RunTimer {
 public:
  enum class Phase { awaitingReply, ending };

  struct Outcome {
    bool replyTimedOut = false;
    bool killed = false;
  };

  RunTimer(host::PluginProcess& process, Phase phase, std::chrono::milliseconds wait,
           std::chrono::milliseconds exitWait)
      : process_(process), phase_(phase), wait_(wait), exitWait_(exitWait), deadline_(Clock::now() + wait) {
    thread_ = std::thread([this] { run(); });
  }

  ~RunTimer() { (void)finish(); }
  RunTimer(const RunTimer&) = delete;
  RunTimer& operator=(const RunTimer&) = delete;

  void restartWait() {
    {
      std::lock_guard lock(mutex_);
      if (phase_ == Phase::awaitingReply) deadline_ = Clock::now() + wait_;
    }
    cv_.notify_all();
  }

  void replied() {
    {
      std::lock_guard lock(mutex_);
      if (phase_ != Phase::awaitingReply) return;
      phase_ = Phase::ending;
      deadline_ = Clock::now() + exitWait_;
    }
    cv_.notify_all();
  }

  Outcome finish() {
    {
      std::lock_guard lock(mutex_);
      done_ = true;
    }
    cv_.notify_all();
    if (thread_.joinable()) thread_.join();
    std::lock_guard lock(mutex_);
    return outcome_;
  }

 private:
  void run() {
    std::unique_lock lock(mutex_);
    while (!done_) {
      if (Clock::now() < deadline_) {
        cv_.wait_until(lock, deadline_);
        continue;
      }
      if (phase_ == Phase::awaitingReply) {
        outcome_.replyTimedOut = true;
        phase_ = Phase::ending;
        deadline_ = Clock::now() + exitWait_;
        lock.unlock();
        process_.closeInput();
        lock.lock();
        continue;
      }
      outcome_.killed = true;
      lock.unlock();
      process_.kill();
      return;
    }
  }

  host::PluginProcess& process_;
  std::mutex mutex_;
  std::condition_variable cv_;
  Phase phase_;
  std::chrono::milliseconds wait_;
  std::chrono::milliseconds exitWait_;
  Clock::time_point deadline_;
  bool done_ = false;
  Outcome outcome_;
  std::thread thread_;
};

std::expected<std::unique_ptr<host::PluginProcess>, std::string> startDriver(const ConformanceOptions& options,
                                                                            std::vector<std::string> arguments,
                                                                            std::string who) {
  return host::PluginProcess::start(
      host::PluginProcessOptions{options.driver, std::move(arguments), std::move(who), options.onDriverStderr});
}

std::string trimmed(std::string text) {
  while (!text.empty() && (text.back() == '\n' || text.back() == '\r' || text.back() == ' ')) text.pop_back();
  return text;
}

}

std::vector<std::string> modeArguments(std::string_view mode, const host::DriverConnection& connection) {
  std::vector<std::string> arguments = host::watchArguments(connection);
  arguments.front() = std::string(mode);
  return arguments;
}

InterfaceRun runInterface(const ConformanceOptions& options) {
  InterfaceRun run;
  const std::string who = "--interface";
  auto started = startDriver(options, {who}, who);
  if (!started) {
    run.problems.push_back(who + " could not start: " + started.error());
    return run;
  }
  host::PluginProcess& process = **started;
  RunTimer timer(process, RunTimer::Phase::ending, options.timeout, options.exitWait);
  std::array<uint8_t, 64> buffer{};
  for (;;) {
    auto got = process.fromPlugin().read(buffer);
    if (!got) {
      run.problems.push_back(who + " output broke off: " + got.error());
      break;
    }
    if (*got == 0) break;
    run.output.append(reinterpret_cast<const char*>(buffer.data()), *got);
    if (run.output.size() > kMaxInterfaceBytes) {
      run.problems.push_back(who + " wrote more than " + std::to_string(kMaxInterfaceBytes) + " bytes");
      process.kill();
      break;
    }
  }
  run.exitCode = process.waitForExit(options.exitWait);
  const RunTimer::Outcome outcome = timer.finish();
  if (outcome.killed) {
    run.problems.push_back(who + " did not finish within " + millis(options.timeout) + "; it was ended");
  } else if (!run.exitCode) {
    run.problems.push_back(who + " wrote its answer but did not exit");
  }
  run.output = trimmed(std::move(run.output));
  return run;
}

ListRun runList(const ConformanceOptions& options, const std::string& label) {
  ListRun run;
  auto started = startDriver(options, modeArguments("--list", options.connection), label);
  if (!started) {
    run.problems.push_back(label + " could not start: " + started.error());
    return run;
  }
  host::PluginProcess& process = **started;
  RunTimer timer(process, RunTimer::Phase::ending, options.timeout, options.exitWait);
  ipc::FrameReader reader(process.fromPlugin());
  auto first = reader.next();
  if (!first) {
    run.problems.push_back(label + " output broke off: " + first.error().message);
  } else if (!first->has_value()) {
    run.problems.push_back(label + " wrote no device list");
  } else if (auto envelope = ipc::decodeEnvelope(**first); !envelope) {
    run.problems.push_back(label + " wrote message that does not decode: " + envelope.error().message);
  } else if (auto* list = std::get_if<ipc::DeviceList>(&envelope->body); list == nullptr) {
    run.problems.push_back(label + " wrote something other than device list");
  } else {
    run.list = std::move(*list);
  }
  if (first && first->has_value()) {
    auto second = reader.next();
    if (second && second->has_value()) run.problems.push_back(label + " wrote more than its one device list");
  }
  const std::optional<int> code = process.waitForExit(options.exitWait);
  const RunTimer::Outcome outcome = timer.finish();
  if (outcome.killed) {
    run.problems.push_back(label + " did not finish within " + millis(options.timeout) + "; it was ended");
  } else if (!code) {
    run.problems.push_back(label + " wrote its device list but did not exit");
  } else if (*code != 0) {
    run.problems.push_back(label + " exited with code " + std::to_string(*code));
  }
  return run;
}

DeviceRun runDevice(const ConformanceOptions& options, const std::string& deviceId) {
  DeviceRun run;
  const std::string who = "--device " + deviceId;
  std::vector<std::string> mode = modeArguments("--device", options.connection);
  std::vector<std::string> arguments;
  arguments.push_back(std::move(mode.front()));
  arguments.push_back(deviceId);
  std::move(mode.begin() + 1, mode.end(), std::back_inserter(arguments));
  auto started = startDriver(options, std::move(arguments), who);
  if (!started) {
    run.problems.push_back(who + " could not start: " + started.error());
    return run;
  }
  host::PluginProcess& process = **started;
  RunTimer timer(process, RunTimer::Phase::awaitingReply, options.timeout, options.exitWait);
  bool answered = false;
  const auto endHandshake = [&] {
    answered = true;
    timer.replied();
    process.closeInput();
  };

  const ipc::Handshake offer{ipc::kProtocolMajor, ipc::kProtocolMajor, ipc::kProtocolMinor, {}};
  auto bytes = ipc::encodeEnvelope(ipc::Envelope{kHandshakeRequest, offer});
  if (!bytes) {
    run.problems.push_back(who + ": handshake could not be encoded: " + bytes.error());
    endHandshake();
  } else if (auto wrote = ipc::FrameWriter(process.toPlugin()).write(*bytes); !wrote) {
    run.problems.push_back(who + ": handshake could not be sent: " + wrote.error().message);
    endHandshake();
  }

  ipc::FrameReader reader(process.fromPlugin());
  for (;;) {
    auto frame = reader.next();
    if (!frame) {
      run.problems.push_back(who + " output broke off: " + frame.error().message);
      break;
    }
    if (!frame->has_value()) break;
    auto envelope = ipc::decodeEnvelope(**frame);
    if (!envelope) {
      run.problems.push_back(who + " wrote message that does not decode: " + envelope.error().message);
      continue;
    }
    if (auto* progress = std::get_if<ipc::OpenProgress>(&envelope->body)) {
      if (run.reply) {
        run.progressAfterReply.push_back(*progress);
      } else {
        run.progressBeforeReply.push_back(*progress);
        timer.restartWait();
      }
    } else if (auto* reply = std::get_if<ipc::HandshakeReply>(&envelope->body)) {
      if (envelope->requestId != kHandshakeRequest) {
        run.problems.push_back(who + " answered request " + std::to_string(envelope->requestId) +
                               ", which host never sent");
      } else if (run.reply) {
        run.problems.push_back(who + " answered handshake twice");
      } else {
        run.reply = std::move(*reply);
        if (!answered) endHandshake();
      }
    } else if (auto* refusal = std::get_if<ipc::Refusal>(&envelope->body);
               refusal != nullptr && envelope->requestId == kHandshakeRequest && !run.reply) {
      run.problems.push_back(who + " refused handshake message: " + refusal->message);
      if (!answered) endHandshake();
    }
  }

  run.exitCode = process.waitForExit(options.exitWait);
  const RunTimer::Outcome outcome = timer.finish();
  if (outcome.replyTimedOut) {
    run.problems.push_back(who + " gave no handshake reply within " + millis(options.timeout));
  } else if (!run.reply && !answered) {
    run.problems.push_back(who + " ended its output without handshake reply");
  }
  if (outcome.killed) {
    run.exitProblem = who + " did not end within " + millis(options.exitWait) + " after its input closed; it was ended";
  } else if (!run.exitCode) {
    run.exitProblem = who + " did not end within " + millis(options.exitWait) + " after its output ended";
    process.kill();
  } else if (*run.exitCode != 0 && run.reply && run.reply->accepted) {
    run.exitProblem = who + " exited with code " + std::to_string(*run.exitCode) + " after its input closed";
  }
  return run;
}

}

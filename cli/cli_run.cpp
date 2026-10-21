/*
 * SPDX-License-Identifier: MIT
 */

#include "cli_run.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <expected>
#include <functional>
#include <memory>
#include <ostream>
#include <stop_token>
#include <string>
#include <syncstream>
#include <thread>
#include <utility>
#include <vector>

#include "capture_allocator.h"
#include "cli_acquire.h"
#include "cli_args.h"
#include "cli_commands.h"
#include "cli_decode.h"
#include "cli_decoder_plugins.h"
#include "cli_exit_codes.h"
#include "cli_output_check.h"
#include "cli_plugins.h"
#include "cli_save.h"
#include "hdf5_capture_writer.h"
#include "pipe_plugin_host.h"
#include "system_memory_probe.h"

namespace bitoscope::cli {

namespace {

using namespace std::chrono_literals;

void printLine(std::ostream& stream, std::string_view line) {
  std::osyncstream(stream) << line << '\n' << std::flush;
}

std::string pathText(const std::filesystem::path& path) {
  const std::u8string text = path.u8string();
  return std::string(reinterpret_cast<const char*>(text.data()), text.size());
}

int listPlugins(host::PipePluginHost& host, const CliOptions& opts, const std::filesystem::path& directory,
                const std::filesystem::path& decoder_directory, std::ostream& out, std::ostream& err) {
  // WHY: List decoder plugins before the no-driver refusal, so that refusal cannot swallow a decoder listing.
  if (opts.plugin.empty()) {
    const auto decoder_plugins = listDecoderPlugins(decoder_directory);
    if (!decoder_plugins.empty()) {
      printLine(out, "decoder plugins in " + pathText(decoder_directory) + ":");
      for (const auto& [name, info] : decoder_plugins) {
        std::osyncstream(out) << describeDecoderPlugin(name, info) << std::flush;
      }
    }
  }
  std::vector<std::string> drivers;
  if (!opts.plugin.empty()) {
    auto driver = selectDriver(host.drivers(), opts.plugin, directory);
    if (!driver) {
      printLine(err, "bitoscope_cli: " + driver.error());
      return kExitFailed;
    }
    drivers.push_back(*driver);
  } else {
    drivers = host.drivers();
    if (drivers.empty()) {
      printLine(err, "bitoscope_cli: no drivers in " + pathText(directory) +
                         " (give driver directory with --driver-dir)");
      return kExitFailed;
    }
    std::string names;
    for (const std::string& driver : drivers) names += (names.empty() ? "" : ", ") + driver;
    printLine(out, "drivers in " + pathText(directory) + ": " + names);
  }
  int code = kExitComplete;
  for (const std::string& driver : drivers) {
    auto list = host.listDevices(driver);
    if (!list) {
      printLine(err, "bitoscope_cli: driver \"" + driver + "\" could not list its devices: " + list.error());
      code = kExitFailed;
      continue;
    }
    std::osyncstream(out) << describeDevices(driver, *list) << std::flush;
  }
  return code;
}

int listProperties(host::PipePluginHost& host, const CliOptions& opts, const std::filesystem::path& directory,
                   std::ostream& out, std::ostream& err) {
  auto driver = selectDriver(host.drivers(), opts.plugin, directory);
  if (!driver) {
    printLine(err, "bitoscope_cli: " + driver.error());
    return kExitFailed;
  }
  auto list = host.listDevices(*driver);
  if (!list) {
    printLine(err, "bitoscope_cli: driver \"" + *driver + "\" could not list its devices: " + list.error());
    return kExitFailed;
  }
  auto device = selectDevice(*list, *driver, opts.device);
  if (!device) {
    printLine(err, "bitoscope_cli: " + device.error());
    return kExitFailed;
  }
  std::osyncstream(out) << describeDeviceProperties(*driver, *device) << std::flush;
  return kExitComplete;
}

constexpr uint64_t kWatchListAgainIntervalMs = 2000;

uint64_t steadyMs() {
  return static_cast<uint64_t>(
      std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch())
          .count());
}

// WHY: Self-ended watch ends whole run with exit 1: going on would look alive while devices go unnoticed.
int watchDevices(host::PipePluginHost& host, const CliOptions& opts, const std::filesystem::path& directory,
                 std::ostream& out, std::ostream& err, const std::function<bool()>& interrupted,
                 uint64_t listAgainIntervalMs) {
  std::vector<std::string> drivers;
  if (!opts.plugin.empty()) {
    auto driver = selectDriver(host.drivers(), opts.plugin, directory);
    if (!driver) {
      printLine(err, "bitoscope_cli: " + driver.error());
      return kExitFailed;
    }
    drivers.push_back(*driver);
  } else {
    drivers = host.drivers();
    if (drivers.empty()) {
      printLine(err, "bitoscope_cli: no drivers in " + pathText(directory) +
                         " (give driver directory with --driver-dir)");
      return kExitFailed;
    }
  }
  auto ended = std::make_shared<std::atomic<bool>>(false);
  std::vector<std::unique_ptr<host::DeviceWatcher>> watchers;
  for (const std::string& driver : drivers) {
    auto list = host.listDevices(driver);
    if (!list) {
      printLine(err, "bitoscope_cli: driver \"" + driver + "\" could not list its devices: " + list.error());
      continue;
    }
    if (!list->supportsWatch) {
      std::string ids;
      for (const ipc::DeviceInfo& d : list->devices) ids += (ids.empty() ? "" : ", ") + d.id;
      printLine(out, driver + ": does not watch for devices; found now: " + (ids.empty() ? std::string("none") : ids));
      continue;
    }
    host::DeviceWatcherEvents events;
    events.onDeviceAppeared = [&out, driver](const ipc::DeviceInfo& d) {
      printLine(out, describeListedDevice(driver, d));
    };
    events.onDeviceChanged = events.onDeviceAppeared;
    events.onDeviceLost = [&out, driver](std::string_view id, std::string_view reason) {
      printLine(out, driver + "/" + std::string(id) + " lost: " + std::string(reason));
    };
    events.onLog = [&out, &err, driver](const ipc::LogEvent& e) {
      printLine(isErrorLog(e) ? err : out, formatLogLine(driver, e));
    };
    events.onEnded = [&err, ended](const host::WatchEnd& end) {
      printLine(err, "bitoscope_cli: " + end.message);
      ended->store(true);
    };
    auto watcher = host.watch(driver, std::move(events));
    if (!watcher) {
      printLine(err, "bitoscope_cli: " + watcher.error());
      continue;
    }
    watchers.push_back(std::move(*watcher));
  }
  if (watchers.empty()) {
    printLine(err, "bitoscope_cli: no driver can watch for devices");
    return kExitFailed;
  }
  printLine(out, opts.duration.count() > 0
                     ? "watching for devices for " + std::to_string(opts.duration.count()) + " s (Ctrl+C to stop)"
                     : std::string("watching for devices (Ctrl+C to stop)"));
  const uint64_t until = opts.duration.count() > 0 ? steadyMs() + static_cast<uint64_t>(opts.duration.count()) * 1000
                                                   : UINT64_MAX;
  uint64_t listAgainAt = steadyMs() + listAgainIntervalMs;
  while (!interrupted() && steadyMs() < until && !ended->load()) {
    if (steadyMs() >= listAgainAt) {
      for (auto& watcher : watchers) (void)watcher->listAgain();
      listAgainAt = steadyMs() + listAgainIntervalMs;
    }
    std::this_thread::sleep_for(50ms);
  }
  for (auto& watcher : watchers) watcher->stop();
  printLine(out, "stopped watching");
  return ended->load() ? kExitFailed : kExitComplete;
}

}

int runCli(std::span<const std::string_view> args, const CliEnvironment& environment) {
  std::ostream& raw_out = *environment.out;
  std::ostream& raw_err = *environment.err;
  // WHY: Declared first so destroyed last: driver shutdown lines must come after CLI's own last lines.
  CaptureEvents events;
  std::optional<host::PipePluginHost> plugins;
  std::osyncstream out(raw_out);
  std::osyncstream err(raw_err);
  out.rdbuf()->set_emit_on_sync(true);
  err.rdbuf()->set_emit_on_sync(true);
  const auto interrupted = [&environment] { return environment.interrupted && environment.interrupted(); };

  const auto parsed = parseArgs(args);
  if (!parsed.has_value()) {
    err << "bitoscope_cli: " << parsed.error() << "\n\n" << usageText() << std::flush;
    return kExitUsage;
  }
  const CliOptions& opts = *parsed;

  if (opts.action == Action::Help) {
    out << usageText() << std::flush;
    return kExitComplete;
  }

  if (opts.action == Action::Acquire) {
    if (const auto target = checkOutputTarget(opts); !target.has_value()) {
      err << "bitoscope_cli: " << target.error() << std::endl;
      return kExitFailed;
    }
  }

  const uint32_t samples_per_window = windowSizeForCapture(opts);

  const std::filesystem::path decoder_directory = decoderDirectory(opts.decoder_dir, environment.executable);

  std::vector<ConfiguredDecoder> decoders;
  // WHY: Allocated before any PluginDecoder exists: its cancel pointer is fixed at construction.
  std::atomic<bool> decode_cancel{false};
  if (opts.action == Action::Acquire && !opts.decoders.empty()) {
    const bool default_set = opts.decoder_plugin.empty();
    const std::vector<std::string> plugin_names =
        default_set ? defaultDecoderPlugins() : std::vector<std::string>{opts.decoder_plugin};
    auto executables = selectDecoderPluginExecutables(decoder_directory, plugin_names);
    if (!executables.has_value()) {
      err << "bitoscope_cli: " << executables.error() << std::endl;
      return kExitFailed;
    }
    decoder_host::ClientOptions client_options;
    client_options.on_log = [&raw_err](std::string_view line) { printLine(raw_err, line); };
    // WHY: Ask driver started WITHOUT script dir first: one that runs no scripts refuses --script-dir as unknown.
    if (opts.decoder_script_dir.has_value() && !default_set) {
      auto probe = startDecoderPlugin(executables->front(), std::nullopt, client_options);
      if (!probe.has_value()) {
        err << "bitoscope_cli: " << probe.error() << std::endl;
        return kExitFailed;
      }
      if (auto accepted = scriptDirAccepted(opts.decoder_plugin, (*probe)->info()); !accepted.has_value()) {
        err << "bitoscope_cli: " << accepted.error() << std::endl;
        return kExitUsage;
      }
    }
    std::vector<std::shared_ptr<decoder_host::DecoderPluginClient>> clients;
    if (opts.decoder_script_dir.has_value() && default_set) {
      auto started = startDecoderPluginsGivingScriptDir(*executables, *opts.decoder_script_dir, client_options);
      if (!started.has_value()) {
        err << "bitoscope_cli: " << started.error() << std::endl;
        return kExitFailed;
      }
      if (started->took_script_dir == 0) {
        err << "bitoscope_cli: " << defaultSetScriptDirRefusal(plugin_names) << std::endl;
        return kExitUsage;
      }
      clients = std::move(started->clients);
    } else {
      auto started = startDecoderPlugins(*executables, opts.decoder_script_dir, client_options);
      if (!started.has_value()) {
        err << "bitoscope_cli: " << started.error() << std::endl;
        return kExitFailed;
      }
      clients = std::move(*started);
    }
    decoder_host::PluginDecoderOptions plugin_options;
    plugin_options.job.cancel = &decode_cancel;
    plugin_options.before_feed = environment.before_decode_feed_for_test;
    if (environment.decode_ring_limit_for_test) {
      plugin_options.job.max_ring_bytes_per_channel = *environment.decode_ring_limit_for_test;
    }
    auto registry = decoderRegistryFor(clients, plugin_options);
    if (!registry.has_value()) {
      err << "bitoscope_cli: " << registry.error() << std::endl;
      return kExitFailed;
    }
    auto validated = validateDecoders(opts, *registry, samples_per_window);
    if (!validated.has_value()) {
      err << "bitoscope_cli: " << validated.error() << std::endl;
      return kExitFailed;
    }
    if (auto classes = resolveAnnotationFilter(opts.annotation_classes, *validated); !classes.has_value()) {
      err << "bitoscope_cli: " << classes.error() << std::endl;
      return kExitFailed;
    }
    for (const std::string& warning : decoderWindowWarnings(*validated)) {
      err << "bitoscope_cli: warning: " << warning << std::endl;
    }
    decoders = std::move(*validated);
  }

  const std::filesystem::path directory = pluginDirectory(opts.plugin_dir, environment.executable);
  host::PipePluginHostOptions host_options;
  host_options.pluginDirectory = directory;
  host_options.startTimeout = std::chrono::duration_cast<std::chrono::milliseconds>(opts.wait);
  host_options.onStderrLine = [&raw_err](std::string_view line) { printLine(raw_err, line); };
  host_options.onLog = [&raw_out, &raw_err](std::string_view plugin_id, const ipc::LogEvent& event) {
    printLine(isErrorLog(event) ? raw_err : raw_out, formatLogLine(plugin_id, event));
  };
  host_options.onOpenProgress = [&raw_out](std::string_view plugin_id, const ipc::OpenProgress& progress) {
    printLine(raw_out, describeOpenProgress(plugin_id, progress));
  };
  host_options.onFacetEvent = [&events](std::string_view, const ipc::FacetEvent& event) {
    events.onFacetEvent(event);
  };
  host::PipePluginHost& plugin_host = plugins.emplace(std::move(host_options));

  if (opts.action == Action::Listen || opts.action == Action::ListDevices) {
    return listPlugins(plugin_host, opts, directory, decoder_directory, raw_out, raw_err);
  }
  if (opts.action == Action::WatchDevices) {
    return watchDevices(plugin_host, opts, directory, raw_out, raw_err, interrupted,
                        environment.list_again_interval_ms_for_test.value_or(kWatchListAgainIntervalMs));
  }
  if (opts.action == Action::ListProperties) {
    return listProperties(plugin_host, opts, directory, raw_out, raw_err);
  }

  auto device = openDevice(plugin_host, opts.plugin, opts.device, directory);
  if (!device) {
    err << "bitoscope_cli: " << device.error() << std::endl;
    return kExitFailed;
  }
  const std::shared_ptr<host::PluginConnection> connection = plugin_host.connection(device->plugin_id);
  const auto plugin_closed = [connection]() -> std::optional<std::string> {
    return connection ? connection->closedReason() : std::optional<std::string>("driver is not open");
  };

  if (opts.action == Action::Acquire) {
    bitoscope::capture::SystemMemoryProbe system_probe;
    bitoscope::capture::SystemBlockSource blocks;
    bitoscope::capture::MemoryProbe& probe =
        environment.probe_for_test != nullptr ? *environment.probe_for_test : system_probe;
    AcquireSeams seams{probe, blocks};
    seams.samples_per_window = samples_per_window;
    seams.create_ring = acquisition::createHostRing;
    seams.now_ms = steadyMs;
    seams.sleep_ms = [](uint64_t ms) { std::this_thread::sleep_for(std::chrono::milliseconds(ms)); };
    seams.abandon_requested = interrupted;
    seams.err = &err;
    seams.stderr_columns = environment.stderr_columns;
    seams.plugin_closed = plugin_closed;
    seams.device_lost = [connection]() -> std::optional<std::string> {
      return connection ? connection->deviceLostReason() : std::nullopt;
    };
    seams.capture_ended = [&events](uint64_t capture_id) { return events.ended(capture_id); };
    if (environment.inactivity_timeout_ms_for_test) {
      seams.inactivity_timeout_ms = *environment.inactivity_timeout_ms_for_test;
    }
    seams.ring_geometry_for_test = environment.ring_geometry_for_test;

    const auto acquired = runAcquire(opts, *device, plugin_host, out, seams);
    if (!acquired.has_value()) {
      err << "bitoscope_cli: " << acquired.error() << std::endl;
      return kExitFailed;
    }
    if (environment.on_result_for_test) environment.on_result_for_test(*acquired);
    std::unique_ptr<bitoscope::hdf5::Hdf5CaptureWriter> hdf5;
    bitoscope::capture::CaptureWriter* writer = nullptr;
    if (!opts.output_file.empty()) {
      if (environment.writer_for_test != nullptr) {
        writer = environment.writer_for_test;
      } else {
        bitoscope::hdf5::Hdf5WriterOptions writer_options;
        writer_options.gzip_level = opts.gzip_level.value_or(writer_options.gzip_level);
        hdf5 = std::make_unique<bitoscope::hdf5::Hdf5CaptureWriter>(opts.output_file, writer_options);
        writer = hdf5.get();
      }
    }
    std::expected<void, std::string> saved;
    {
      // WHY: jthread: destructor requests stop before joining, so watcher always ends whatever finishAcquisition does.
      std::jthread cancel_watcher([&](std::stop_token stop) {
        while (!stop.stop_requested()) {
          if (interrupted()) {
            decode_cancel.store(true, std::memory_order_relaxed);
            return;
          }
          std::this_thread::sleep_for(50ms);
        }
      });
      saved = finishAcquisition(opts, *acquired, *device, writer, out, decoders, &decode_cancel,
                                environment.before_annotation_collect_for_test);
    }
    if (!saved.has_value()) {
      err << "bitoscope_cli: " << saved.error() << std::endl;
      return kExitFailed;
    }
    return exitCodeFor(*acquired);
  }

  const auto hold = [&] {
    const auto deadline = opts.duration.count() > 0 ? std::chrono::steady_clock::now() + opts.duration
                                                    : std::chrono::steady_clock::time_point::max();
    while (!interrupted() && std::chrono::steady_clock::now() < deadline) {
      if (const auto ended = events.latest()) {
        printLine(raw_out, "capture ended by itself: " + describeCaptureEnd(*ended));
        return;
      }
      if (plugin_closed()) return;
      std::this_thread::sleep_for(50ms);
    }
  };
  StartCaptureSeams capture_seams;
  capture_seams.create_ring = acquisition::createHostRing;
  capture_seams.new_capture_id = acquisition::randomCaptureId;
  capture_seams.ring_geometry_for_test = environment.ring_geometry_for_test;
  capture_seams.plugin_closed = plugin_closed;
  const auto result = runAction(opts, *device, plugin_host, out, hold, capture_seams);
  if (!result.has_value()) {
    err << "bitoscope_cli: " << result.error() << std::endl;
    return kExitFailed;
  }
  return kExitComplete;
}

}

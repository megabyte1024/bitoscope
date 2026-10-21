/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <algorithm>
#include <bit>
#include <array>
#include <charconv>
#include <chrono>
#include <cstdint>
#include <expected>
#include <limits>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <vector>

#include "cli_units.h"
#include "ring_config.h"
#include "trigger_spec.h"

namespace bitoscope::cli {

enum class Action {
  Listen,
  Help,
  StartCapture,
  Stop,
  PwmStart,
  PwmStop,
  Acquire,
  ListDevices,
  WatchDevices,
  ListProperties
};

enum class MemoryStrategy { Chunked, Whole };

enum class OnMemoryFull { Keep, Discard };

// WHY: device_matches and capture_matches differ in type on purpose: device numbers must not act as capture indices.
struct TriggerOptions {
  std::vector<DeviceChannelMatch> device_matches;
  std::vector<bitoscope::trigger::ChannelMatch> capture_matches;
  bitoscope::trigger::Combine combine = bitoscope::trigger::Combine::kAnd;
  uint32_t capture_ratio_percent = 0;
  uint64_t timeout_s = 0;
  std::optional<uint64_t> pre_samples;
  std::optional<uint64_t> pre_time_ms;
};

inline PreTriggerRequest preTriggerRequest(const TriggerOptions& trigger) {
  PreTriggerRequest pre;
  if (trigger.pre_samples.has_value()) {
    pre.kind = PreTriggerKind::kPreSamples;
    pre.pre_samples = *trigger.pre_samples;
  } else if (trigger.pre_time_ms.has_value()) {
    pre.kind = PreTriggerKind::kPreTime;
    pre.pre_time_ms = *trigger.pre_time_ms;
  } else {
    pre.kind = PreTriggerKind::kRatio;
    pre.ratio_percent = trigger.capture_ratio_percent;
  }
  return pre;
}

struct CliOptions {
  Action action = Action::Listen;
  std::string plugin;
  std::string device;
  std::string plugin_dir;
  std::chrono::seconds wait{30};

  uint64_t rate_hz = 0;
  uint32_t channel_mask = 0;
  uint64_t sample_limit = 0;
  bool buffered = false;
  std::chrono::seconds duration{0};
  std::map<std::string, std::string> configure;

  uint64_t sample_rate_milli_hz = 0;
  uint64_t total_samples = 0;
  std::optional<uint64_t> time_ms;
  std::vector<Channel> channels;
  std::string output_file;
  std::string output_format = "hdf5";
  std::optional<uint32_t> gzip_level;
  std::vector<std::string> decoders;
  std::vector<std::string> annotation_classes;
  std::string decoder_plugin;
  std::string decoder_dir;
  std::optional<std::string> decoder_script_dir;
  MemoryStrategy memory = MemoryStrategy::Chunked;
  std::optional<uint64_t> reserve_bytes;
  std::optional<uint64_t> memory_budget_bytes;
  uint32_t start_retries = 2;
  OnMemoryFull on_memory_full = OnMemoryFull::Keep;
  std::optional<TriggerOptions> trigger;
  uint64_t ring_mib = acquisition::kDefaultRingMiB;
  uint32_t pack_workers = 0;
  uint32_t clearing_workers = 1;
  bool quiet = false;

  uint32_t port_mask = 0;
  uint64_t freq_hz = 0;
  uint32_t duty_percent = 50;
};

inline uint32_t windowSizeForCapture(const CliOptions& opts) {
  return windowSizeForCapture(opts.trigger.has_value(), opts.sample_rate_milli_hz);
}

inline constexpr std::string_view kUsageText =
    "Usage:\n"
    "  bitoscope_cli                   List drivers in driver directory and devices and\n"
    "                                  facets each one finds (--list-devices: one driver's).\n"
    "  bitoscope_cli ACTION [options]  Start driver, drive one of its devices, then exit.\n"
    "\n"
    "Driver: executable in driver directory (default: drivers/ next to bitoscope_cli),\n"
    "started by bitoscope_cli for one device and ended when bitoscope_cli exits.\n"
    "\n"
    "Actions:\n"
    "  --list-devices    List devices one driver finds, each with its id and facets; with no\n"
    "                    --driver, every driver's. Device that cannot be used now shows\n"
    "                    \"unavailable: <reason>\"; name that is not device's own is marked\n"
    "                    \"(generic name)\". Opens no device.\n"
    "  --list-properties Show one device's property groups and properties: displayed name, id,\n"
    "                    type, unit, digits, limits and default, whether each can change during\n"
    "                    capture, enumeration choices and its help text; for logic analyzers\n"
    "                    also their channel total, their sample rates, rate ceiling for each\n"
    "                    number of channels, and their channel presets. --driver and --device pick\n"
    "                    device, as for commands; with several found and none named,\n"
    "                    bitoscope_cli refuses and lists them. Opens no device.\n"
    "  --watch-devices   Report devices as they are plugged in and unplugged: devices found\n"
    "                    first, then one line for each that appears, changes (name, serial\n"
    "                    number, availability) or is lost, until Ctrl+C. While watching, it\n"
    "                    asks drivers to look again every 2 s.\n"
    "                    With no --driver, every driver that can watch (one that cannot is named,\n"
    "                    with its devices now). Opens no device. When one driver's watch ends by\n"
    "                    itself, every watch stops: one line says which and why, exit code 1.\n"
    "      --duration-s S    Watch for S seconds (default: until Ctrl+C)\n"
    "  --start-capture   Start logic-analyzer capture and print live summary. Runs until\n"
    "                    --duration-s elapses, Ctrl+C, or capture ends by itself (--limit),\n"
    "                    then stops capture.\n"
    "      --rate-hz N       Sample rate in whole hertz (required)\n"
    "      --channel-mask MASK  Channel bitmask, decimal or 0x hex (required)\n"
    "      --limit N         Driver stops after N samples; 0 = until stopped (default 0)\n"
    "      --buffered        Capture on device, publish at end (default: streaming)\n"
    "      --duration-s S    Capture for S seconds (default: until Ctrl+C)\n"
    "      --configure K=V   Device setting: K is id of one of device's properties, V\n"
    "                        value in that property's unit, with or without prefix, e.g.\n"
    "                        threshold=1.6, threshold=1.6V or threshold=1600mV; repeatable.\n"
    "                        Checked before device is touched: unknown id, value\n"
    "                        outside property's limits or finer than its digits is\n"
    "                        refused (exit code 1). All of them are set in one request before\n"
    "                        capture starts\n"
    "      --ring-mib N      As for --samples/--time below (default 256)\n"
    "  --samples N | --time T   Acquire bounded capture into memory (no action\n"
    "                    flag). -o saves capture as HDF5 after it has finished; -P decodes\n"
    // WHY: Written as UTF-8 bytes: the source may be compiled with any execution character set.
    "                    it (UART, SPI, I\xC2\xB2"
    "C) once it has.\n"
    "      --samples N       Length in samples; bare number, or k, m, g (x1000 each)\n"
    "      --time T          Length as time; bare number is ms, or 2s / 500ms. Rounded up\n"
    "                        to whole samples. Give --samples OR --time, not both\n"
    "      -c, --config K=V[:K=V...]  samplerate=RATE (required; 1000000, 1m, 200kHz) and any\n"
    "                        device settings, as for --configure above: property's id and\n"
    "                        value in its unit, e.g. -c samplerate=1m:threshold=1600mV. Checked\n"
    "                        before device is touched\n"
    "      -C, --channels LIST   Channels, e.g. 0-3 or 1=CLK,2-4,7 (required; 0-31)\n"
    "      -o, --output-file F   Save capture to F (HDF5). Refuses existing file, or\n"
    "                        missing directory, before device is touched; nothing is overwritten\n"
    "      -O, --output-format hdf5[:gzip=N]   Output format; gzip level 0-9\n"
    "      -P, --protocol-decoders SPEC   e.g. uart:rx=D1:baudrate=115200; repeatable.\n"
    "                        Each channel is -C name or number. It is checked before anything\n"
    "                        starts (its decoder plugin is started, its listed decoders read,\n"
    "                        its own CHECK run against capture); every annotation is\n"
    "                        printed. File keeps setup only, never decoded results: decoders\n"
    "                        run again on open. Decoders stack: give one id= and bind\n"
    "                        input of upper decoder to its output, e.g. -P uart:id=link:rx=D1\n"
    "                        -P cobs:in=link.rx (each -P's lines start with its id). Decoder may\n"
    "                        be designed for certain lower decoders only; listing names them\n"
    "                        (\"designed for SPI, QSPI\") and binding to any other output is\n"
    "                        refused before device is touched\n"
    "      -A, --annotation-classes ID:CLASS[,CLASS...]   Print only these annotation classes of\n"
    "                        decoder with this id (-P's id=, else its name); repeatable, e.g.\n"
    "                        -A uart:start,framing_error. Class is its name in lower case with\n"
    "                        underscores, its role (\"data\", \"bit\") for one with no name, or\n"
    "                        its number. Class decoder does not declare\n"
    "                        is refused before device is touched, and\n"
    "                        message lists what it does declare. Decoder with no -A prints all.\n"
    "                        Requires -P\n"
    "      --decoder-plugin NAME   Decoder plugin every -P of this run is decoded through;\n"
    "                        executable in decoder directory. Default: decoders_lowlevel and\n"
    "                        decoders_luajit together, so decoders chain across them\n"
    "                        Requires -P (usage error without one: there is no decoder to run)\n"
    "      --decoder-dir DIR   Where decoder plugins are (default: decoders/ next to\n"
    "                        bitoscope_cli, apart from drivers' directory)\n"
    "      --decoder-script-dir DIR   Extra *.lua decoder scripts for scripting decoder plugin\n"
    "                        (decoders_luajit), loaded next to its built-in ones. With default set\n"
    "                        it goes to decoders_luajit, which runs scripts; refused with driver\n"
    "                        that runs no scripts.\n"
    "                        Requires -P (usage error without one: there is no decoder to run)\n"
    "                    Exit code 0: whole requested length arrived with no hole. Exit code 3:\n"
    "                    memory reserved for capture filled first and what was acquired was\n"
    "                    KEPT - whole, contiguous run from sample 0, shorter than asked for, and\n"
    "                    saved file says so (see --on-memory-full). Exit code 1: anything else\n"
    "                    (hole, Ctrl+C, stall, --trigger that never fires) and nothing is\n"
    "                    saved. Ctrl+C always discards capture, whatever --on-memory-full says.\n"
    "                    Requests larger than uncompressed memory budget warn and start\n"
    "                    anyway; warning carries floor, not estimate.\n"
    "      --on-memory-full keep|discard   What to do with capture whose memory fills before\n"
    "                        reaching requested length (default keep). keep saves what was acquired,\n"
    "                        labels file with requested and stored counts, and exits 3;\n"
    "                        discard saves nothing and exits 1. It applies to memory exhaustion\n"
    "                        alone - hole, Ctrl+C, stall or rejected chunk still discard\n"
    "                        everything whatever it says\n"
    "      --memory whole|chunked   How capture buffer is obtained (default chunked)\n"
    "      --reserve SIZE    RAM to leave free, e.g. 2GiB; 0 = none\n"
    "      --memory-budget SIZE   Most RAM capture may take, e.g. 512MiB\n"
    "      --ring-mib N      Size in MiB of shared-memory ring that drivers with one write\n"
    "                        capture into (default 256): slots of 4 MiB, so multiple of 4\n"
    "                        and at least 8. It is memory too: budget holds it back\n"
    "      --pack-workers N  Threads that compress capture (default: one per GB/s of capture\n"
    "                        data plus one, at least 2, at most hardware threads less two);\n"
    "                        0 compresses on thread that receives it\n"
    "      --quiet           No live status line. Without it, while capture runs and stderr\n"
    "                        is terminal, one line there shows samples captured, memory\n"
    "                        and ring in use (or how long armed trigger has waited),\n"
    "                        redrawn 4 times per second and replaced by summary at end.\n"
    "                        Redirected stderr never gets it\n"
    "      --start-retries N   Times to start over when capture loses its start: samples lost\n"
    "                        before its first chunk, no data at all within inactivity\n"
    "                        timeout (5 s), or driver fails or ends it before its first chunk\n"
    "                        (retried at once); default 2 (3 attempts in all), 0 = never. Nothing\n"
    "                        is kept from failed attempt. Hole or stall later in\n"
    "                        capture is never retried\n"
    "      --trigger SPEC    Arm software trigger. SPEC is CHANNEL=CONDITION[,CHANNEL=CONDITION...],\n"
    "                        e.g. 0=r,2=f. CHANNEL is -C/--channels device channel number;\n"
    "                        CONDITION is r (rising), f (falling), e (either edge), 0 (low) or\n"
    "                        1 (high)\n"
    "      --trigger-combine and|or   How --trigger channels combine (default and)\n"
    "      --capture-ratio N   Percent of capture that sits before trigger, 0-100\n"
    "                        (default 0). Only valid with --trigger\n"
    "      --pre-samples N   Samples before trigger; bare number, or k, m, g (x1000 each),\n"
    "                        same grammar as --samples\n"
    "      --pre-time T      Time before trigger; bare number is ms, or 2s / 500ms, same\n"
    "                        grammar as --time. With --time, each side is rounded up on its own,\n"
    "                        so file can be one sample longer than --time alone.\n"
    "                        At most one of --pre-samples, --pre-time and --capture-ratio; each\n"
    "                        requires --trigger. File holds exactly requested length, with\n"
    "                        trigger right after pre-trigger samples. Pre-trigger equal to\n"
    "                        whole length becomes length - 1, so file ends on\n"
    "                        trigger sample (run says so). Pre-trigger longer than\n"
    "                        length is refused\n"
    "      --trigger-timeout S   Seconds to wait for trigger before giving up; 0 (default)\n"
    "                        waits forever, same as pressing Ctrl+C when it elapses. Only valid\n"
    "                        with --trigger\n"
    "  --stop            Send STOP to device's logic analyzer. Only bitoscope_cli that\n"
    "                    started capture drives that device's driver: stop another one's capture\n"
    "                    with Ctrl+C there.\n"
    "  --pwm-start       Start PWM outputs on signal generator. Outputs\n"
    "                    stay on after bitoscope_cli exits, until --pwm-stop (or device is\n"
    "                    unplugged).\n"
    "      --ports MASK      Port bitmask, decimal or 0x hex (required)\n"
    "      --freq-hz N       Frequency in whole hertz (required)\n"
    "      --duty PERCENT    Duty cycle, 0-100 (default 50)\n"
    "  --pwm-stop        Stop PWM outputs.\n"
    "      --ports MASK      Port bitmask (required)\n"
    "\n"
    "Options for any action:\n"
    "  --driver NAME     Driver to start: its executable's name in driver directory, without\n"
    "                    extension (e.g. demo). Default: sole driver there\n"
    "  --device ID       Device to drive: id that --list-devices prints (e.g. demo-0, bus1-15).\n"
    "                    Default: sole device that driver finds; with several found and none\n"
    "                    named, bitoscope_cli refuses and lists them. Not for --list-devices\n"
    "  --driver-dir DIR  Where drivers are (default: drivers/ next to bitoscope_cli); also with\n"
    "                    no action\n"
    "  --wait-s S        How long driver may take to start: its device list and its handshake\n"
    "                    (default 30); also with no action\n"
    "  -h, --help        Show this text\n"
    "\n"
    "Exit codes:\n"
    "  0  Done: command finished, or acquisition delivered its whole requested length\n"
    "  1  Failed: anything that went wrong after command line was understood; failed\n"
    "     acquisition saves nothing\n"
    "  2  Command line was refused (usage error); nothing was started\n"
    "  3  Acquisition's memory filled first and short capture was kept (--on-memory-full)\n";

inline std::string_view usageText() { return kUsageText; }

namespace detail {

inline std::optional<uint64_t> parseUnsigned(std::string_view text) {
  int base = 10;
  if (text.size() >= 2 && text[0] == '0' && (text[1] == 'x' || text[1] == 'X')) {
    base = 16;
    text.remove_prefix(2);
  }
  if (text.empty()) {
    return std::nullopt;
  }
  uint64_t value = 0;
  const char* end = text.data() + text.size();
  const auto [ptr, ec] = std::from_chars(text.data(), end, value, base);
  if (ec != std::errc{} || ptr != end) {
    return std::nullopt;
  }
  return value;
}

constexpr unsigned bit(Action a) { return 1u << static_cast<unsigned>(a); }

inline constexpr unsigned kAcquire = bit(Action::Acquire);

inline constexpr unsigned kAnyAction = bit(Action::StartCapture) | bit(Action::Stop) | kAcquire |
                                       bit(Action::PwmStart) | bit(Action::PwmStop);
inline constexpr unsigned kPluginAction =
    kAnyAction | bit(Action::ListDevices) | bit(Action::WatchDevices) | bit(Action::ListProperties);
inline constexpr unsigned kPluginDirAction = kPluginAction | bit(Action::Listen);
inline constexpr unsigned kDeviceAction = kAnyAction | bit(Action::ListProperties);
inline constexpr unsigned kDecoderDirAction = kAcquire | bit(Action::Listen) | bit(Action::ListDevices);

inline constexpr bool kRepeatable = true;

struct OptionSpec {
  std::string_view flag;
  bool takes_value;
  unsigned actions;
  bool repeatable = false;
};

inline constexpr std::array<OptionSpec, 39> kOptions = {{
    {"--driver", true, kPluginAction},
    {"--device", true, kDeviceAction},
    {"--driver-dir", true, kPluginDirAction},
    {"--wait-s", true, kPluginDirAction},
    {"--rate-hz", true, bit(Action::StartCapture)},
    {"--channel-mask", true, bit(Action::StartCapture)},
    {"--limit", true, bit(Action::StartCapture)},
    {"--buffered", false, bit(Action::StartCapture)},
    {"--duration-s", true, bit(Action::StartCapture) | bit(Action::WatchDevices)},
    {"--configure", true, bit(Action::StartCapture), kRepeatable},
    {"--ports", true, bit(Action::PwmStart) | bit(Action::PwmStop)},
    {"--freq-hz", true, bit(Action::PwmStart)},
    {"--duty", true, bit(Action::PwmStart)},
    {"--samples", true, kAcquire},
    {"--time", true, kAcquire},
    {"--config", true, kAcquire},
    {"--channels", true, kAcquire},
    {"--output-file", true, kAcquire},
    {"--output-format", true, kAcquire},
    {"--protocol-decoders", true, kAcquire, kRepeatable},
    {"--annotation-classes", true, kAcquire, kRepeatable},
    {"--decoder-plugin", true, kAcquire},
    {"--decoder-dir", true, kDecoderDirAction},
    {"--decoder-script-dir", true, kAcquire},
    {"--memory", true, kAcquire},
    {"--reserve", true, kAcquire},
    {"--memory-budget", true, kAcquire},
    {"--start-retries", true, kAcquire},
    {"--on-memory-full", true, kAcquire},
    {"--trigger", true, kAcquire},
    {"--trigger-combine", true, kAcquire},
    {"--capture-ratio", true, kAcquire},
    {"--trigger-timeout", true, kAcquire},
    {"--pre-samples", true, kAcquire},
    {"--pre-time", true, kAcquire},
    {"--ring-mib", true, kAcquire | bit(Action::StartCapture)},
    {"--pack-workers", true, kAcquire},
    {"--quiet", false, kAcquire},
}};

inline std::string_view canonicalFlag(std::string_view flag) {
  if (flag == "-c") return "--config";
  if (flag == "-C") return "--channels";
  if (flag == "-o") return "--output-file";
  if (flag == "-O") return "--output-format";
  if (flag == "-P") return "--protocol-decoders";
  if (flag == "-A") return "--annotation-classes";
  return flag;
}

inline std::expected<void, std::string> parseConfigList(std::string_view text, CliOptions& opts) {
  bool have_rate = false;
  size_t begin = 0;
  while (true) {
    const size_t colon = text.find(':', begin);
    const std::string_view item =
        text.substr(begin, colon == std::string_view::npos ? std::string_view::npos : colon - begin);
    const size_t equals = item.find('=');
    if (equals == std::string_view::npos) {
      return std::unexpected("--config: '" + std::string(item) + "' is not key=value");
    }
    const std::string_view key = item.substr(0, equals);
    const std::string_view value = item.substr(equals + 1);
    if (key.empty()) {
      return std::unexpected("--config: '" + std::string(item) + "' has no key before '='");
    }
    if (value.empty()) {
      return std::unexpected("--config: '" + std::string(item) + "' has no value after '='");
    }
    if (key == "samplerate") {
      if (have_rate) {
        return std::unexpected("--config: samplerate was given more than once");
      }
      have_rate = true;
      const auto rate = parseSampleRate(value);
      if (!rate.has_value()) {
        return std::unexpected("--config: " + rate.error());
      }
      opts.sample_rate_milli_hz = *rate;
    } else if (!opts.configure.emplace(std::string(key), std::string(value)).second) {
      return std::unexpected("--config: key '" + std::string(key) +
                             "' was given more than once");
    }
    if (colon == std::string_view::npos) break;
    begin = colon + 1;
  }
  if (!have_rate) {
    return std::unexpected("--config: samplerate is required (for example -c samplerate=1m)");
  }
  return {};
}

inline std::expected<void, std::string> parseOutputFormat(std::string_view text,
                                                          CliOptions& opts) {
  const size_t colon = text.find(':');
  const std::string_view format = text.substr(0, colon);
  if (format != "hdf5") {
    return std::unexpected("--output-format: '" + std::string(format) +
                           "' is not supported; only hdf5 is supported");
  }
  opts.output_format = "hdf5";
  if (colon == std::string_view::npos) {
    return {};
  }
  const std::string_view options = text.substr(colon + 1);
  constexpr std::string_view kGzip = "gzip=";
  if (!options.starts_with(kGzip)) {
    return std::unexpected("--output-format: '" + std::string(options) +
                           "' is not hdf5 option; its only option is gzip=N");
  }
  const auto level = parseUnsigned(options.substr(kGzip.size()));
  if (!level.has_value() || options.substr(kGzip.size()).starts_with("0x") || *level > 9) {
    return std::unexpected("--output-format: gzip level must be whole number from 0 to 9");
  }
  opts.gzip_level = static_cast<uint32_t>(*level);
  return {};
}

inline std::optional<Action> actionForFlag(std::string_view flag) {
  if (flag == "--start-capture") return Action::StartCapture;
  if (flag == "--stop") return Action::Stop;
  if (flag == "--pwm-start") return Action::PwmStart;
  if (flag == "--pwm-stop") return Action::PwmStop;
  if (flag == "--list-devices") return Action::ListDevices;
  if (flag == "--watch-devices") return Action::WatchDevices;
  if (flag == "--list-properties") return Action::ListProperties;
  return std::nullopt;
}

inline std::string_view flagForAction(Action action) {
  switch (action) {
    case Action::StartCapture: return "--start-capture";
    case Action::Stop: return "--stop";
    case Action::PwmStart: return "--pwm-start";
    case Action::PwmStop: return "--pwm-stop";
    case Action::Help: return "--help";
    case Action::Acquire: return "--samples/--time";
    case Action::ListDevices: return "--list-devices";
    case Action::WatchDevices: return "--watch-devices";
    case Action::ListProperties: return "--list-properties";
    case Action::Listen: break;
  }
  return "";
}

inline constexpr uint64_t kMaxWholeHz = std::numeric_limits<uint64_t>::max() / 1000;

// WHY: Stays well inside what std::chrono::seconds can add to a time_point.
inline constexpr uint64_t kMaxSeconds = std::numeric_limits<int32_t>::max();
inline constexpr uint64_t kMaxStartRetries = 100;

}

inline std::expected<CliOptions, std::string> parseArgs(
    std::span<const std::string_view> args,
    uint32_t hardware_threads = std::thread::hardware_concurrency()) {
  using detail::parseUnsigned;

  CliOptions opts;

  std::optional<Action> action;
  for (const std::string_view arg : args) {
    if (arg == "--help" || arg == "-h") {
      opts.action = Action::Help;
      return opts;
    }
    if (const auto found = detail::actionForFlag(arg); found.has_value()) {
      if (action.has_value()) {
        return std::unexpected("give exactly one action: both " +
                                std::string(detail::flagForAction(*action)) + " and " +
                                std::string(arg) + " were given");
      }
      action = found;
    }
  }
  if (!action.has_value() &&
      std::any_of(args.begin(), args.end(),
                  [](std::string_view a) { return a == "--samples" || a == "--time"; })) {
    action = Action::Acquire;
  }
  opts.action = action.value_or(Action::Listen);
  if (opts.action == Action::Acquire) opts.clearing_workers = acquisition::defaultClearingWorkers(hardware_threads);

  std::vector<std::string_view> seen;
  for (size_t i = 0; i < args.size(); ++i) {
    const std::string_view arg = detail::canonicalFlag(args[i]);
    if (detail::actionForFlag(arg).has_value()) {
      continue;
    }

    if (arg == "--channels" && action == Action::StartCapture) {
      return std::unexpected(
          "--channels no longer takes bitmask; use --channel-mask MASK (for example "
          "--channel-mask 0x0F)");
    }
    if (arg == "--frames") {
      return std::unexpected(
          "--frames is not supported: one acquisition is one frame here. Give --samples or "
          "--time for its length");
    }
    if (arg == "--continuous") {
      return std::unexpected(
          "--continuous is not supported: memory for capture is sized from its length, "
          "so give --samples or --time");
    }

    const auto spec = std::find_if(detail::kOptions.begin(), detail::kOptions.end(),
                                   [&](const detail::OptionSpec& s) { return s.flag == arg; });
    if (spec == detail::kOptions.end()) {
      return std::unexpected((arg.starts_with('-') ? "unknown option: " : "unexpected argument: ") +
                              std::string(arg));
    }
    if (!action.has_value() && (spec->actions & detail::bit(Action::Listen)) == 0) {
      return std::unexpected(std::string(arg) +
                              " needs action (--samples/--time, --start-capture, --stop, --pwm-start, "
                              "--pwm-stop or --list-devices); without one, bitoscope_cli lists every driver "
                              "in driver directory");
    }
    if (action.has_value() && (spec->actions & detail::bit(*action)) == 0) {
      return std::unexpected(std::string(arg) + " does not apply to " +
                              std::string(detail::flagForAction(*action)));
    }
    if (!spec->repeatable && std::find(seen.begin(), seen.end(), arg) != seen.end()) {
      return std::unexpected(std::string(arg) + " was given more than once");
    }
    seen.push_back(arg);

    if (!spec->takes_value) {
      if (arg == "--buffered") opts.buffered = true;
      if (arg == "--quiet") opts.quiet = true;
      continue;
    }

    if (i + 1 >= args.size() || args[i + 1].starts_with("--") ||
        (action == Action::Acquire && args[i + 1].starts_with('-'))) {
      return std::unexpected(std::string(arg) + " needs value");
    }
    const std::string_view text = args[++i];

    if (arg == "--driver") {
      opts.plugin = std::string(text);
      continue;
    }
    if (arg == "--device" || arg == "--driver-dir") {
      if (text.empty()) {
        return std::unexpected(std::string(arg) + (arg == "--device" ? " needs device id" : " needs directory"));
      }
      (arg == "--device" ? opts.device : opts.plugin_dir) = std::string(text);
      continue;
    }

    if (arg == "--config") {
      if (auto parsed = detail::parseConfigList(text, opts); !parsed.has_value()) {
        return std::unexpected(parsed.error());
      }
      continue;
    }
    if (arg == "--channels") {
      auto list = parseChannelList(text);
      if (!list.has_value()) {
        return std::unexpected("--channels: " + list.error());
      }
      opts.channel_mask = list->mask;
      opts.channels = std::move(list->channels);
      continue;
    }
    if (arg == "--samples") {
      const auto samples = parseSampleCount(text);
      if (!samples.has_value()) {
        return std::unexpected(samples.error());
      }
      opts.total_samples = *samples;
      continue;
    }
    if (arg == "--time") {
      const auto ms = parseDurationMs(text);
      if (!ms.has_value()) {
        return std::unexpected(ms.error());
      }
      opts.time_ms = *ms;
      continue;
    }
    if (arg == "--output-file") {
      if (text.empty()) {
        return std::unexpected("--output-file needs file name");
      }
      opts.output_file = std::string(text);
      continue;
    }
    if (arg == "--output-format") {
      if (auto parsed = detail::parseOutputFormat(text, opts); !parsed.has_value()) {
        return std::unexpected(parsed.error());
      }
      continue;
    }
    if (arg == "--protocol-decoders") {
      if (text.empty()) {
        return std::unexpected("--protocol-decoders needs decoder, for example uart:rx=D0");
      }
      if (text.find(',') != std::string_view::npos) {
        return std::unexpected("--protocol-decoders: '" + std::string(text) +
                               "': comma does not stack decoders; give each decoder its own -P and bind "
                               "upper one's input with in=ID.OUTPUT, e.g. -P uart:id=link:rx=D1 -P cobs:in=link.rx");
      }
      opts.decoders.emplace_back(text);
      continue;
    }
    if (arg == "--annotation-classes") {
      const size_t colon = text.find(':');
      if (colon == std::string_view::npos || colon == 0 || colon + 1 == text.size()) {
        return std::unexpected(
            "--annotation-classes needs ID:CLASS[,CLASS...], for example uart:start,framing_error");
      }
      opts.annotation_classes.emplace_back(text);
      continue;
    }
    if (arg == "--decoder-plugin") {
      if (text.empty()) {
        return std::unexpected("--decoder-plugin needs name, for example decoders_luajit");
      }
      opts.decoder_plugin = std::string(text);
      continue;
    }
    if (arg == "--decoder-dir") {
      if (text.empty()) {
        return std::unexpected("--decoder-dir needs directory");
      }
      opts.decoder_dir = std::string(text);
      continue;
    }
    if (arg == "--decoder-script-dir") {
      if (text.empty()) {
        return std::unexpected("--decoder-script-dir needs directory");
      }
      opts.decoder_script_dir = std::string(text);
      continue;
    }
    if (arg == "--memory") {
      if (text == "whole") {
        opts.memory = MemoryStrategy::Whole;
      } else if (text == "chunked") {
        opts.memory = MemoryStrategy::Chunked;
      } else {
        return std::unexpected("--memory: '" + std::string(text) +
                               "' is not strategy; use whole or chunked");
      }
      continue;
    }
    if (arg == "--on-memory-full") {
      // WHY: No post-loop 'requires' rule: even a capture the budget covers can run short if other processes take free RAM.
      if (text == "keep") {
        opts.on_memory_full = OnMemoryFull::Keep;
      } else if (text == "discard") {
        opts.on_memory_full = OnMemoryFull::Discard;
      } else {
        return std::unexpected("--on-memory-full: '" + std::string(text) +
                               "' is not choice; use keep or discard");
      }
      continue;
    }
    if (arg == "--reserve" || arg == "--memory-budget") {
      const auto bytes = parseSize(text);
      if (!bytes.has_value()) {
        return std::unexpected(std::string(arg) + ": " + bytes.error());
      }
      if (arg == "--memory-budget") {
        if (*bytes == 0) {
          return std::unexpected("--memory-budget must be greater than zero");
        }
        opts.memory_budget_bytes = *bytes;
      } else {
        opts.reserve_bytes = *bytes;
      }
      continue;
    }

    if (arg == "--trigger") {
      auto matches = parseTriggerSpec(text);
      if (!matches.has_value()) {
        return std::unexpected(matches.error());
      }
      if (!opts.trigger.has_value()) opts.trigger.emplace();
      opts.trigger->device_matches = std::move(*matches);
      continue;
    }
    if (arg == "--trigger-combine") {
      auto combine = parseTriggerCombine(text);
      if (!combine.has_value()) {
        return std::unexpected(combine.error());
      }
      if (!opts.trigger.has_value()) opts.trigger.emplace();
      opts.trigger->combine = *combine;
      continue;
    }
    if (arg == "--capture-ratio") {
      auto ratio = parseCaptureRatio(text);
      if (!ratio.has_value()) {
        return std::unexpected(ratio.error());
      }
      if (!opts.trigger.has_value()) opts.trigger.emplace();
      opts.trigger->capture_ratio_percent = *ratio;
      continue;
    }
    if (arg == "--pre-samples") {
      auto samples = parsePreSampleCount(text);
      if (!samples.has_value()) {
        return std::unexpected(samples.error());
      }
      if (!opts.trigger.has_value()) opts.trigger.emplace();
      opts.trigger->pre_samples = *samples;
      continue;
    }
    if (arg == "--pre-time") {
      auto ms = parsePreDurationMs(text);
      if (!ms.has_value()) {
        return std::unexpected(ms.error());
      }
      if (!opts.trigger.has_value()) opts.trigger.emplace();
      opts.trigger->pre_time_ms = *ms;
      continue;
    }

    if (arg == "--configure") {
      const size_t equals = text.find('=');
      if (equals == std::string_view::npos) {
        return std::unexpected("--configure: '" + std::string(text) + "' is not key=value");
      }
      const std::string_view key = text.substr(0, equals);
      const std::string_view value = text.substr(equals + 1);
      if (key.empty()) {
        return std::unexpected("--configure: '" + std::string(text) + "' has no key before '='");
      }
      if (value.empty()) {
        return std::unexpected("--configure: '" + std::string(text) + "' has no value after '='");
      }
      if (!opts.configure.emplace(std::string(key), std::string(value)).second) {
        return std::unexpected("--configure: key '" + std::string(key) +
                                "' was given more than once");
      }
      continue;
    }

    const auto number = parseUnsigned(text);
    if (!number.has_value()) {
      return std::unexpected(std::string(arg) + ": '" + std::string(text) +
                              "' is not non-negative whole number");
    }
    const uint64_t value = *number;

    auto tooLarge = [&](std::string_view why) {
      return std::unexpected(std::string(arg) + ": " + std::string(text) + " is too large - " +
                              std::string(why));
    };

    if (arg == "--wait-s" || arg == "--duration-s") {
      if (value > detail::kMaxSeconds) {
        return tooLarge("at most " + std::to_string(detail::kMaxSeconds) + " seconds");
      }
      (arg == "--wait-s" ? opts.wait : opts.duration) =
          std::chrono::seconds(static_cast<int64_t>(value));
    } else if (arg == "--rate-hz" || arg == "--freq-hz") {
      if (value == 0) {
        return std::unexpected(std::string(arg) + " must be greater than zero");
      }
      if (value > detail::kMaxWholeHz) {
        return tooLarge("wire carries millihertz, so at most " +
                        std::to_string(detail::kMaxWholeHz) + " Hz");
      }
      (arg == "--rate-hz" ? opts.rate_hz : opts.freq_hz) = value;
    } else if (arg == "--channel-mask" || arg == "--ports") {
      if (value == 0) {
        return std::unexpected(std::string(arg) + " must select at least one bit");
      }
      if (value > std::numeric_limits<uint32_t>::max()) {
        return tooLarge("mask is at most 32 bits");
      }
      (arg == "--channel-mask" ? opts.channel_mask : opts.port_mask) = static_cast<uint32_t>(value);
    } else if (arg == "--start-retries") {
      if (value > detail::kMaxStartRetries) {
        return tooLarge("at most " + std::to_string(detail::kMaxStartRetries));
      }
      opts.start_retries = static_cast<uint32_t>(value);
    } else if (arg == "--limit") {
      opts.sample_limit = value;
    } else if (arg == "--ring-mib") {
      if (auto geometry = acquisition::ringGeometryForMiB(value); !geometry) {
        return std::unexpected("--ring-mib: " + geometry.error());
      }
      opts.ring_mib = value;
    } else if (arg == "--pack-workers") {
      if (value > acquisition::kMaxPackWorkers) {
        return tooLarge("at most " + std::to_string(acquisition::kMaxPackWorkers));
      }
      opts.pack_workers = static_cast<uint32_t>(value);
    } else if (arg == "--trigger-timeout") {
      if (value > detail::kMaxSeconds) {
        return tooLarge("at most " + std::to_string(detail::kMaxSeconds) + " seconds");
      }
      if (!opts.trigger.has_value()) opts.trigger.emplace();
      opts.trigger->timeout_s = value;
    } else if (arg == "--duty") {
      if (value > 100) {
        return std::unexpected(std::string(arg) + ": " + std::string(text) +
                                " is not percentage (0-100)");
      }
      opts.duty_percent = static_cast<uint32_t>(value);
    }
  }

  if (opts.action == Action::Acquire &&
      std::find(seen.begin(), seen.end(), std::string_view("--pack-workers")) == seen.end()) {
    opts.pack_workers = acquisition::defaultPackWorkers(
        hardware_threads,
        acquisition::captureBytesPerSecond(static_cast<uint32_t>(std::popcount(opts.channel_mask)),
                                           opts.sample_rate_milli_hz));
  }

  auto require = [&](std::string_view flag) -> std::expected<void, std::string> {
    if (std::find(seen.begin(), seen.end(), flag) == seen.end()) {
      return std::unexpected(std::string(detail::flagForAction(*action)) + " requires " +
                              std::string(flag));
    }
    return {};
  };
  std::expected<void, std::string> required;
  switch (opts.action) {
    case Action::StartCapture:
      required = require("--rate-hz").and_then([&] { return require("--channel-mask"); });
      break;
    case Action::Acquire: {
      const auto has = [&](std::string_view flag) {
        return std::find(seen.begin(), seen.end(), flag) != seen.end();
      };
      if (has("--samples") && has("--time")) {
        required = std::unexpected(
            "give either --samples or --time, not both (length of one capture)");
      } else if (!has("--config")) {
        required = std::unexpected(
            "acquisition requires -c samplerate=RATE (for example -c samplerate=1m)");
      } else {
        required = require("--channels");
      }
      if (required.has_value() && opts.time_ms.has_value()) {
        const auto samples = samplesForDuration(*opts.time_ms, opts.sample_rate_milli_hz);
        if (!samples.has_value()) {
          required = std::unexpected(samples.error());
        } else {
          opts.total_samples = *samples;
        }
      }
      if (required.has_value() && has("--capture-ratio") && !has("--trigger")) {
        required = std::unexpected("--capture-ratio requires --trigger");
      } else if (required.has_value() && has("--pre-samples") && !has("--trigger")) {
        required = std::unexpected("--pre-samples requires --trigger");
      } else if (required.has_value() && has("--pre-time") && !has("--trigger")) {
        required = std::unexpected("--pre-time requires --trigger");
      } else if (required.has_value() && has("--trigger-combine") && !has("--trigger")) {
        required = std::unexpected("--trigger-combine requires --trigger");
      } else if (required.has_value() && has("--trigger-timeout") && !has("--trigger")) {
        required = std::unexpected("--trigger-timeout requires --trigger");
      } else if (required.has_value()) {
        std::vector<std::string_view> given_pre;
        if (has("--capture-ratio")) given_pre.push_back("--capture-ratio");
        if (has("--pre-samples")) given_pre.push_back("--pre-samples");
        if (has("--pre-time")) given_pre.push_back("--pre-time");
        if (given_pre.size() > 1) {
          std::string list;
          for (size_t i = 0; i < given_pre.size(); ++i) {
            if (i != 0) list += (i + 1 == given_pre.size() ? " and " : ", ");
            list += given_pre[i];
          }
          required = std::unexpected("give at most one of --capture-ratio, --pre-samples and "
                                     "--pre-time (" +
                                     list + " were given)");
        } else if (opts.trigger.has_value()) {
          auto mapped = mapTriggerChannels(opts.trigger->device_matches, opts.channels);
          if (!mapped.has_value()) {
            required = std::unexpected(mapped.error());
          } else {
            opts.trigger->capture_matches = std::move(*mapped);
          }
        }
      }
      if (required.has_value() && !opts.annotation_classes.empty() && opts.decoders.empty()) {
        required = std::unexpected("--annotation-classes requires -P (there is no decoder to print)");
      }
      if (required.has_value() && opts.decoders.empty()) {
        if (has("--decoder-plugin")) {
          required = std::unexpected("--decoder-plugin requires -P (there is no decoder to run)");
        } else if (has("--decoder-script-dir")) {
          required = std::unexpected("--decoder-script-dir requires -P (there is no decoder to run)");
        }
      }
      break;
    }
    case Action::PwmStart:
      required = require("--ports").and_then([&] { return require("--freq-hz"); });
      break;
    case Action::PwmStop:
      required = require("--ports");
      break;
    case Action::Listen:
    case Action::Help:
    case Action::Stop:
    case Action::ListDevices:
    case Action::WatchDevices:
    case Action::ListProperties:
      break;
  }
  if (!required.has_value()) {
    return std::unexpected(required.error());
  }
  return opts;
}

}

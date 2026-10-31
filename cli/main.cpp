/*
 * SPDX-License-Identifier: MIT
 */

#include <iostream>
#include <string>
#include <string_view>
#include <vector>

#include "cli_drivers.h"
#include "cli_run.h"
#include "console_code_page.h"
#include "display_text_file.h"
#include "shutdown_signal.h"
#include "status_line.h"

int main(int argc, char** argv) {
  bitoscope::cli::ShutdownSignal::onGiveUp(&bitoscope::console::Utf8ConsoleOutput::restoreActive);
  return bitoscope::console::runWithUtf8Console(bitoscope::console::systemConsoleCodePage(), [&] {
    bitoscope::cli::ShutdownSignal::install();

    const std::vector<std::string_view> args(argv + 1, argv + argc);
    bitoscope::cli::CliEnvironment environment;
    environment.out = &std::cout;
    environment.err = &std::cerr;
    environment.executable = bitoscope::cli::currentExecutable(argc > 0 ? argv[0] : "");
    namespace dtf = bitoscope::display_text_file;
    for (const std::string& line : dtf::linesToShow(
             dtf::applyAbbreviations({environment.executable.parent_path(),
                                      dtf::settingsDirectory(dtf::systemSettingsFacts()),
                                      dtf::abbreviationsFromEnvironment()}),
             true)) {
      std::cerr << "bitoscope: " << line << "\n";
    }
    environment.interrupted = [] { return bitoscope::cli::ShutdownSignal::requested(); };
    environment.stderr_columns = bitoscope::cli::stderrTerminalColumns;
    return bitoscope::cli::runCli(args, environment);
  });
}

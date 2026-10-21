/*
 * SPDX-License-Identifier: MIT
 */

#include "color_scheme_watcher.h"

#include "portal_settings.h"

#include <dbus/dbus.h>
#include <poll.h>
#include <sys/eventfd.h>
#include <unistd.h>

#include <cerrno>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <format>
#include <optional>
#include <utility>

namespace bitoscope::gui::linux_platform {

namespace {

constexpr const char* kNamespace = "org.freedesktop.appearance";
constexpr const char* kKey = "color-scheme";

const char* schemeName(gui_linux::ColorScheme scheme) {
  switch (scheme) {
  case gui_linux::ColorScheme::PreferDark:
    return "prefer dark";
  case gui_linux::ColorScheme::PreferLight:
    return "prefer light";
  case gui_linux::ColorScheme::NoPreference:
    break;
  }
  return "no preference";
}

std::string busErrorText(const DBusError& error) {
  return dbus_error_is_set(&error) != 0 ? std::format("{}: {}", error.name, error.message)
                                        : std::string("no error");
}

}

ColorSchemeWatcher::ColorSchemeWatcher(std::function<void()> onChange)
    : onChange_(std::move(onChange)), stopEvent_(eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK)) {
  if (stopEvent_ < 0) {
    note(std::format("Desktop theme: no event for stopping watcher (errno {}); theme is "
                     "read once and not followed",
                     errno));
  }
  thread_ = std::thread([this] { run(); });
}

ColorSchemeWatcher::~ColorSchemeWatcher() {
  if (stopEvent_ >= 0) {
    const std::uint64_t one = 1;
    [[maybe_unused]] const ssize_t written = write(stopEvent_, &one, sizeof(one));
  }
  if (thread_.joinable()) {
    thread_.join();
  }
  if (stopEvent_ >= 0) {
    close(stopEvent_);
  }
}

gui_linux::ColorScheme ColorSchemeWatcher::current() {
  std::unique_lock lock(mutex_);
  if (!state_.firstAnswered && !state_.firstTimedOut) {
    if (!answered_.wait_for(lock, std::chrono::milliseconds(kFirstAnswerWaitMilliseconds),
                            [this] { return state_.firstAnswered; })) {
      state_.firstTimedOut = true;
      notes_.push_back(std::format(
        "Desktop theme: portal did not answer within {} ms; starting light, following it if it "
        "answers later",
        kFirstAnswerWaitMilliseconds));
    }
  }
  return state_.scheme;
}

std::vector<std::string> ColorSchemeWatcher::takeNotes() {
  std::lock_guard lock(mutex_);
  return std::exchange(notes_, {});
}

void ColorSchemeWatcher::note(std::string line) {
  std::lock_guard lock(mutex_);
  notes_.push_back(std::move(line));
}

void ColorSchemeWatcher::publish(gui_linux::ColorScheme scheme, bool first) {
  bool changed = false;
  {
    std::lock_guard lock(mutex_);
    changed = gui_linux::applyColorSchemeAnswer(state_, scheme, first);
  }
  if (first) {
    answered_.notify_all();
  }
  if (changed && onChange_) {
    onChange_();
  }
}

void ColorSchemeWatcher::run() {
  DBusError error;
  dbus_error_init(&error);
  // WHY: Private bus connection: a shared one must not be closed by this thread, and others in the process use libdbus.
  DBusConnection* connection = dbus_bus_get_private(DBUS_BUS_SESSION, &error);
  if (connection == nullptr) {
    note("Desktop theme: no session D-Bus (" + busErrorText(error) + "); using light theme");
    dbus_error_free(&error);
    publish(gui_linux::ColorScheme::NoPreference, true);
    return;
  }
  // WHY: libdbus exits the process when the bus goes away unless told otherwise.
  dbus_connection_set_exit_on_disconnect(connection, FALSE);

  // WHY: Subscribe BEFORE reading the value, so a change between the two is not missed.
  const std::string rule = std::format(
    "type='signal',sender='{}',path='{}',interface='{}',member='SettingChanged',arg0='{}',arg1='{}'",
    kPortalService, kPortalPath, kPortalSettingsInterface, kNamespace, kKey);
  dbus_bus_add_match(connection, rule.c_str(), &error);
  const bool subscribed = dbus_error_is_set(&error) == 0;
  if (!subscribed) {
    note("Desktop theme: changes cannot be followed (" + busErrorText(error) + ")");
  }
  dbus_error_free(&error);

  const std::expected<PortalValue, std::string> value = readPortalSetting(connection, kNamespace, kKey);
  if (value && value->number) {
    const gui_linux::ColorScheme scheme = gui_linux::colorSchemeFromPortal(*value->number);
    note(std::format("Desktop theme: portal's {} {} is {} ({}){}", kNamespace, kKey,
                     *value->number, schemeName(scheme), subscribed ? ", followed" : ""));
    publish(scheme, true);
  } else {
    note(std::format("Desktop theme: portal's {} {} could not be read ({}); using light "
                     "theme{}",
                     kNamespace, kKey, value ? "not uint32" : value.error(),
                     subscribed ? " until it says otherwise" : ""));
    publish(gui_linux::ColorScheme::NoPreference, true);
  }

  int busFd = -1;
  if (subscribed && stopEvent_ >= 0 && !dbus_connection_get_unix_fd(connection, &busFd)) {
    note("Desktop theme: bus connection has no socket to wait on; changes are not followed");
  }
  while (busFd >= 0) {
    // WHY: Dispatch what libdbus already read into its buffers: poll() would not see it again.
    dbus_connection_read_write(connection, 0);
    while (DBusMessage* message = dbus_connection_pop_message(connection)) {
      if (const std::optional<PortalValue> changed = changedPortalSetting(message, kNamespace, kKey);
          changed && changed->number) {
        publish(gui_linux::colorSchemeFromPortal(*changed->number), false);
      }
      dbus_message_unref(message);
    }
    if (!dbus_connection_get_is_connected(connection)) {
      note("Desktop theme: session D-Bus went away; changes are no longer followed");
      break;
    }
    pollfd fds[2] = {{busFd, POLLIN, 0}, {stopEvent_, POLLIN, 0}};
    if (poll(fds, 2, -1) < 0) {
      if (errno == EINTR) {
        continue;
      }
      note(std::format("Desktop theme: waiting for changes failed (errno {}); no longer followed",
                       errno));
      break;
    }
    if ((fds[1].revents & POLLIN) != 0) {
      break;
    }
  }
  dbus_connection_close(connection);
  dbus_connection_unref(connection);
}

}

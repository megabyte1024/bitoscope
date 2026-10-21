/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <dbus/dbus.h>

#include <cstdint>
#include <expected>
#include <optional>
#include <string>

namespace bitoscope::gui::linux_platform {

inline constexpr const char* kPortalService = "org.freedesktop.portal.Desktop";
inline constexpr const char* kPortalPath = "/org/freedesktop/portal/desktop";
inline constexpr const char* kPortalSettingsInterface = "org.freedesktop.portal.Settings";

struct BusError {
  DBusError error;
  BusError() { dbus_error_init(&error); }
  ~BusError() { dbus_error_free(&error); }
  BusError(const BusError&) = delete;
  BusError& operator=(const BusError&) = delete;
  bool set() const { return dbus_error_is_set(&error) != 0; }
  std::string text() const;
};

struct PortalValue {
  std::optional<std::uint32_t> number;
  std::optional<std::string> text;
  const char* method = "ReadOne";
};

std::expected<PortalValue, std::string> readPortalSetting(DBusConnection* connection, const char* ns,
                                                          const char* key);

std::expected<PortalValue, std::string> readPortalSetting(const char* ns, const char* key);

std::optional<PortalValue> changedPortalSetting(DBusMessage* message, const char* ns, const char* key);

// WHY: A portal being started by D-Bus activation answers slowly, and one that hangs must not hold the caller.
inline constexpr int kPortalCallTimeoutMilliseconds = 1000;

}

/*
 * SPDX-License-Identifier: MIT
 */

#include "portal_settings.h"

#include <cstring>
#include <format>

namespace bitoscope::gui::linux_platform {

namespace {

std::optional<PortalValue> valueInVariants(DBusMessageIter* iter) {
  for (int depth = 0; depth < 4; ++depth) {
    const int type = dbus_message_iter_get_arg_type(iter);
    if (type == DBUS_TYPE_UINT32) {
      dbus_uint32_t value = 0;
      dbus_message_iter_get_basic(iter, &value);
      PortalValue out;
      out.number = value;
      return out;
    }
    if (type == DBUS_TYPE_INT32) {
      // WHY: org.gnome.desktop.interface cursor-size answers int32, not uint32; a negative size counts as no number.
      dbus_int32_t value = 0;
      dbus_message_iter_get_basic(iter, &value);
      if (value < 0) {
        return std::nullopt;
      }
      PortalValue out;
      out.number = static_cast<std::uint32_t>(value);
      return out;
    }
    if (type == DBUS_TYPE_STRING) {
      const char* value = nullptr;
      dbus_message_iter_get_basic(iter, &value);
      PortalValue out;
      out.text = std::string(value != nullptr ? value : "");
      return out;
    }
    if (type != DBUS_TYPE_VARIANT) {
      return std::nullopt;
    }
    DBusMessageIter inner;
    dbus_message_iter_recurse(iter, &inner);
    *iter = inner;
  }
  return std::nullopt;
}

std::expected<PortalValue, std::string> call(DBusConnection* connection, const char* method,
                                             const char* ns, const char* key, bool& unknownMethod) {
  unknownMethod = false;
  DBusMessage* message =
    dbus_message_new_method_call(kPortalService, kPortalPath, kPortalSettingsInterface, method);
  if (message == nullptr) {
    return std::unexpected("out of memory building method call");
  }
  dbus_message_append_args(message, DBUS_TYPE_STRING, &ns, DBUS_TYPE_STRING, &key, DBUS_TYPE_INVALID);
  BusError error;
  DBusMessage* reply = dbus_connection_send_with_reply_and_block(
    connection, message, kPortalCallTimeoutMilliseconds, &error.error);
  dbus_message_unref(message);
  if (reply == nullptr) {
    unknownMethod = error.set() && std::strcmp(error.error.name, DBUS_ERROR_UNKNOWN_METHOD) == 0;
    return std::unexpected(error.text());
  }
  std::optional<PortalValue> value;
  DBusMessageIter iter;
  if (dbus_message_iter_init(reply, &iter)) {
    value = valueInVariants(&iter);
  }
  dbus_message_unref(reply);
  if (!value) {
    return std::unexpected("answer holds no uint32 or string");
  }
  value->method = method;
  return *value;
}

}

std::string BusError::text() const {
  return set() ? std::format("{}: {}", error.name, error.message) : std::string("no error");
}

std::expected<PortalValue, std::string> readPortalSetting(DBusConnection* connection, const char* ns,
                                                          const char* key) {
  bool unknownMethod = false;
  auto value = call(connection, "ReadOne", ns, key, unknownMethod);
  if (!value && unknownMethod) {
    value = call(connection, "Read", ns, key, unknownMethod);
  }
  return value;
}

std::expected<PortalValue, std::string> readPortalSetting(const char* ns, const char* key) {
  BusError error;
  DBusConnection* connection = dbus_bus_get_private(DBUS_BUS_SESSION, &error.error);
  if (connection == nullptr) {
    return std::unexpected("no session D-Bus (" + error.text() + ")");
  }
  dbus_connection_set_exit_on_disconnect(connection, FALSE);
  auto value = readPortalSetting(connection, ns, key);
  dbus_connection_close(connection);
  dbus_connection_unref(connection);
  return value;
}

std::optional<PortalValue> changedPortalSetting(DBusMessage* message, const char* ns, const char* key) {
  if (!dbus_message_is_signal(message, kPortalSettingsInterface, "SettingChanged")) {
    return std::nullopt;
  }
  DBusMessageIter iter;
  if (!dbus_message_iter_init(message, &iter)) {
    return std::nullopt;
  }
  const char* strings[2] = {nullptr, nullptr};
  for (const char*& text : strings) {
    if (dbus_message_iter_get_arg_type(&iter) != DBUS_TYPE_STRING) {
      return std::nullopt;
    }
    dbus_message_iter_get_basic(&iter, &text);
    if (!dbus_message_iter_next(&iter)) {
      return std::nullopt;
    }
  }
  if (std::strcmp(strings[0], ns) != 0 || std::strcmp(strings[1], key) != 0) {
    return std::nullopt;
  }
  return valueInVariants(&iter);
}

}

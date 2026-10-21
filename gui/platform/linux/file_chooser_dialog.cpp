/*
 * SPDX-License-Identifier: MIT
 */

#include "file_chooser_dialog.h"

#include "portal_settings.h"

#include <dbus/dbus.h>

#include <chrono>
#include <cstdint>
#include <cstring>
#include <format>
#include <random>
#include <system_error>
#include <utility>

namespace bitoscope::gui::linux_platform {

namespace {

constexpr const char* kFileChooserInterface = "org.freedesktop.portal.FileChooser";
constexpr const char* kRequestInterface = "org.freedesktop.portal.Request";

struct Connection {
  DBusConnection* handle = nullptr;
  Connection() = default;
  ~Connection() {
    if (handle != nullptr) {
      dbus_connection_close(handle);
      dbus_connection_unref(handle);
    }
  }
  Connection(const Connection&) = delete;
  Connection& operator=(const Connection&) = delete;
};

struct Message {
  DBusMessage* handle = nullptr;
  explicit Message(DBusMessage* message) : handle(message) {}
  ~Message() {
    if (handle != nullptr) {
      dbus_message_unref(handle);
    }
  }
  Message(const Message&) = delete;
  Message& operator=(const Message&) = delete;
};

bool appendString(DBusMessageIter* iter, const std::string& text) {
  const char* value = text.c_str();
  return dbus_message_iter_append_basic(iter, DBUS_TYPE_STRING, &value) != 0;
}

bool appendFilter(DBusMessageIter* iter, const gui_linux::FileFilter& filter) {
  DBusMessageIter entry;
  DBusMessageIter patterns;
  bool ok = dbus_message_iter_open_container(iter, DBUS_TYPE_STRUCT, nullptr, &entry) != 0;
  ok = ok && appendString(&entry, filter.name);
  ok = ok && dbus_message_iter_open_container(&entry, DBUS_TYPE_ARRAY, "(us)", &patterns) != 0;
  for (const gui_linux::FilterPattern& pattern : filter.patterns) {
    DBusMessageIter one;
    const dbus_uint32_t kind = pattern.kind;
    ok = ok && dbus_message_iter_open_container(&patterns, DBUS_TYPE_STRUCT, nullptr, &one) != 0;
    ok = ok && dbus_message_iter_append_basic(&one, DBUS_TYPE_UINT32, &kind) != 0;
    ok = ok && appendString(&one, pattern.pattern);
    ok = ok && dbus_message_iter_close_container(&patterns, &one) != 0;
  }
  ok = ok && dbus_message_iter_close_container(&entry, &patterns) != 0;
  return ok && dbus_message_iter_close_container(iter, &entry) != 0;
}

template <typename Fill>
bool appendOption(DBusMessageIter* options, const char* key, const char* signature, Fill&& fill) {
  DBusMessageIter entry;
  DBusMessageIter variant;
  bool ok = dbus_message_iter_open_container(options, DBUS_TYPE_DICT_ENTRY, nullptr, &entry) != 0;
  ok = ok && dbus_message_iter_append_basic(&entry, DBUS_TYPE_STRING, &key) != 0;
  ok = ok && dbus_message_iter_open_container(&entry, DBUS_TYPE_VARIANT, signature, &variant) != 0;
  ok = ok && fill(&variant);
  ok = ok && dbus_message_iter_close_container(&entry, &variant) != 0;
  return ok && dbus_message_iter_close_container(options, &entry) != 0;
}

DBusMessage* buildCall(const gui_linux::FileChooserRequest& request, const std::string& parentWindow) {
  DBusMessage* message =
    dbus_message_new_method_call(kPortalService, kPortalPath, kFileChooserInterface, request.method.c_str());
  if (message == nullptr) {
    return nullptr;
  }
  DBusMessageIter args;
  DBusMessageIter options;
  dbus_message_iter_init_append(message, &args);
  bool ok = appendString(&args, parentWindow) && appendString(&args, request.title);
  ok = ok && dbus_message_iter_open_container(&args, DBUS_TYPE_ARRAY, "{sv}", &options) != 0;
  ok = ok && appendOption(&options, "handle_token", "s",
                          [&](DBusMessageIter* v) { return appendString(v, request.handleToken); });
  ok = ok && appendOption(&options, "filters", "a(sa(us))", [&](DBusMessageIter* v) {
         DBusMessageIter filters;
         bool filled = dbus_message_iter_open_container(v, DBUS_TYPE_ARRAY, "(sa(us))", &filters) != 0;
         for (const gui_linux::FileFilter& filter : request.filters) {
           filled = filled && appendFilter(&filters, filter);
         }
         return filled && dbus_message_iter_close_container(v, &filters) != 0;
       });
  ok = ok && appendOption(&options, "current_filter", "(sa(us))",
                          [&](DBusMessageIter* v) { return appendFilter(v, request.currentFilter); });
  if (request.currentName) {
    ok = ok && appendOption(&options, "current_name", "s",
                            [&](DBusMessageIter* v) { return appendString(v, *request.currentName); });
  }
  if (request.currentFolder) {
    ok = ok && appendOption(&options, "current_folder", "ay", [&](DBusMessageIter* v) {
           DBusMessageIter bytes;
           const std::uint8_t* data = request.currentFolder->data();
           return dbus_message_iter_open_container(v, DBUS_TYPE_ARRAY, "y", &bytes) != 0 &&
                  dbus_message_iter_append_fixed_array(&bytes, DBUS_TYPE_BYTE, &data,
                                                       static_cast<int>(request.currentFolder->size())) != 0 &&
                  dbus_message_iter_close_container(v, &bytes) != 0;
         });
  }
  ok = ok && dbus_message_iter_close_container(&args, &options) != 0;
  if (!ok) {
    dbus_message_unref(message);
    return nullptr;
  }
  return message;
}

struct Response {
  std::uint32_t code = 2;
  std::vector<std::string> uris;
};

std::optional<Response> readResponse(DBusMessage* message) {
  DBusMessageIter args;
  if (!dbus_message_iter_init(message, &args) || dbus_message_iter_get_arg_type(&args) != DBUS_TYPE_UINT32) {
    return std::nullopt;
  }
  Response response;
  dbus_uint32_t code = 0;
  dbus_message_iter_get_basic(&args, &code);
  response.code = code;
  if (!dbus_message_iter_next(&args) || dbus_message_iter_get_arg_type(&args) != DBUS_TYPE_ARRAY) {
    return response;
  }
  DBusMessageIter results;
  dbus_message_iter_recurse(&args, &results);
  for (; dbus_message_iter_get_arg_type(&results) == DBUS_TYPE_DICT_ENTRY; dbus_message_iter_next(&results)) {
    DBusMessageIter entry;
    dbus_message_iter_recurse(&results, &entry);
    if (dbus_message_iter_get_arg_type(&entry) != DBUS_TYPE_STRING) {
      continue;
    }
    const char* key = nullptr;
    dbus_message_iter_get_basic(&entry, &key);
    if (key == nullptr || std::strcmp(key, "uris") != 0 || !dbus_message_iter_next(&entry) ||
        dbus_message_iter_get_arg_type(&entry) != DBUS_TYPE_VARIANT) {
      continue;
    }
    DBusMessageIter variant;
    dbus_message_iter_recurse(&entry, &variant);
    if (dbus_message_iter_get_arg_type(&variant) != DBUS_TYPE_ARRAY ||
        dbus_message_iter_get_element_type(&variant) != DBUS_TYPE_STRING) {
      continue;
    }
    DBusMessageIter uris;
    dbus_message_iter_recurse(&variant, &uris);
    for (; dbus_message_iter_get_arg_type(&uris) == DBUS_TYPE_STRING; dbus_message_iter_next(&uris)) {
      const char* uri = nullptr;
      dbus_message_iter_get_basic(&uris, &uri);
      response.uris.emplace_back(uri != nullptr ? uri : "");
    }
  }
  return response;
}

void deliver(DBusConnection* connection, DBusMessage* message, dbus_uint32_t callSerial,
             gui_linux::FileChooserExchange& exchange, std::vector<std::string>& notes) {
  const int type = dbus_message_get_type(message);
  if (type == DBUS_MESSAGE_TYPE_METHOD_RETURN && dbus_message_get_reply_serial(message) == callSerial) {
    const char* handle = nullptr;
    BusError error;
    if (!dbus_message_get_args(message, &error.error, DBUS_TYPE_OBJECT_PATH, &handle, DBUS_TYPE_INVALID) ||
        handle == nullptr) {
      exchange.callFailed("org.freedesktop.DBus.Error.InvalidArgs", "reply holds no request object path");
      return;
    }
    if (const std::optional<std::string> other = exchange.callReturned(handle)) {
      notes.push_back(std::format("File dialog: portal's request is {} (not expected path)", *other));
      BusError matchError;
      dbus_bus_add_match(connection, gui_linux::responseMatchRule(*other).c_str(), &matchError.error);
      if (matchError.set()) {
        exchange.callFailed(matchError.error.name, matchError.error.message);
      }
    }
    return;
  }
  if (type == DBUS_MESSAGE_TYPE_ERROR && dbus_message_get_reply_serial(message) == callSerial) {
    const char* name = dbus_message_get_error_name(message);
    const char* text = nullptr;
    BusError ignored;
    if (!dbus_message_get_args(message, &ignored.error, DBUS_TYPE_STRING, &text, DBUS_TYPE_INVALID)) {
      text = nullptr;
    }
    exchange.callFailed(name != nullptr ? name : "", text != nullptr ? text : "");
    return;
  }
  if (dbus_message_is_signal(message, kRequestInterface, "Response")) {
    const char* path = dbus_message_get_path(message);
    if (const std::optional<Response> response = readResponse(message); response && path != nullptr) {
      exchange.responseArrived(path, response->code, response->uris);
    }
  }
}

void closeRequest(DBusConnection* connection, const std::string& path) {
  const Message close(dbus_message_new_method_call(kPortalService, path.c_str(), kRequestInterface, "Close"));
  if (close.handle == nullptr) {
    return;
  }
  dbus_message_set_no_reply(close.handle, TRUE);
  if (dbus_connection_send(connection, close.handle, nullptr)) {
    dbus_connection_flush(connection);
  }
}

gui_linux::FileChooserAnswer askOnce(gui_linux::FileChooserKind kind, const gui_linux::FileChooserTexts& texts,
                                     const std::string& parentWindow, const DialogPump& pump,
                                     std::vector<std::string>& notes) {
  Connection connection;
  {
    BusError error;
    connection.handle = dbus_bus_get_private(DBUS_BUS_SESSION, &error.error);
    if (connection.handle == nullptr) {
      return std::unexpected(gui_linux::noSessionBusError(error.text()));
    }
  }
  // WHY: libdbus exits the process when the bus goes away unless told otherwise.
  dbus_connection_set_exit_on_disconnect(connection.handle, FALSE);

  std::random_device random;
  const std::uint64_t unique = (static_cast<std::uint64_t>(random()) << 32) | random();
  const std::string token = gui_linux::handleToken(unique);
  const char* uniqueName = dbus_bus_get_unique_name(connection.handle);
  const std::optional<std::string> requestPath =
    gui_linux::requestObjectPath(uniqueName != nullptr ? uniqueName : "", token);
  if (!requestPath) {
    return std::unexpected(std::format("session D-Bus gave unusable connection name \"{}\"",
                                       uniqueName != nullptr ? uniqueName : ""));
  }

  // WHY: Subscribe to the Response BEFORE the call, so it cannot arrive unseen.
  {
    BusError error;
    dbus_bus_add_match(connection.handle, gui_linux::responseMatchRule(*requestPath).c_str(), &error.error);
    if (error.set()) {
      return std::unexpected(std::format("session D-Bus refused subscription to file dialog's answer ({})",
                                         error.text()));
    }
  }

  const gui_linux::FileChooserRequest request = gui_linux::fileChooserRequest(kind, texts, token);
  const Message call(buildCall(request, parentWindow));
  dbus_uint32_t serial = 0;
  if (call.handle == nullptr || !dbus_connection_send(connection.handle, call.handle, &serial)) {
    return std::unexpected(std::string("out of memory building file dialog request"));
  }
  dbus_connection_flush(connection.handle);
  notes.push_back(std::format("File dialog: {} asked of desktop portal, request {}", request.method, *requestPath));

  gui_linux::FileChooserExchange exchange(*requestPath);
  const auto sent = std::chrono::steady_clock::now();
  while (!exchange.done()) {
    dbus_connection_read_write(connection.handle, kFileChooserPollMilliseconds);
    while (!exchange.done()) {
      const Message message(dbus_connection_pop_message(connection.handle));
      if (message.handle == nullptr) {
        break;
      }
      deliver(connection.handle, message.handle, serial, exchange, notes);
    }
    if (!dbus_connection_get_is_connected(connection.handle)) {
      exchange.busLost();
    }
    exchange.waited(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - sent));
    if (!exchange.done() && pump && !pump()) {
      if (const std::optional<std::string> open = exchange.abandon()) {
        closeRequest(connection.handle, *open);
        notes.push_back(std::format("File dialog: given up by application; request {} closed", *open));
      }
    }
  }
  return exchange.outcome();
}

}

std::expected<std::optional<std::filesystem::path>, std::string> showFileChooser(
  gui_linux::FileChooserKind kind, const gui_linux::FileChooserTexts& texts, const std::string& parentWindow,
  const DialogPump& pump, std::vector<std::string>& notes) {
  gui_linux::FileChooserAnswer answer = askOnce(kind, texts, parentWindow, pump, notes);
  if (!answer) {
    return std::unexpected(std::move(answer.error()));
  }
  if (!*answer) {
    return std::optional<std::filesystem::path>();
  }
  return std::optional<std::filesystem::path>(std::filesystem::path(**answer));
}

}

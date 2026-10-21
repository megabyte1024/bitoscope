/*
 * SPDX-License-Identifier: MIT
 */

#include "portal_file_chooser.h"

#include <cstddef>
#include <format>
#include <utility>

namespace bitoscope::gui_linux {

namespace {

constexpr std::string_view kSessionExtension = ".h5";
constexpr std::string_view kSessionGlob = "*.h5";
// WHY: Listed after kSessionGlob so a file named in capitals is listed too; two plain globs rather than "*.[hH]5"
// because a backend that appends the first glob's extension must still see "*.h5" first and plain.
constexpr std::string_view kSessionGlobCapitals = "*.H5";
constexpr std::string_view kAllFilesGlob = "*";
constexpr std::string_view kRequestPathPrefix = "/org/freedesktop/portal/desktop/request/";

char lowerAscii(char c) {
  return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c;
}

bool isAsciiLetter(char c) {
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

bool isAsciiDigit(char c) {
  return c >= '0' && c <= '9';
}

bool startsWithNoCase(std::string_view text, std::string_view prefix) {
  if (text.size() < prefix.size()) {
    return false;
  }
  for (std::size_t i = 0; i < prefix.size(); ++i) {
    if (lowerAscii(text[i]) != lowerAscii(prefix[i])) {
      return false;
    }
  }
  return true;
}

std::optional<unsigned> hexDigit(char c) {
  if (isAsciiDigit(c)) {
    return static_cast<unsigned>(c - '0');
  }
  const char lower = lowerAscii(c);
  if (lower >= 'a' && lower <= 'f') {
    return static_cast<unsigned>(lower - 'a' + 10);
  }
  return std::nullopt;
}

bool isObjectPathElement(std::string_view element) {
  if (element.empty()) {
    return false;
  }
  for (const char c : element) {
    if (!isAsciiLetter(c) && !isAsciiDigit(c) && c != '_') {
      return false;
    }
  }
  return true;
}

std::size_t utf8SequenceLength(std::string_view text) {
  const auto byte = [&text](std::size_t i) { return static_cast<unsigned char>(text[i]); };
  const auto continuation = [&](std::size_t i, unsigned char low, unsigned char high) {
    return i < text.size() && byte(i) >= low && byte(i) <= high;
  };
  const unsigned char lead = byte(0);
  if (lead < 0x80) {
    return 1;
  }
  if (lead >= 0xC2 && lead <= 0xDF) {
    return continuation(1, 0x80, 0xBF) ? 2 : 0;
  }
  if (lead >= 0xE0 && lead <= 0xEF) {
    const unsigned char low = lead == 0xE0 ? 0xA0 : 0x80;
    const unsigned char high = lead == 0xED ? 0x9F : 0xBF;
    return continuation(1, low, high) && continuation(2, 0x80, 0xBF) ? 3 : 0;
  }
  if (lead >= 0xF0 && lead <= 0xF4) {
    const unsigned char low = lead == 0xF0 ? 0x90 : 0x80;
    const unsigned char high = lead == 0xF4 ? 0x8F : 0xBF;
    return continuation(1, low, high) && continuation(2, 0x80, 0xBF) && continuation(3, 0x80, 0xBF) ? 4 : 0;
  }
  return 0;
}

FileFilter sessionFilter(std::string_view fileTypeName) {
  return FileFilter{std::format("{} ({})", dbusText(fileTypeName), kSessionGlob),
                    {FilterPattern{0, std::string(kSessionGlob)}, FilterPattern{0, std::string(kSessionGlobCapitals)}}};
}

std::string unusableUri(std::string_view uri, std::string_view why) {
  return std::format("file dialog's answer \"{}\" {}", uri, why);
}

}

std::expected<std::string, std::string> pathFromFileUri(std::string_view uri) {
  constexpr std::string_view kScheme = "file:";
  constexpr std::string_view kLocalHost = "localhost";
  if (!startsWithNoCase(uri, kScheme)) {
    return std::unexpected(unusableUri(uri, "is not file:// URI"));
  }
  std::string_view rest = uri.substr(kScheme.size());
  if (rest.starts_with("//")) {
    rest.remove_prefix(2);
    const std::size_t pathStart = rest.find('/');
    const std::string_view host = rest.substr(0, pathStart);
    if (!host.empty() && !(host.size() == kLocalHost.size() && startsWithNoCase(host, kLocalHost))) {
      return std::unexpected(unusableUri(uri, std::format("names file on another host (\"{}\")", host)));
    }
    rest = pathStart == std::string_view::npos ? std::string_view() : rest.substr(pathStart);
  }
  if (!rest.starts_with('/')) {
    return std::unexpected(unusableUri(uri, "is file:// URI without absolute path"));
  }
  std::string path;
  path.reserve(rest.size());
  for (std::size_t i = 0; i < rest.size(); ++i) {
    const char c = rest[i];
    if (c == '?' || c == '#') {
      return std::unexpected(unusableUri(uri, "has query or fragment, which no file has"));
    }
    if (c != '%') {
      path.push_back(c);
      continue;
    }
    const std::optional<unsigned> high = i + 1 < rest.size() ? hexDigit(rest[i + 1]) : std::nullopt;
    const std::optional<unsigned> low = i + 2 < rest.size() ? hexDigit(rest[i + 2]) : std::nullopt;
    if (!high || !low) {
      return std::unexpected(unusableUri(uri, "has malformed percent escape"));
    }
    const char decoded = static_cast<char>(*high * 16 + *low);
    if (decoded == '/' || decoded == '\0') {
      return std::unexpected(unusableUri(uri, "has escaped '/' or NUL, which no file name holds"));
    }
    path.push_back(decoded);
    i += 2;
  }
  return path;
}

bool hasFileExtension(std::string_view fileName) {
  const std::size_t dot = fileName.rfind('.');
  if (dot == std::string_view::npos || dot == 0 || dot + 1 == fileName.size()) {
    return false;
  }
  bool letter = false;
  for (const char c : fileName.substr(dot + 1)) {
    if (isAsciiLetter(c)) {
      letter = true;
    } else if (!isAsciiDigit(c)) {
      return false;
    }
  }
  return letter;
}

std::string withSessionExtension(std::string_view path) {
  const std::size_t slash = path.rfind('/');
  const std::string_view name = slash == std::string_view::npos ? path : path.substr(slash + 1);
  std::string out(path);
  if (name.empty() || hasFileExtension(name)) {
    return out;
  }
  out += name.ends_with('.') ? kSessionExtension.substr(1) : kSessionExtension;
  return out;
}

SaveAnswer saveAnswer(std::string_view chosenPath) {
  SaveAnswer answer;
  answer.path = withSessionExtension(chosenPath);
  answer.extensionAdded = answer.path != chosenPath;
  return answer;
}

bool saveNeedsReplaceQuestion(const SaveAnswer& answered, std::string_view path, bool pathExists) {
  return answered.extensionAdded && path == answered.path && pathExists;
}

FileChooserRequest fileChooserRequest(FileChooserKind kind, const FileChooserTexts& texts,
                                      std::string_view handleToken) {
  FileChooserRequest request;
  request.method = kind == FileChooserKind::Save ? "SaveFile" : "OpenFile";
  request.title = dbusText(texts.title);
  request.handleToken = std::string(handleToken);
  request.currentFilter = sessionFilter(texts.fileTypeName);
  request.filters.push_back(request.currentFilter);
  if (kind == FileChooserKind::Open) {
    request.filters.push_back(
      FileFilter{texts.allFilesName.empty() ? std::string(kAllFilesGlob) : dbusText(texts.allFilesName),
                 {FilterPattern{0, std::string(kAllFilesGlob)}}});
  }
  if (kind == FileChooserKind::Save && !texts.fileName.empty() &&
      texts.fileName.find('/') == std::string_view::npos) {
    request.currentName = dbusText(texts.fileName);
  }
  if (texts.folder.starts_with('/') && texts.folder.find('\0') == std::string_view::npos) {
    std::vector<std::uint8_t> bytes(texts.folder.begin(), texts.folder.end());
    bytes.push_back(0);  // WHY: The portal expects current_folder bytes to end with a NUL byte.
    request.currentFolder = std::move(bytes);
  }
  return request;
}

std::string dbusText(std::string_view text) {
  std::string out;
  out.reserve(text.size());
  while (!text.empty()) {
    const std::size_t length = text.front() == '\0' ? 0 : utf8SequenceLength(text);
    if (length == 0) {
      out.push_back('?');
      text.remove_prefix(1);
      continue;
    }
    out.append(text.substr(0, length));
    text.remove_prefix(length);
  }
  return out;
}

std::string handleToken(std::uint64_t unique) {
  return std::format("bitoscope_{:016x}", unique);
}

std::optional<std::string> requestObjectPath(std::string_view uniqueName, std::string_view token) {
  if (!uniqueName.starts_with(':')) {
    return std::nullopt;
  }
  std::string sender(uniqueName.substr(1));
  for (char& c : sender) {
    if (c == '.') {
      c = '_';
    }
  }
  if (!isObjectPathElement(sender) || !isObjectPathElement(token)) {
    return std::nullopt;
  }
  return std::format("{}{}/{}", kRequestPathPrefix, sender, token);
}

std::string responseMatchRule(std::string_view requestPath) {
  return std::format("type='signal',sender='org.freedesktop.portal.Desktop',"
                     "interface='org.freedesktop.portal.Request',member='Response',path='{}'",
                     requestPath);
}

std::string parentWindowIdentifier(std::optional<std::uint64_t> x11Window) {
  if (!x11Window || *x11Window == 0) {
    return {};
  }
  return std::format("x11:{:x}", *x11Window);
}

FileChooserAnswer fileChooserAnswer(std::uint32_t response, std::span<const std::string> uris) {
  if (response == 1) {
    return std::optional<std::string>();
  }
  if (response == 2) {
    return std::unexpected(std::string("file dialog ended without answer (desktop portal response 2)"));
  }
  if (response != 0) {
    return std::unexpected(std::format("file dialog ended with unknown desktop portal response {}", response));
  }
  if (uris.empty()) {
    return std::unexpected(std::string("file dialog's answer names no file"));
  }
  if (uris.size() != 1) {
    return std::unexpected(std::format("file dialog's answer names {} files where one was asked for", uris.size()));
  }
  auto path = pathFromFileUri(uris.front());
  if (!path) {
    return std::unexpected(std::move(path.error()));
  }
  return std::optional<std::string>(std::move(*path));
}

std::string fileChooserCallError(std::string_view errorName, std::string_view errorMessage) {
  constexpr std::string_view kDBusError = "org.freedesktop.DBus.Error.";
  const std::string_view kind = errorName.starts_with(kDBusError) ? errorName.substr(kDBusError.size()) : "";
  if (kind == "ServiceUnknown" || kind == "NameHasNoOwner" || kind.starts_with("Spawn.")) {
    return std::format("desktop portal (xdg-desktop-portal) is not running and could not be started; file "
                       "dialogs need it and its backend for this desktop ({})",
                       errorName);
  }
  if (kind == "UnknownMethod" || kind == "UnknownInterface" || kind == "UnknownObject") {
    return "desktop portal has no file chooser (org.freedesktop.portal.FileChooser); its backend for this "
           "desktop is missing (xdg-desktop-portal-kde, -gnome or -gtk)";
  }
  if (kind == "NoReply" || kind == "Timeout" || kind == "TimedOut") {
    return std::format("desktop portal (xdg-desktop-portal) did not answer ({})", errorName);
  }
  return std::format("desktop portal refused file dialog ({}: {})", errorName, errorMessage);
}

std::string noSessionBusError(std::string_view detail) {
  return std::format("no session D-Bus ({}); file dialogs need desktop portal (xdg-desktop-portal) on it",
                     detail);
}

FileChooserExchange::FileChooserExchange(std::string expectedRequestPath)
    : requestPath_(std::move(expectedRequestPath)) {}

std::optional<std::string> FileChooserExchange::callReturned(std::string_view requestPath) {
  if (done()) {
    return std::nullopt;
  }
  replied_ = true;
  if (requestPath == requestPath_) {
    return std::nullopt;
  }
  requestPath_ = std::string(requestPath);
  return requestPath_;
}

void FileChooserExchange::callFailed(std::string_view errorName, std::string_view errorMessage) {
  if (!done()) {
    outcome_ = std::unexpected(fileChooserCallError(errorName, errorMessage));
  }
}

void FileChooserExchange::responseArrived(std::string_view path, std::uint32_t response,
                                          std::span<const std::string> uris) {
  if (!done() && path == requestPath_) {
    outcome_ = fileChooserAnswer(response, uris);
  }
}

void FileChooserExchange::busLost() {
  if (!done()) {
    outcome_ = std::unexpected(std::string("session D-Bus connection was lost while file dialog was open"));
  }
}

void FileChooserExchange::waited(std::chrono::milliseconds sinceCall) {
  if (!done() && !replied_ && sinceCall >= kFileChooserReplyTimeout) {
    outcome_ = std::unexpected(std::format("desktop portal (xdg-desktop-portal) did not answer within {} s",
                                           kFileChooserReplyTimeout.count() / 1000));
  }
}

std::optional<std::string> FileChooserExchange::abandon() {
  if (done()) {
    return std::nullopt;
  }
  outcome_ = std::optional<std::string>();
  return requestPath_;
}

}

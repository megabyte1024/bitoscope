/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

// WHY: Subscribe to Response BEFORE calling SaveFile or OpenFile: the Request path is predictable and an
// answer could slip in.

#include <chrono>
#include <cstdint>
#include <expected>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace bitoscope::gui_linux {

std::expected<std::string, std::string> pathFromFileUri(std::string_view uri);

bool hasFileExtension(std::string_view fileName);

std::string withSessionExtension(std::string_view path);

struct SaveAnswer {
  std::string path;
  bool extensionAdded = false;
  bool operator==(const SaveAnswer&) const = default;
};
SaveAnswer saveAnswer(std::string_view chosenPath);

bool saveNeedsReplaceQuestion(const SaveAnswer& answered, std::string_view path, bool pathExists);

enum class FileChooserKind { Save, Open };

struct FilterPattern {
  std::uint32_t kind = 0;
  std::string pattern;
  bool operator==(const FilterPattern&) const = default;
};

struct FileFilter {
  std::string name;
  std::vector<FilterPattern> patterns;
  bool operator==(const FileFilter&) const = default;
};

struct FileChooserTexts {
  std::string_view title = {};
  std::string_view fileTypeName = {};
  std::string_view allFilesName = {};
  std::string_view folder = {};
  std::string_view fileName = {};
};

struct FileChooserRequest {
  std::string method;
  std::string title;
  std::string handleToken;
  std::vector<FileFilter> filters;
  FileFilter currentFilter;
  std::optional<std::string> currentName;
  std::optional<std::vector<std::uint8_t>> currentFolder;
  bool operator==(const FileChooserRequest&) const = default;
};

FileChooserRequest fileChooserRequest(FileChooserKind kind, const FileChooserTexts& texts,
                                      std::string_view handleToken);

std::string dbusText(std::string_view text);

std::string handleToken(std::uint64_t unique);

std::optional<std::string> requestObjectPath(std::string_view uniqueName, std::string_view token);

std::string responseMatchRule(std::string_view requestPath);

std::string parentWindowIdentifier(std::optional<std::uint64_t> x11Window);

using FileChooserAnswer = std::expected<std::optional<std::string>, std::string>;

FileChooserAnswer fileChooserAnswer(std::uint32_t response, std::span<const std::string> uris);

std::string fileChooserCallError(std::string_view errorName, std::string_view errorMessage);

std::string noSessionBusError(std::string_view detail);

inline constexpr std::chrono::milliseconds kFileChooserReplyTimeout{25000};

class FileChooserExchange {
 public:
  explicit FileChooserExchange(std::string expectedRequestPath);

  std::optional<std::string> callReturned(std::string_view requestPath);

  void callFailed(std::string_view errorName, std::string_view errorMessage);

  void responseArrived(std::string_view path, std::uint32_t response, std::span<const std::string> uris);

  void busLost();

  void waited(std::chrono::milliseconds sinceCall);

  std::optional<std::string> abandon();

  bool done() const { return outcome_.has_value(); }
  bool replied() const { return replied_; }
  const FileChooserAnswer& outcome() const { return *outcome_; }

 private:
  std::string requestPath_;
  bool replied_ = false;
  std::optional<FileChooserAnswer> outcome_;
};

}

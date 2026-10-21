/*
 * SPDX-License-Identifier: MIT
 */

#include "cli_annotation_filter.h"

#include <algorithm>
#include <cctype>

#include "annotation_role.h"
#include "display_text.h"

namespace bitoscope::cli {

bool AnnotationFilter::wants(std::size_t instance, uint32_t class_id) const {
  if (instance >= classes.size() || !classes[instance].has_value()) return true;
  return classes[instance]->contains(class_id);
}

std::string annotationClassWord(const decode::AnnotationClass& cls) {
  if (cls.name.empty()) {
    return cls.role == decode::AnnotationRole::None ? "data" : std::string(decode::annotationRoleName(cls.role));
  }
  std::string word;
  bool pending_separator = false;
  for (const char raw : cls.name) {
    const auto c = static_cast<unsigned char>(raw);
    if (std::isalnum(c) != 0) {
      if (pending_separator && !word.empty()) word += '_';
      pending_separator = false;
      word += static_cast<char>(std::tolower(c));
    } else {
      pending_separator = true;
    }
  }
  return word.empty() ? "data" : word;
}

std::expected<AnnotationClassRequest, std::string> parseAnnotationClassRequest(std::string_view text) {
  const std::size_t colon = text.find(':');
  if (colon == std::string_view::npos || colon == 0 || colon + 1 == text.size()) {
    return std::unexpected(
        "--annotation-classes needs ID:CLASS[,CLASS...], for example uart:start,framing_error");
  }
  AnnotationClassRequest request;
  request.id = std::string(text.substr(0, colon));
  std::string_view rest = text.substr(colon + 1);
  while (true) {
    const std::size_t comma = rest.find(',');
    const std::string_view word = rest.substr(0, comma);
    if (word.empty()) {
      return std::unexpected("-A '" + std::string(text) + "': empty class name");
    }
    request.words.emplace_back(word);
    if (comma == std::string_view::npos) break;
    rest.remove_prefix(comma + 1);
  }
  return request;
}

namespace {

std::string declaredList(const std::vector<decode::AnnotationClass>& classes) {
  std::string list;
  for (const decode::AnnotationClass& cls : classes) {
    list += (list.empty() ? "" : ", ") + annotationClassWord(cls) + " (" + std::to_string(cls.id) + ")";
  }
  return list;
}

}

std::expected<AnnotationFilter, std::string> resolveAnnotationFilter(
    std::span<const std::string> texts, std::span<const decode::ChainInstance> instances) {
  AnnotationFilter filter;
  filter.classes.resize(instances.size());
  for (const std::string& text : texts) {
    auto request = parseAnnotationClassRequest(text);
    if (!request) return std::unexpected(request.error());
    const auto found = std::find_if(instances.begin(), instances.end(),
                                    [&](const decode::ChainInstance& i) { return i.id == request->id; });
    if (found == instances.end()) {
      std::string ids;
      for (const decode::ChainInstance& i : instances) ids += (ids.empty() ? "" : ", ") + i.id;
      return std::unexpected("-A '" + text + "': no -P has id '" + request->id + "'; ids: " + ids);
    }
    const std::size_t index = static_cast<std::size_t>(found - instances.begin());
    const std::vector<decode::AnnotationClass> declared = found->decoder->annotationClasses();
    auto& slot = filter.classes[index];
    if (!slot.has_value()) slot.emplace();
    std::set<uint32_t>& wanted = *slot;
    for (const std::string& word : request->words) {
      bool matched = false;
      for (const decode::AnnotationClass& cls : declared) {
        if (annotationClassWord(cls) == word || std::to_string(cls.id) == word) {
          wanted.insert(cls.id);
          matched = true;
        }
      }
      if (!matched) {
        return std::unexpected("-A '" + text + "': " + display_text::displayName(found->decoder->name()) +
                               " declares no annotation class '" + word + "'; it declares: " +
                               declaredList(declared));
      }
    }
  }
  return filter;
}

}

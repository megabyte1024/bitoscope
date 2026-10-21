/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "decoder.h"
#include "decoder_chain.h"

namespace bitoscope::cli {

struct AnnotationFilter {
  std::vector<std::optional<std::set<uint32_t>>> classes;

  bool wants(std::size_t instance, uint32_t class_id) const;
};

std::string annotationClassWord(const decode::AnnotationClass& cls);

struct AnnotationClassRequest {
  std::string id;
  std::vector<std::string> words;
};
std::expected<AnnotationClassRequest, std::string> parseAnnotationClassRequest(std::string_view text);

std::expected<AnnotationFilter, std::string> resolveAnnotationFilter(
    std::span<const std::string> texts, std::span<const decode::ChainInstance> instances);

}

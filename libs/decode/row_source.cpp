/*
 * SPDX-License-Identifier: MIT
 */

#include "row_source.h"

#include <algorithm>
#include <cstddef>
#include <format>
#include <map>
#include <string_view>

#include "decoder.h"

namespace bitoscope::decode {

namespace {

bool isYesNo(const OptionSpec& o) {
  const std::vector<std::string>& v = o.allowed_values;
  return o.type == OptionType::String && v.size() == 2 && std::find(v.begin(), v.end(), "yes") != v.end() &&
         std::find(v.begin(), v.end(), "no") != v.end();
}

}

bool derivesAnnotations(const RowSource& source) {
  return std::holds_alternative<RowSource::BitGrid>(source.rule) ||
         std::holds_alternative<RowSource::ClockEdges>(source.rule);
}

std::expected<void, std::string> checkRowSources(const Decoder& decoder) {
  const std::vector<RowSource> sources = decoder.rowSources();
  if (sources.empty()) return {};
  const std::string name = decoder.name();
  const std::vector<AnnotationRow> rows = decoder.rows();
  const std::vector<AnnotationClass> classes = decoder.annotationClasses();
  const std::vector<OptionSpec> options = decoder.options();
  const std::vector<ChannelSpec> channels = decoder.channels();

  const auto refuse = [&name](const std::string& why) -> std::expected<void, std::string> {
    return std::unexpected(std::format("decoder '{}': {}", name, why));
  };
  const auto rowDeclared = [&rows](uint32_t id) {
    return std::any_of(rows.begin(), rows.end(), [id](const AnnotationRow& r) { return r.id == id; });
  };
  const auto classDeclared = [&classes](uint32_t id) {
    return std::any_of(classes.begin(), classes.end(), [id](const AnnotationClass& c) { return c.id == id; });
  };
  const auto option = [&options](std::string_view wanted) -> const OptionSpec* {
    const auto it =
        std::find_if(options.begin(), options.end(), [wanted](const OptionSpec& o) { return o.name == wanted; });
    return it == options.end() ? nullptr : &*it;
  };
  const auto optionOfType = [&option](std::string_view wanted, OptionType type) {
    const OptionSpec* o = option(wanted);
    return o != nullptr && o->type == type;
  };
  const auto channelDeclared = [&channels](std::string_view wanted) {
    return std::any_of(channels.begin(), channels.end(), [wanted](const ChannelSpec& c) { return c.name == wanted; });
  };

  std::map<uint32_t, uint32_t> sourceRowOf;
  for (const RowSource& s : sources) {
    if (std::holds_alternative<RowSource::Unknown>(s.rule)) continue;
    if (!rowDeclared(s.row)) {
      return refuse(std::format("row source names row {}, which decoder does not declare", s.row));
    }
    if (!sourceRowOf.emplace(s.row, s.source_row).second) return refuse(std::format("row {} has two sources", s.row));
    if (!classDeclared(s.cls)) {
      return refuse(std::format("row {} has content of class {}, which decoder does not declare", s.row, s.cls));
    }
    if (!rowDeclared(s.source_row)) {
      return refuse(std::format("row {} takes its content from row {}, which decoder does not declare", s.row,
                                s.source_row));
    }
    if (s.source_row == s.row) return refuse(std::format("row {} takes its content from itself", s.row));
    if (s.source_classes.empty()) {
      return refuse(std::format("row {} takes its content from no class of row {}", s.row, s.source_row));
    }
    for (const uint32_t c : s.source_classes) {
      if (!classDeclared(c)) {
        return refuse(std::format("row {} takes its content from class {} of row {}, which decoder does not declare",
                                  s.row, c, s.source_row));
      }
    }
    if (!s.enable_option.empty()) {
      const OptionSpec* enable = option(s.enable_option);
      if (enable == nullptr) {
        return refuse(std::format("row {} is switched on by option '{}', which decoder does not declare", s.row,
                                  s.enable_option));
      }
      if (!isYesNo(*enable)) {
        return refuse(std::format(
            "row {} is switched on by option '{}', which is not String option with values yes and no", s.row,
            s.enable_option));
      }
    }
    if (const auto* grid = std::get_if<RowSource::BitGrid>(&s.rule)) {
      if (!optionOfType(grid->rate_option, OptionType::Integer)) {
        return refuse(std::format("row {} takes its bit rate from '{}', which is not Integer option", s.row,
                                  grid->rate_option));
      }
      if (!optionOfType(grid->count_option, OptionType::Integer)) {
        return refuse(std::format("row {} takes its bit count from '{}', which is not Integer option", s.row,
                                  grid->count_option));
      }
      if (!optionOfType(grid->order_option, OptionType::String)) {
        return refuse(std::format("row {} takes its bit order from '{}', which is not String option", s.row,
                                  grid->order_option));
      }
    } else if (const auto* edges = std::get_if<RowSource::ClockEdges>(&s.rule)) {
      if (!channelDeclared(edges->clock_channel)) {
        return refuse(std::format("row {} reads clock channel '{}', which decoder does not have", s.row,
                                  edges->clock_channel));
      }
      if (!channelDeclared(edges->data_channel)) {
        return refuse(std::format("row {} reads data channel '{}', which decoder does not have", s.row,
                                  edges->data_channel));
      }
      if (!edges->count_option.empty()) {
        if (!optionOfType(edges->count_option, OptionType::Integer)) {
          return refuse(std::format("row {} takes its bit count from '{}', which is not Integer option", s.row,
                                    edges->count_option));
        }
      } else if (edges->count < 1 || edges->count > 64) {
        return refuse(std::format("row {} needs bit count of 1 to 64 or Integer option that sets it", s.row));
      }
    }
  }

  // WHY: Loop check goes by row id, in the same order as the Lua gateway, so both name the same row.
  for (const auto& [row, first] : sourceRowOf) {
    uint32_t at = first;
    for (std::size_t step = 0; step < sourceRowOf.size(); ++step) {
      if (at == row) return refuse(std::format("row {} takes its content from itself through row {}", row, first));
      const auto next = sourceRowOf.find(at);
      if (next == sourceRowOf.end()) break;
      at = next->second;
    }
  }
  return {};
}

}

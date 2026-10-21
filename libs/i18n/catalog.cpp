/*
 * SPDX-License-Identifier: MIT
 */

#include "catalog.h"

#include <algorithm>
#include <fstream>
#include <iterator>
#include <utility>

namespace bitoscope::i18n {

namespace {

constexpr uint32_t kMagic = 0x950412de;
constexpr char kContextSeparator = '\x04';
constexpr char kPluralSeparator = '\0';

uint32_t readU32LE(std::span<const uint8_t> data, std::size_t offset) {
  return static_cast<uint32_t>(data[offset]) |
         (static_cast<uint32_t>(data[offset + 1]) << 8) |
         (static_cast<uint32_t>(data[offset + 2]) << 16) |
         (static_cast<uint32_t>(data[offset + 3]) << 24);
}

uint32_t byteSwap32(uint32_t v) {
  return ((v & 0x000000ffu) << 24) | ((v & 0x0000ff00u) << 8) | ((v & 0x00ff0000u) >> 8) |
         ((v & 0xff000000u) >> 24);
}

struct StringRef {
  uint32_t length;
  uint32_t offset;
};

struct SplitOriginal {
  std::string_view context;
  std::string_view msgid;
  bool has_context = false;
};

SplitOriginal splitOriginal(std::string_view raw) {
  SplitOriginal result;
  std::string_view rest = raw;
  auto eot = raw.find(kContextSeparator);
  if (eot != std::string_view::npos) {
    result.context = raw.substr(0, eot);
    result.has_context = true;
    rest = raw.substr(eot + 1);
  }
  auto nul = rest.find(kPluralSeparator);
  result.msgid = (nul == std::string_view::npos) ? rest : rest.substr(0, nul);
  return result;
}

std::vector<std::string> splitForms(std::string_view raw) {
  std::vector<std::string> forms;
  std::size_t start = 0;
  for (;;) {
    auto nul = raw.find(kPluralSeparator, start);
    if (nul == std::string_view::npos) {
      forms.emplace_back(raw.substr(start));
      break;
    }
    forms.emplace_back(raw.substr(start, nul - start));
    start = nul + 1;
  }
  return forms;
}

std::string makeKey(std::string_view context, bool has_context, std::string_view msgid) {
  if (!has_context) return std::string(msgid);
  std::string key;
  key.reserve(context.size() + 1 + msgid.size());
  key.append(context);
  key.push_back(kContextSeparator);
  key.append(msgid);
  return key;
}

std::expected<std::vector<StringRef>, std::string> readStringTable(std::span<const uint8_t> data,
                                                                     uint64_t table_offset,
                                                                     uint64_t n, bool swap,
                                                                     std::string_view table_name) {
  std::vector<StringRef> refs;
  refs.reserve(static_cast<std::size_t>(n));

  uint64_t table_bytes = n * 8;
  if (table_offset > data.size() || table_bytes > data.size() - table_offset) {
    return std::unexpected(std::string(table_name) + " table runs past end of file");
  }

  for (uint64_t i = 0; i < n; ++i) {
    std::size_t entry_off = static_cast<std::size_t>(table_offset + i * 8);
    uint32_t length = readU32LE(data, entry_off);
    uint32_t offset = readU32LE(data, entry_off + 4);
    if (swap) {
      length = byteSwap32(length);
      offset = byteSwap32(offset);
    }

    // WHY: Compute in 64 bits: (offset=0xffffffff, length=2) must not wrap to a tiny, in-bounds end in 32 bits.
    uint64_t end = static_cast<uint64_t>(offset) + static_cast<uint64_t>(length);
    if (end >= data.size()) {
      return std::unexpected(std::string(table_name) + " string entry " +
                  std::to_string(i) + " runs past end of file");
    }
    if (data[static_cast<std::size_t>(end)] != 0) {
      return std::unexpected(std::string(table_name) + " string entry " +
                  std::to_string(i) + " is not NUL-terminated");
    }
    refs.push_back({length, offset});
  }

  std::vector<std::size_t> order(refs.size());
  for (std::size_t i = 0; i < order.size(); ++i) order[i] = i;
  std::sort(order.begin(), order.end(),
            [&](std::size_t a, std::size_t b) { return refs[a].offset < refs[b].offset; });
  for (std::size_t k = 1; k < order.size(); ++k) {
    const StringRef& prev = refs[order[k - 1]];
    const StringRef& cur = refs[order[k]];
    uint64_t prev_end = static_cast<uint64_t>(prev.offset) + prev.length + 1;
    if (cur.offset < prev_end) {
      return std::unexpected(std::string(table_name) + " has overlapping strings");
    }
  }

  return refs;
}

std::string_view stringAt(std::span<const uint8_t> data, const StringRef& ref) {
  return std::string_view(reinterpret_cast<const char*>(data.data()) + ref.offset, ref.length);
}

}

std::expected<Catalog, std::string> Catalog::parse(std::span<const uint8_t> data) {
  constexpr std::size_t kHeaderSize = 28;
  if (data.size() < kHeaderSize) {
    return std::unexpected(std::string("truncated .mo file: shorter than its header"));
  }

  uint32_t magic = readU32LE(data, 0);
  bool swap;
  if (magic == kMagic) {
    swap = false;
  } else if (magic == byteSwap32(kMagic)) {
    swap = true;
  } else {
    return std::unexpected(std::string("not .mo file: bad magic number"));
  }

  auto field = [&](std::size_t offset) {
    uint32_t v = readU32LE(data, offset);
    return swap ? byteSwap32(v) : v;
  };

  uint32_t revision = field(4);
  uint32_t nstrings = field(8);
  uint32_t orig_table_offset = field(12);
  uint32_t trans_table_offset = field(16);

  if ((revision >> 16) != 0) {
    return std::unexpected(std::string("unsupported .mo format revision ") +
                std::to_string(revision >> 16) + "." +
                std::to_string(revision & 0xffffu));
  }

  auto orig_refs = readStringTable(data, orig_table_offset, nstrings, swap, "original-string");
  if (!orig_refs) return std::unexpected(orig_refs.error());
  auto trans_refs =
    readStringTable(data, trans_table_offset, nstrings, swap, "translated-string");
  if (!trans_refs) return std::unexpected(trans_refs.error());

  Catalog catalog;
  std::string header_text;

  for (uint32_t i = 0; i < nstrings; ++i) {
    std::string_view orig_raw = stringAt(data, (*orig_refs)[i]);
    std::string_view trans_raw = stringAt(data, (*trans_refs)[i]);

    SplitOriginal split = splitOriginal(orig_raw);
    std::string key = makeKey(split.context, split.has_context, split.msgid);
    Entry entry;
    entry.forms = splitForms(trans_raw);

    if (key.empty()) {
      header_text = entry.forms.empty() ? std::string() : entry.forms.front();
    }

    catalog.entries_.emplace(std::move(key), std::move(entry));
  }

  constexpr std::string_view kPluralFormsKey = "Plural-Forms:";
  std::size_t line_start = 0;
  while (line_start <= header_text.size()) {
    std::size_t line_end = header_text.find('\n', line_start);
    std::size_t stop = (line_end == std::string::npos) ? header_text.size() : line_end;
    std::string_view line(header_text.data() + line_start, stop - line_start);
    if (line.substr(0, kPluralFormsKey.size()) == kPluralFormsKey) {
      std::string_view value = line.substr(kPluralFormsKey.size());
      while (!value.empty() && (value.front() == ' ' || value.front() == '\t')) {
        value.remove_prefix(1);
      }
      auto rule = PluralRule::parse(value);
      if (!rule) return std::unexpected(rule.error());
      catalog.plural_rule_ = *rule;
      break;
    }
    if (line_end == std::string::npos) break;
    line_start = line_end + 1;
  }

  return catalog;
}

std::string_view Catalog::translate(std::string_view msgid) const {
  auto it = entries_.find(std::string(msgid));
  if (it == entries_.end() || it->second.forms.empty() || it->second.forms.front().empty()) {
    return msgid;
  }
  return it->second.forms.front();
}

std::string_view Catalog::translate(std::string_view context, std::string_view msgid) const {
  std::string key = makeKey(context, true, msgid);
  auto it = entries_.find(key);
  if (it == entries_.end() || it->second.forms.empty() || it->second.forms.front().empty()) {
    return msgid;
  }
  return it->second.forms.front();
}

std::string_view Catalog::translatePlural(std::string_view msgid, std::string_view msgid_plural,
                                           unsigned long n) const {
  auto it = entries_.find(std::string(msgid));
  if (it == entries_.end() || it->second.forms.empty()) {
    return n == 1 ? msgid : msgid_plural;
  }
  const auto& forms = it->second.forms;
  std::size_t index = static_cast<std::size_t>(plural_rule_.evaluate(n));
  // WHY: Clamp: header nplurals may exceed the forms this entry stored, and the expression may go out of range.
  if (index >= forms.size()) index = forms.size() - 1;
  if (forms[index].empty()) return n == 1 ? msgid : msgid_plural;
  return forms[index];
}

std::expected<Catalog, std::string> loadCatalogFile(std::string_view path) {
  std::ifstream file(std::string(path), std::ios::binary);
  if (!file) {
    return std::unexpected(std::string("cannot open '") + std::string(path) + "'");
  }
  // WHY: Braced, not parenthesized: parentheses would parse as a function declaration (most vexing parse).
  std::vector<uint8_t> bytes{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
  if (file.bad()) {
    return std::unexpected(std::string("read error on '") + std::string(path) + "'");
  }
  return Catalog::parse(bytes);
}

}

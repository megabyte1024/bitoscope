/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <cstdint>
#include <expected>
#include <string>
#include <string_view>
#include <vector>

namespace bitoscope::i18n {

enum class PlNodeKind : uint8_t {
  Number,
  N,
  Not,
  Mul, Div, Mod,
  Add, Sub,
  Lt, Gt, Le, Ge,
  Eq, Ne,
  And, Or,
  Ternary,
};

struct PlNode {
  PlNodeKind kind = PlNodeKind::Number;
  unsigned long value = 0;
  std::size_t a = 0;
  std::size_t b = 0;
  std::size_t c = 0;
};

class PluralRule {
 public:
  static PluralRule germanicDefault();

  static std::expected<PluralRule, std::string> parse(std::string_view header_value);

  unsigned long evaluate(unsigned long n) const;

  unsigned long nplurals() const { return nplurals_; }

 private:
  PluralRule() = default;

  unsigned long evalNode(std::size_t index, unsigned long n) const;

  unsigned long nplurals_ = 2;
  std::vector<PlNode> nodes_;
  std::size_t root_ = 0;
};

}

/*
 * SPDX-License-Identifier: MIT
 */

#include "plural_rule.h"

#include <cctype>
#include <initializer_list>
#include <utility>

namespace bitoscope::i18n {

namespace {

enum class TokKind {
  Number, N, Not,
  Star, Slash, Percent,
  Plus, Minus,
  Lt, Gt, Le, Ge,
  EqEq, NotEq,
  AndAnd, OrOr,
  Question, Colon,
  LParen, RParen,
  End,
};

struct Token {
  TokKind kind;
  unsigned long value = 0;
};

std::expected<std::vector<Token>, std::string> tokenize(std::string_view expr) {
  std::vector<Token> tokens;
  std::size_t i = 0;
  const std::size_t n = expr.size();

  auto peek2 = [&](char a, char b) {
    return i + 1 < n && expr[i] == a && expr[i + 1] == b;
  };

  while (i < n) {
    const char c = expr[i];
    if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
      ++i;
      continue;
    }
    if (std::isdigit(static_cast<unsigned char>(c))) {
      unsigned long value = 0;
      while (i < n && std::isdigit(static_cast<unsigned char>(expr[i]))) {
        value = value * 10 + static_cast<unsigned long>(expr[i] - '0');
        ++i;
      }
      tokens.push_back({TokKind::Number, value});
      continue;
    }
    if (c == 'n') {
      tokens.push_back({TokKind::N});
      ++i;
      continue;
    }
    if (peek2('&', '&')) { tokens.push_back({TokKind::AndAnd}); i += 2; continue; }
    if (peek2('|', '|')) { tokens.push_back({TokKind::OrOr}); i += 2; continue; }
    if (peek2('=', '=')) { tokens.push_back({TokKind::EqEq}); i += 2; continue; }
    if (peek2('!', '=')) { tokens.push_back({TokKind::NotEq}); i += 2; continue; }
    if (peek2('<', '=')) { tokens.push_back({TokKind::Le}); i += 2; continue; }
    if (peek2('>', '=')) { tokens.push_back({TokKind::Ge}); i += 2; continue; }
    switch (c) {
      case '!': tokens.push_back({TokKind::Not}); ++i; continue;
      case '*': tokens.push_back({TokKind::Star}); ++i; continue;
      case '/': tokens.push_back({TokKind::Slash}); ++i; continue;
      case '%': tokens.push_back({TokKind::Percent}); ++i; continue;
      case '+': tokens.push_back({TokKind::Plus}); ++i; continue;
      case '-': tokens.push_back({TokKind::Minus}); ++i; continue;
      case '<': tokens.push_back({TokKind::Lt}); ++i; continue;
      case '>': tokens.push_back({TokKind::Gt}); ++i; continue;
      case '?': tokens.push_back({TokKind::Question}); ++i; continue;
      case ':': tokens.push_back({TokKind::Colon}); ++i; continue;
      case '(': tokens.push_back({TokKind::LParen}); ++i; continue;
      case ')': tokens.push_back({TokKind::RParen}); ++i; continue;
      default:
        return std::unexpected(
          "plural expression: unexpected character '" + std::string(1, c) +
          "' at offset " + std::to_string(i));
    }
  }
  tokens.push_back({TokKind::End});
  return tokens;
}

class ExprParser {
 public:
  explicit ExprParser(std::vector<Token> tokens) : tokens_(std::move(tokens)) {}

  std::expected<std::pair<std::vector<PlNode>, std::size_t>, std::string> parse() {
    auto root = parseTernary();
    if (!root) return std::unexpected(root.error());
    if (peek().kind != TokKind::End) {
      return std::unexpected(std::string("plural expression: trailing characters after "
                                           "complete expression"));
    }
    return std::make_pair(std::move(nodes_), *root);
  }

 private:
  const Token& peek() const { return tokens_[pos_]; }

  bool accept(TokKind k) {
    if (peek().kind == k) {
      ++pos_;
      return true;
    }
    return false;
  }

  std::size_t addNode(PlNode node) {
    nodes_.push_back(node);
    return nodes_.size() - 1;
  }

  using Result = std::expected<std::size_t, std::string>;

  Result parseTernary() {
    auto cond = parseOr();
    if (!cond) return cond;
    if (accept(TokKind::Question)) {
      auto then_branch = parseTernary();
      if (!then_branch) return then_branch;
      if (!accept(TokKind::Colon)) {
        return std::unexpected(std::string(
          "plural expression: expected ':' to complete ?: expression"));
      }
      auto else_branch = parseTernary();
      if (!else_branch) return else_branch;
      PlNode node;
      node.kind = PlNodeKind::Ternary;
      node.a = *cond;
      node.b = *then_branch;
      node.c = *else_branch;
      return addNode(node);
    }
    return cond;
  }

  Result parseBinaryLevel(Result (ExprParser::*next)(),
                           std::initializer_list<std::pair<TokKind, PlNodeKind>> ops) {
    auto left = (this->*next)();
    if (!left) return left;
    for (;;) {
      bool matched = false;
      for (auto [tok, kind] : ops) {
        if (peek().kind == tok) {
          ++pos_;
          auto right = (this->*next)();
          if (!right) return right;
          PlNode node;
          node.kind = kind;
          node.a = *left;
          node.b = *right;
          left = addNode(node);
          matched = true;
          break;
        }
      }
      if (!matched) return left;
    }
  }

  Result parseOr() {
    return parseBinaryLevel(&ExprParser::parseAnd, {{TokKind::OrOr, PlNodeKind::Or}});
  }
  Result parseAnd() {
    return parseBinaryLevel(&ExprParser::parseEquality, {{TokKind::AndAnd, PlNodeKind::And}});
  }
  Result parseEquality() {
    return parseBinaryLevel(&ExprParser::parseRelational,
                             {{TokKind::EqEq, PlNodeKind::Eq}, {TokKind::NotEq, PlNodeKind::Ne}});
  }
  Result parseRelational() {
    return parseBinaryLevel(&ExprParser::parseAdditive,
                             {{TokKind::Lt, PlNodeKind::Lt}, {TokKind::Gt, PlNodeKind::Gt},
                              {TokKind::Le, PlNodeKind::Le}, {TokKind::Ge, PlNodeKind::Ge}});
  }
  Result parseAdditive() {
    return parseBinaryLevel(&ExprParser::parseMultiplicative,
                             {{TokKind::Plus, PlNodeKind::Add}, {TokKind::Minus, PlNodeKind::Sub}});
  }
  Result parseMultiplicative() {
    return parseBinaryLevel(&ExprParser::parseUnary,
                             {{TokKind::Star, PlNodeKind::Mul}, {TokKind::Slash, PlNodeKind::Div},
                              {TokKind::Percent, PlNodeKind::Mod}});
  }

  Result parseUnary() {
    if (accept(TokKind::Not)) {
      auto operand = parseUnary();
      if (!operand) return operand;
      PlNode node;
      node.kind = PlNodeKind::Not;
      node.a = *operand;
      return addNode(node);
    }
    return parsePrimary();
  }

  Result parsePrimary() {
    if (peek().kind == TokKind::Number) {
      PlNode node;
      node.kind = PlNodeKind::Number;
      node.value = peek().value;
      ++pos_;
      return addNode(node);
    }
    if (accept(TokKind::N)) {
      PlNode node;
      node.kind = PlNodeKind::N;
      return addNode(node);
    }
    if (accept(TokKind::LParen)) {
      auto inner = parseTernary();
      if (!inner) return inner;
      if (!accept(TokKind::RParen)) {
        return std::unexpected(std::string("plural expression: expected ')'"));
      }
      return inner;
    }
    return std::unexpected(std::string("plural expression: expected term (n, number, "
                      "'!', or '(')"));
  }

  std::vector<Token> tokens_;
  std::size_t pos_ = 0;
  std::vector<PlNode> nodes_;
};

}

std::expected<PluralRule, std::string> PluralRule::parse(std::string_view header_value) {
  const std::string_view kNpluralsKey = "nplurals=";
  const std::string_view kPluralKey = "plural=";

  auto nplurals_pos = header_value.find(kNpluralsKey);
  if (nplurals_pos == std::string_view::npos) {
    return std::unexpected(std::string("Plural-Forms header missing 'nplurals='"));
  }
  std::size_t digits_start = nplurals_pos + kNpluralsKey.size();
  std::size_t digits_end = digits_start;
  while (digits_end < header_value.size() &&
         std::isdigit(static_cast<unsigned char>(header_value[digits_end]))) {
    ++digits_end;
  }
  if (digits_end == digits_start) {
    return std::unexpected(std::string("Plural-Forms header: 'nplurals=' has no digits"));
  }
  unsigned long nplurals = 0;
  for (std::size_t i = digits_start; i < digits_end; ++i) {
    nplurals = nplurals * 10 + static_cast<unsigned long>(header_value[i] - '0');
  }

  auto plural_pos = header_value.find(kPluralKey, digits_end);
  if (plural_pos == std::string_view::npos) {
    return std::unexpected(std::string("Plural-Forms header missing 'plural='"));
  }
  std::size_t expr_start = plural_pos + kPluralKey.size();
  std::size_t semicolon = header_value.find(';', expr_start);
  if (semicolon == std::string_view::npos) {
    return std::unexpected(
      std::string("Plural-Forms header: 'plural=' expression has no terminating ';'"));
  }
  std::string_view expr_text = header_value.substr(expr_start, semicolon - expr_start);

  auto tokens = tokenize(expr_text);
  if (!tokens) return std::unexpected(tokens.error());

  ExprParser parser(std::move(*tokens));
  auto parsed = parser.parse();
  if (!parsed) return std::unexpected(parsed.error());

  PluralRule rule;
  rule.nplurals_ = nplurals;
  rule.nodes_ = std::move(parsed->first);
  rule.root_ = parsed->second;
  return rule;
}

PluralRule PluralRule::germanicDefault() {
  // WHY: Built directly, not parsed from a string, so a parser bug cannot make the fallback rule unavailable.
  PluralRule rule;
  rule.nplurals_ = 2;
  PlNode n_node;
  n_node.kind = PlNodeKind::N;
  rule.nodes_.push_back(n_node);
  PlNode one_node;
  one_node.kind = PlNodeKind::Number;
  one_node.value = 1;
  rule.nodes_.push_back(one_node);
  PlNode ne_node;
  ne_node.kind = PlNodeKind::Ne;
  ne_node.a = 0;
  ne_node.b = 1;
  rule.nodes_.push_back(ne_node);
  rule.root_ = 2;
  return rule;
}

unsigned long PluralRule::evaluate(unsigned long n) const {
  return evalNode(root_, n);
}

unsigned long PluralRule::evalNode(std::size_t index, unsigned long n) const {
  const PlNode& node = nodes_[index];
  switch (node.kind) {
    case PlNodeKind::Number: return node.value;
    case PlNodeKind::N: return n;
    case PlNodeKind::Not: return evalNode(node.a, n) != 0 ? 0ul : 1ul;
    case PlNodeKind::Mul: return evalNode(node.a, n) * evalNode(node.b, n);
    case PlNodeKind::Div: {
      unsigned long divisor = evalNode(node.b, n);
      if (divisor == 0) return 0;
      return evalNode(node.a, n) / divisor;
    }
    case PlNodeKind::Mod: {
      unsigned long divisor = evalNode(node.b, n);
      if (divisor == 0) return 0;
      return evalNode(node.a, n) % divisor;
    }
    case PlNodeKind::Add: return evalNode(node.a, n) + evalNode(node.b, n);
    case PlNodeKind::Sub: return evalNode(node.a, n) - evalNode(node.b, n);
    case PlNodeKind::Lt: return evalNode(node.a, n) < evalNode(node.b, n) ? 1ul : 0ul;
    case PlNodeKind::Gt: return evalNode(node.a, n) > evalNode(node.b, n) ? 1ul : 0ul;
    case PlNodeKind::Le: return evalNode(node.a, n) <= evalNode(node.b, n) ? 1ul : 0ul;
    case PlNodeKind::Ge: return evalNode(node.a, n) >= evalNode(node.b, n) ? 1ul : 0ul;
    case PlNodeKind::Eq: return evalNode(node.a, n) == evalNode(node.b, n) ? 1ul : 0ul;
    case PlNodeKind::Ne: return evalNode(node.a, n) != evalNode(node.b, n) ? 1ul : 0ul;
    case PlNodeKind::And:
      return (evalNode(node.a, n) != 0 && evalNode(node.b, n) != 0) ? 1ul : 0ul;
    case PlNodeKind::Or:
      return (evalNode(node.a, n) != 0 || evalNode(node.b, n) != 0) ? 1ul : 0ul;
    case PlNodeKind::Ternary:
      return evalNode(node.a, n) != 0 ? evalNode(node.b, n) : evalNode(node.c, n);
  }
  return 0;
}

}

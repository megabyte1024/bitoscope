/*
 * SPDX-License-Identifier: MIT
 */

#include "annotation_format.h"

#include <cstdlib>
#include <format>

namespace bitoscope::decode {

namespace {

std::string formatHex(uint64_t value, uint32_t digits) { return std::format("0x{:0{}X}", value, digits); }

uint32_t hexDigits(const ValueFormat& format, const OptionMap& options) {
  if (format.hex_digits_option.empty()) return format.hex_min_digits;
  const auto it = options.find(format.hex_digits_option);
  const int64_t bits = it == options.end() ? 0 : std::strtoll(it->second.c_str(), nullptr, 10);
  return bits <= 0 ? 0 : static_cast<uint32_t>((bits + 3) / 4);
}

uint32_t byteDigits(const ValueFormat& format, const OptionMap& options) {
  int64_t bits = 0;
  if (!format.hex_digits_option.empty()) {
    const auto it = options.find(format.hex_digits_option);
    if (it != options.end()) bits = std::strtoll(it->second.c_str(), nullptr, 10);
  } else {
    bits = int64_t{4} * format.hex_min_digits;
  }
  if (bits <= 0) return 2;
  return static_cast<uint32_t>((bits + 7) / 8 * 2);
}

std::string styledHex(uint64_t value, uint32_t digits, HexStyle style) {
  std::string text = std::format("{:0{}X}", value, digits);
  if (text.size() % 2 != 0) text.insert(text.begin(), '0');
  switch (style) {
  case HexStyle::Prefixed:
    return "0x" + text;
  case HexStyle::Suffixed:
    return text + "h";
  case HexStyle::Bare:
    break;
  }
  return text;
}

std::string asciiText(uint8_t byte) {
  constexpr char kBackslash = 92;
  constexpr char kQuote = 39;
  if (byte == kQuote || byte == kBackslash) return std::string{kQuote, kBackslash, static_cast<char>(byte), kQuote};
  if (byte > 0x20 && byte < 0x7F) return std::string{kQuote, static_cast<char>(byte), kQuote};
  char escape = 0;
  switch (byte) {
  case 0x07: escape = 'a'; break;
  case 0x08: escape = 'b'; break;
  case 0x09: escape = 't'; break;
  case 0x0A: escape = 'n'; break;
  case 0x0B: escape = 'v'; break;
  case 0x0C: escape = 'f'; break;
  case 0x0D: escape = 'r'; break;
  default: break;
  }
  if (escape != 0) return std::string{kQuote, kBackslash, escape, kQuote};
  return std::format("[{:02X}]", byte);
}

std::string nOfMText(const AnnotationClass& cls, uint64_t value, const char* unit) {
  const auto n = static_cast<uint32_t>(value & 0xFFFFFFFFu);
  const auto m = static_cast<uint32_t>(value >> 32);
  return cls.name.empty() ? std::format("{} of {}", n, m) : std::format("{} ({} of {} {})", cls.name, n, m, unit);
}

std::string bytesText(std::span<const uint8_t> bytes, HexStyle style) {
  std::string text;
  for (const uint8_t byte : bytes) {
    if (!text.empty()) text += ' ';
    text += styledHex(byte, 2, style);
  }
  return text;
}

const AnnotationClass* classOf(const std::vector<AnnotationClass>& classes, uint32_t id) {
  for (const AnnotationClass& cls : classes) {
    if (cls.id == id) return &cls;
  }
  return nullptr;
}

std::string withName(const AnnotationClass& cls, const std::string& formatted) {
  if (formatted.empty()) return cls.name;
  if (cls.name.empty()) return formatted;
  return std::format("{}: {}", cls.name, formatted);
}

}

std::string formatAnnotation(const AnnotationClass& cls, uint64_t value, const OptionMap& options) {
  std::string formatted;
  switch (cls.format.kind) {
  case ValueFormatKind::None:
  case ValueFormatKind::Bytes:
    break;
  case ValueFormatKind::Hex:
    formatted = formatHex(value, hexDigits(cls.format, options));
    break;
  case ValueFormatKind::Address7:
    formatted = formatHex(value, 2);
    break;
  case ValueFormatKind::Decimal:
    formatted = std::format("{}", value);
    break;
  case ValueFormatKind::NOfM:
    return nOfMText(cls, value, "bits");
  case ValueFormatKind::NOfMBytes:
    return nOfMText(cls, value, "bytes");
  }
  return withName(cls, formatted);
}

std::string formatAnnotation(const AnnotationClass& cls, uint64_t value, const OptionMap& options,
                             const NumberFormat& format) {
  std::string formatted;
  switch (cls.format.kind) {
  case ValueFormatKind::None:
  case ValueFormatKind::Bytes:
    break;
  case ValueFormatKind::Hex:
    if (format.data == DataFormat::Dec) {
      formatted = std::format("{}", value);
    } else if (format.data == DataFormat::Ascii && value <= 0xFF) {
      formatted = asciiText(static_cast<uint8_t>(value));
    } else {
      // WHY: Value above 255 in an ASCII class is shown as hex: no single character stands for a 9-bit or 16-bit word.
      formatted = styledHex(value, byteDigits(cls.format, options), format.style);
    }
    break;
  case ValueFormatKind::Address7:
    formatted = format.data == DataFormat::Dec ? std::format("{}", value) : styledHex(value, 2, format.style);
    break;
  case ValueFormatKind::Decimal:
    formatted = std::format("{}", value);
    break;
  case ValueFormatKind::NOfM:
    return nOfMText(cls, value, "bits");
  case ValueFormatKind::NOfMBytes:
    return nOfMText(cls, value, "bytes");
  }
  return withName(cls, formatted);
}

std::string formatAnnotation(const std::vector<AnnotationClass>& classes, const Annotation& annotation,
                             const OptionMap& options) {
  for (const AnnotationClass& cls : classes) {
    if (cls.id == annotation.cls) return formatAnnotation(cls, annotation.value, options);
  }
  return std::format("<unknown annotation class {}>", annotation.cls);
}

std::string formatAnnotation(const std::vector<AnnotationClass>& classes, const Annotation& annotation,
                             const OptionMap& options, const NumberFormat& format) {
  for (const AnnotationClass& cls : classes) {
    if (cls.id == annotation.cls) return formatAnnotation(cls, annotation.value, options, format);
  }
  return std::format("<unknown annotation class {}>", annotation.cls);
}

bool takesDataFormat(const AnnotationClass& cls) {
  return cls.format.kind == ValueFormatKind::Hex || cls.format.kind == ValueFormatKind::Address7;
}

bool showsBytes(const std::vector<AnnotationClass>& classes, uint32_t cls) {
  const AnnotationClass* found = classOf(classes, cls);
  return found != nullptr && found->format.kind == ValueFormatKind::Bytes;
}

std::string formatAnnotation(const std::vector<AnnotationClass>& classes, const Annotation& annotation,
                             const OptionMap& options, std::span<const uint8_t> bytes) {
  const AnnotationClass* cls = classOf(classes, annotation.cls);
  if (cls == nullptr || cls->format.kind != ValueFormatKind::Bytes) return formatAnnotation(classes, annotation, options);
  return withName(*cls, bytesText(bytes, HexStyle::Bare));
}

std::string formatAnnotation(const std::vector<AnnotationClass>& classes, const Annotation& annotation,
                             const OptionMap& options, const NumberFormat& format, std::span<const uint8_t> bytes) {
  const AnnotationClass* cls = classOf(classes, annotation.cls);
  if (cls == nullptr || cls->format.kind != ValueFormatKind::Bytes) {
    return formatAnnotation(classes, annotation, options, format);
  }
  return withName(*cls, bytesText(bytes, format.style));
}

}

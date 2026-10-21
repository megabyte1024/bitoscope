/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include "decoder.h"
#include "number_format.h"

namespace bitoscope::decode {

std::string formatAnnotation(const AnnotationClass& cls, uint64_t value, const OptionMap& options = {});

std::string formatAnnotation(const AnnotationClass& cls, uint64_t value, const OptionMap& options,
                             const NumberFormat& format);

std::string formatAnnotation(const std::vector<AnnotationClass>& classes, const Annotation& annotation,
                             const OptionMap& options = {});
std::string formatAnnotation(const std::vector<AnnotationClass>& classes, const Annotation& annotation,
                             const OptionMap& options, const NumberFormat& format);

bool takesDataFormat(const AnnotationClass& cls);

bool showsBytes(const std::vector<AnnotationClass>& classes, uint32_t cls);
std::string formatAnnotation(const std::vector<AnnotationClass>& classes, const Annotation& annotation,
                             const OptionMap& options, std::span<const uint8_t> bytes);
std::string formatAnnotation(const std::vector<AnnotationClass>& classes, const Annotation& annotation,
                             const OptionMap& options, const NumberFormat& format, std::span<const uint8_t> bytes);

}

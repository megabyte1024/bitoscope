/*
 * SPDX-License-Identifier: MIT
 */

#include "svg_raster.h"

#include <plutosvg.h>

#include <climits>
#include <cstring>
#include <memory>
#include <new>

namespace bitoscope::svg_raster {

namespace {

struct DocumentDeleter {
  void operator()(plutosvg_document_t* d) const { plutosvg_document_destroy(d); }
};
struct SurfaceDeleter {
  void operator()(plutovg_surface_t* s) const { plutovg_surface_destroy(s); }
};

}

std::expected<RgbaImage, RasterError> rasterize(std::string_view svg, std::uint32_t pixelSize,
                                                Rgba8 currentColor) {
  if (pixelSize == 0) {
    return std::unexpected(RasterError::ZeroSize);
  }
  if (pixelSize > kMaxPixelSize) {
    return std::unexpected(RasterError::TooLarge);
  }
  if (svg.size() > static_cast<std::size_t>(INT_MAX)) {
    return std::unexpected(RasterError::DocumentTooLarge);
  }
  const std::unique_ptr<plutosvg_document_t, DocumentDeleter> document{
    plutosvg_document_load_from_data(svg.data(), static_cast<int>(svg.size()), -1, -1, nullptr,
                                     nullptr)};
  if (document == nullptr) {
    return std::unexpected(RasterError::Malformed);
  }

  const plutovg_color_t color{currentColor.r / 255.0f, currentColor.g / 255.0f,
                              currentColor.b / 255.0f, currentColor.a / 255.0f};
  const int size = static_cast<int>(pixelSize);
  const std::unique_ptr<plutovg_surface_t, SurfaceDeleter> surface{
    plutosvg_document_render_to_surface(document.get(), nullptr, size, size, &color, nullptr,
                                        nullptr)};
  if (surface == nullptr) {
    return std::unexpected(RasterError::RenderFailed);
  }

  unsigned char* data = plutovg_surface_get_data(surface.get());
  const int stride = plutovg_surface_get_stride(surface.get());
  plutovg_convert_argb_to_rgba(data, data, size, size, stride);

  RgbaImage image;
  image.width = pixelSize;
  image.height = pixelSize;
  const std::size_t rowBytes = static_cast<std::size_t>(pixelSize) * 4;
  try {
    image.pixels.resize(rowBytes * pixelSize);
  } catch (const std::bad_alloc&) {
    return std::unexpected(RasterError::OutOfMemory);
  }
  for (std::uint32_t y = 0; y < pixelSize; ++y) {
    std::memcpy(image.pixels.data() + y * rowBytes, data + static_cast<std::size_t>(y) * stride,
                rowBytes);
  }
  return image;
}

std::string_view describe(RasterError error) {
  switch (error) {
  case RasterError::ZeroSize:
    return "pixel size is 0";
  case RasterError::TooLarge:
    return "pixel size is above limit";
  case RasterError::DocumentTooLarge:
    return "SVG document is too large";
  case RasterError::Malformed:
    return "SVG document could not be loaded (not SVG, broken, or without size)";
  case RasterError::RenderFailed:
    return "SVG document could not be drawn";
  case RasterError::OutOfMemory:
    return "out of memory for pixels";
  }
  return "unknown error";
}

}

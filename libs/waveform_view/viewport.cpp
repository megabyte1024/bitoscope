/*
 * SPDX-License-Identifier: MIT
 */

#include "viewport.h"

#include <algorithm>
#include <cmath>

#include "fixed_time.h"

namespace bitoscope::waveform_view {

Viewport::Viewport(uint64_t sampleRateMilliHz, uint32_t widthPixels)
    : sampleRateMilliHz_(sampleRateMilliHz), widthPixels_(widthPixels) {}

double Viewport::marginSamples(uint64_t totalSamples) {
  return (std::max)(16.0, static_cast<double>(totalSamples) * Viewport::kZoomOutMarginFraction);
}

double Viewport::maxSamplesPerPixelFor(uint64_t totalSamples) const {
  if (widthPixels_ == 0) return kMinSamplesPerPixel;
  const double margin = marginSamples(totalSamples);
  const double widest = static_cast<double>(totalSamples) + 2.0 * margin;
  return (std::max)(kMinSamplesPerPixel, widest / static_cast<double>(widthPixels_));
}

Viewport Viewport::resampled(uint32_t widthPixels) const {
  Viewport out = *this;
  out.widthPixels_ = widthPixels;
  if (widthPixels != 0 && widthPixels_ != 0) {
    out.samplesPerPixel_ =
      samplesPerPixel_ * (static_cast<double>(widthPixels_) / static_cast<double>(widthPixels));
  }
  return out;
}

double Viewport::minFirstVisibleSample() const {
  return -(1.0 - kPanKeepFraction) * samplesPerPixel_ * static_cast<double>(widthPixels_);
}

double Viewport::maxFirstVisibleSample(uint64_t totalSamples) const {
  const double visibleSamples = samplesPerPixel_ * static_cast<double>(widthPixels_);
  return (std::max)(minFirstVisibleSample(), static_cast<double>(totalSamples) - kPanKeepFraction * visibleSamples);
}

void Viewport::clampToLimits(uint64_t totalSamples) {
  samplesPerPixel_ = (std::clamp)(samplesPerPixel_, kMinSamplesPerPixel, maxSamplesPerPixelFor(totalSamples));
  firstVisibleSample_ = (std::clamp)(firstVisibleSample_, minFirstVisibleSample(), maxFirstVisibleSample(totalSamples));
}

void Viewport::zoomAroundPixel(double factor, double pixelX, uint64_t totalSamples) {
  if (factor <= 0.0) return;
  const double anchorSample = pixelToSample(pixelX);
  samplesPerPixel_ *= factor;
  samplesPerPixel_ = (std::clamp)(samplesPerPixel_, kMinSamplesPerPixel, maxSamplesPerPixelFor(totalSamples));
  firstVisibleSample_ = anchorSample - pixelX * samplesPerPixel_;
  clampToLimits(totalSamples);
}

void Viewport::panPixels(double deltaPixels, uint64_t totalSamples) {
  firstVisibleSample_ += deltaPixels * samplesPerPixel_;
  clampToLimits(totalSamples);
}

void Viewport::zoomToFit(uint64_t totalSamples) {
  if (widthPixels_ == 0) return;
  const double exact = static_cast<double>(totalSamples) / static_cast<double>(widthPixels_);
  samplesPerPixel_ = (std::clamp)(exact, kMinSamplesPerPixel, maxSamplesPerPixelFor(totalSamples));
  firstVisibleSample_ = 0.0;
  clampToLimits(totalSamples);
}

void Viewport::zoomTo(double samplesPerPixel, double sampleZeroPixel, uint64_t totalSamples) {
  samplesPerPixel_ = (std::clamp)(samplesPerPixel, kMinSamplesPerPixel, maxSamplesPerPixelFor(totalSamples));
  firstVisibleSample_ = -sampleZeroPixel * samplesPerPixel_;
  clampToLimits(totalSamples);
}

void Viewport::zoom1to1(double pixelX, uint64_t totalSamples) {
  const double anchorSample = pixelToSample(pixelX);
  samplesPerPixel_ = 1.0;
  firstVisibleSample_ = anchorSample - pixelX;
  clampToLimits(totalSamples);
}

double Viewport::samplePixelX(int64_t sample) const {
  const auto columnBegin = [this](int64_t column) {
    return static_cast<int64_t>(std::floor(pixelToSample(static_cast<double>(column))));
  };
  int64_t j = static_cast<int64_t>(std::floor(sampleToPixel(static_cast<double>(sample))));
  while (columnBegin(j) < sample) ++j;
  while (columnBegin(j - 1) >= sample) --j;
  if (columnBegin(j) > sample) --j;
  return static_cast<double>(j) + 0.5;
}

double overlayColumnX(double traceLeft, double framebufferScale, double columnX) {
  return (std::round(traceLeft * framebufferScale) + columnX) / framebufferScale;
}

double filledEdge(double edge, double framebufferScale) {
  return std::ceil(edge * framebufferScale - 0.5) / framebufferScale;
}

int64_t Viewport::sampleToTimePicoseconds(int64_t sample) const {
  return sampleTimePicoseconds(sample - timeOriginSample_, sampleRateMilliHz_);
}

int64_t Viewport::timePicosecondsToSample(int64_t picoseconds) const {
  return samplesFromPicoseconds(picoseconds, sampleRateMilliHz_) + timeOriginSample_;
}

}

/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <cstdint>

namespace bitoscope::waveform_view {

class Viewport {
 public:
  static constexpr double kMinSamplesPerPixel = 1.0 / 64.0;

  static constexpr double kZoomOutMarginFraction = 0.05;

  static constexpr double kPanKeepFraction = 0.1;

  Viewport(uint64_t sampleRateMilliHz, uint32_t widthPixels);

  uint32_t widthPixels() const { return widthPixels_; }
  void setWidthPixels(uint32_t widthPixels) { widthPixels_ = widthPixels; }

  uint64_t sampleRateMilliHz() const { return sampleRateMilliHz_; }

  double firstVisibleSample() const { return firstVisibleSample_; }
  double samplesPerPixel() const { return samplesPerPixel_; }

  void clampToLimits(uint64_t totalSamples);

  double minFirstVisibleSample() const;
  double maxFirstVisibleSample(uint64_t totalSamples) const;

  void zoomAroundPixel(double factor, double pixelX, uint64_t totalSamples);

  void panPixels(double deltaPixels, uint64_t totalSamples);

  void zoomToFit(uint64_t totalSamples);

  void zoomTo(double samplesPerPixel, double sampleZeroPixel, uint64_t totalSamples);

  void zoom1to1(double pixelX, uint64_t totalSamples);

  Viewport resampled(uint32_t widthPixels) const;

  double sampleToPixel(double sample) const { return (sample - firstVisibleSample_) / samplesPerPixel_; }
  double pixelToSample(double pixelX) const { return firstVisibleSample_ + pixelX * samplesPerPixel_; }

  double samplePixelX(int64_t sample) const;

  void setTimeOriginSample(int64_t sample) { timeOriginSample_ = sample; }
  int64_t timeOriginSample() const { return timeOriginSample_; }

  int64_t sampleToTimePicoseconds(int64_t sample) const;
  int64_t timePicosecondsToSample(int64_t picoseconds) const;

  static double marginSamples(uint64_t totalSamples);

 private:
  double maxSamplesPerPixelFor(uint64_t totalSamples) const;

  uint64_t sampleRateMilliHz_;
  uint32_t widthPixels_;
  double firstVisibleSample_ = 0.0;
  double samplesPerPixel_ = 1.0;
  int64_t timeOriginSample_ = 0;
};

double overlayColumnX(double traceLeft, double framebufferScale, double columnX);

double filledEdge(double edge, double framebufferScale);

}

/*
 * SPDX-License-Identifier: MIT
 */

#include "time_ruler.h"

#include <algorithm>
#include <cmath>
#include <string>

#include "fixed_time.h"
#include "viewport.h"

namespace bitoscope::waveform_view {

namespace {

int64_t chooseStep(double minStepPs) {
  if (minStepPs <= 1.0) return 1;
  int64_t decade = 1;
  while (true) {
    for (const int lead : {1, 2, 5}) {
      const int64_t candidate = lead * decade;
      if (static_cast<double>(candidate) >= minStepPs) return candidate;
    }
    decade *= 10;
  }
}

struct RulerUnit {
  int64_t picoseconds;
  const char* name;
};

RulerUnit unitForStep(int64_t stepPs) {
  static constexpr RulerUnit kUnits[] = {
    {1'000'000'000'000, "s"}, {1'000'000'000, "ms"}, {1'000'000, "us"}, {1'000, "ns"}, {1, "ps"}};
  for (const RulerUnit& unit : kUnits) {
    if (stepPs >= unit.picoseconds) return unit;
  }
  return kUnits[4];
}

std::string labelInUnit(int64_t timePs, const RulerUnit& unit) {
  if (timePs == 0) return "0";
  return std::to_string(timePs / unit.picoseconds) + " " + unit.name;
}

constexpr double kLabelGapPixels = 8.0;

double widestLabelWidthForStep(int64_t stepPs, double firstTimeApprox, double lastTimeApprox,
                                const LabelWidthFn& measureLabelWidth) {
  const RulerUnit unit = unitForStep(stepPs);
  const int64_t kLo = static_cast<int64_t>(std::floor(firstTimeApprox / static_cast<double>(stepPs)));
  const int64_t kHi = static_cast<int64_t>(std::ceil(lastTimeApprox / static_cast<double>(stepPs)));
  const double widthLo = measureLabelWidth(labelInUnit(kLo * stepPs, unit));
  const double widthHi = measureLabelWidth(labelInUnit(kHi * stepPs, unit));
  return (std::max)(widthLo, widthHi);
}

// WHY: Major spacing is 2 * widest label + gap: end labels shift inward by up to half a width each.
int64_t chooseMajorStep(double psPerPixel, double minMajorSpacingPixels, double firstTimeApprox,
                         double lastTimeApprox, const LabelWidthFn& measureLabelWidth) {
  int64_t stepPs = chooseStep(psPerPixel * minMajorSpacingPixels);
  if (!measureLabelWidth) return stepPs;
  for (int guard = 0; guard < 64; ++guard) {
    const double spacingPixels = static_cast<double>(stepPs) / psPerPixel;
    const double widestLabel =
      widestLabelWidthForStep(stepPs, firstTimeApprox, lastTimeApprox, measureLabelWidth);
    const double required = (std::max)(minMajorSpacingPixels, 2.0 * widestLabel + kLabelGapPixels);
    if (spacingPixels >= required) return stepPs;
    stepPs = chooseStep(static_cast<double>(stepPs) + 1.0);
  }
  return stepPs;
}

}

std::vector<RulerTick> buildTimeRuler(const Viewport& viewport, double minMajorSpacingPixels,
                                       int minorTicksPerMajor, const LabelWidthFn& measureLabelWidth) {
  std::vector<RulerTick> ticks;
  const uint32_t width = viewport.widthPixels();
  const uint64_t rate = viewport.sampleRateMilliHz();
  if (width == 0 || rate == 0 || viewport.samplesPerPixel() <= 0.0 || minMajorSpacingPixels <= 0.0) {
    return ticks;
  }

  const double periodPsApprox = 1.0e15 / static_cast<double>(rate);
  const double psPerPixel = viewport.samplesPerPixel() * periodPsApprox;

  const double firstSample = viewport.firstVisibleSample() - static_cast<double>(viewport.timeOriginSample());
  const double lastSample = firstSample + viewport.samplesPerPixel() * static_cast<double>(width);
  const double firstTimeApprox = firstSample * periodPsApprox;
  const double lastTimeApprox = lastSample * periodPsApprox;

  const int64_t majorStepPs =
    chooseMajorStep(psPerPixel, minMajorSpacingPixels, firstTimeApprox, lastTimeApprox, measureLabelWidth);
  const int64_t minorStepPs = minorTicksPerMajor > 1 ? majorStepPs / minorTicksPerMajor : 0;
  const RulerUnit unit = unitForStep(majorStepPs);

  const int64_t kStart = static_cast<int64_t>(std::floor(firstTimeApprox / static_cast<double>(majorStepPs))) - 1;
  const int64_t kEnd = static_cast<int64_t>(std::ceil(lastTimeApprox / static_cast<double>(majorStepPs))) + 1;

  for (int64_t k = kStart; k <= kEnd; ++k) {
    const int64_t majorTimePs = k * majorStepPs;
    const int64_t majorSample = viewport.timePicosecondsToSample(majorTimePs);
    const double majorPixel = viewport.samplePixelX(majorSample);
    if (majorPixel >= -1.0 && majorPixel <= static_cast<double>(width) + 1.0) {
      RulerTick tick;
      tick.pixelX = majorPixel;
      tick.sample = majorSample;
      tick.major = true;
      tick.label = labelInUnit(majorTimePs, unit);
      ticks.push_back(tick);
    }
    if (minorStepPs > 0) {
      for (int m = 1; m < minorTicksPerMajor; ++m) {
        const int64_t minorTimePs = majorTimePs + static_cast<int64_t>(m) * minorStepPs;
        const int64_t minorSample = viewport.timePicosecondsToSample(minorTimePs);
        const double minorPixel = viewport.samplePixelX(minorSample);
        if (minorPixel >= -1.0 && minorPixel <= static_cast<double>(width) + 1.0) {
          RulerTick tick;
          tick.pixelX = minorPixel;
          tick.sample = minorSample;
          tick.major = false;
          ticks.push_back(tick);
        }
      }
    }
  }

  if (measureLabelWidth) {
    const double widthPx = static_cast<double>(width);
    for (RulerTick& tick : ticks) {
      if (!tick.major || tick.label.empty()) continue;
      const double labelWidth = measureLabelWidth(tick.label);
      double left = tick.pixelX - labelWidth * 0.5;
      if (labelWidth <= widthPx) {
        left = std::clamp(left, 0.0, widthPx - labelWidth);
      }
      tick.labelPixelX = left;
    }
  }

  return ticks;
}

}

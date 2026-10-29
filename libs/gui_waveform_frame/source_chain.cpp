/*
 * SPDX-License-Identifier: MIT
 */

#include "source_chain.h"

#include <algorithm>

#include "capture_outcome.h"

namespace bitoscope::gui_waveform_frame {

SourceChange WaveformSourceChain::sync(const SourceChoice& choice) {
  if (choice.live) return syncLive(*choice.live);
  if (choice.kept && choice.kept->buffer) return syncKept(*choice.kept);
  return syncIdle(choice.idle);
}

void WaveformSourceChain::drop() {
  source_ = nullptr;
  extent_.reset();
  tailed_.reset();
  tail_ = {};
  store_.reset();
  empty_.reset();
  session_.reset();
  keptBuffer_.reset();
  shows_ = Shows::Nothing;
  liveGeneration_ = 0;
  timeZero_ = 0;
  laneDevice_.clear();
}

bool WaveformSourceChain::snapshotAdvanced() {
  if (!session_) return true;
  const uint64_t generation = session_->snapshot().generation;
  if (observedSnapshotGeneration_ == generation) return false;
  observedSnapshotGeneration_ = generation;
  return true;
}

SourceChange WaveformSourceChain::syncLive(const LiveSource& live) {
  SourceChange change = SourceChange::None;
  if (shows_ != Shows::Live || liveGeneration_ != live.generation) {
    drop();
    shows_ = Shows::Live;
    liveGeneration_ = live.generation;
    requestedSamples_ = live.requestedSamples;
    laneDevice_.assign(live.deviceChannels.begin(), live.deviceChannels.end());
    session_ = live.session;
    observedSnapshotGeneration_.reset();
    if (session_) {
      store_ = std::make_unique<waveform_capture_source::CaptureWaveformSource>(session_->buffer());
      const waveform_view::WaveformSource* stored = store_.get();
      if (session_->publishesTail()) {
        tailed_ = std::make_unique<waveform_capture_source::LiveTailWaveformSource>(*store_, tail_);
        stored = tailed_.get();
      }
      extent_ = std::make_unique<waveform_view::ExtentWaveformSource>(*stored, 0, 0);
      source_ = extent_.get();
    } else {
      empty_ = std::make_unique<waveform_view::EmptyWaveformSource>(static_cast<uint32_t>(laneDevice_.size()),
                                                                    live.sampleRateMilliHz);
      source_ = empty_.get();
    }
    change = SourceChange::ReplacedCapture;
  }
  if (session_ && extent_) {
    const capture::CaptureSnapshot snapshot = tailed_ ? session_->snapshot(tail_) : session_->snapshot();
    extent_->setExtent(snapshot.extent.first_sample, snapshot.extent.count + tail_.count);
    timeZero_ = gui_capture::timeZeroSample(snapshot.trigger_origin, snapshot.trigger_sample_index);
  }
  return change;
}

SourceChange WaveformSourceChain::syncKept(const KeptSource& kept) {
  if (shows_ == Shows::Kept && keptBuffer_ == kept.buffer) {
    if (keptExtent_ == kept.extent && timeZero_ == kept.timeZero && requestedSamples_ == kept.requestedSamples) {
      return SourceChange::None;
    }
    keptExtent_ = kept.extent;
    extent_->setExtent(kept.extent.first_sample, kept.extent.count);
    timeZero_ = kept.timeZero;
    requestedSamples_ = kept.requestedSamples;
    return SourceChange::NewExtent;
  }
  drop();
  shows_ = Shows::Kept;
  session_ = kept.session;
  keptBuffer_ = kept.buffer;
  requestedSamples_ = kept.requestedSamples;
  laneDevice_.assign(kept.deviceChannels.begin(), kept.deviceChannels.end());
  store_ = std::make_unique<waveform_capture_source::CaptureWaveformSource>(*keptBuffer_);
  keptExtent_ = kept.extent;
  extent_ = std::make_unique<waveform_view::ExtentWaveformSource>(*store_, kept.extent.first_sample, kept.extent.count);
  source_ = extent_.get();
  timeZero_ = kept.timeZero;
  return SourceChange::ReplacedCapture;
}

SourceChange WaveformSourceChain::syncIdle(const IdleSource& idle) {
  const bool sameLanes = std::ranges::equal(laneDevice_, idle.devices);
  if (shows_ == Shows::Nothing && empty_ && sameLanes && empty_->sampleRateMilliHz() == idle.sampleRateMilliHz &&
      requestedSamples_ == idle.requestedSamples) {
    return SourceChange::None;
  }
  drop();
  laneDevice_.assign(idle.devices.begin(), idle.devices.end());
  empty_ = std::make_unique<waveform_view::EmptyWaveformSource>(static_cast<uint32_t>(laneDevice_.size()),
                                                                idle.sampleRateMilliHz);
  source_ = empty_.get();
  requestedSamples_ = idle.requestedSamples;
  return SourceChange::ReplacedIdle;
}

}

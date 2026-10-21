/*
 * SPDX-License-Identifier: MIT
 */

#include "item_feed.h"

#include <utility>

namespace bitoscope::decoder_host {

InputItemFeed::InputItemFeed(std::string input, decode::StreamInput& source, uint32_t batch)
    : input_(std::move(input)), source_(source), batch_(batch == 0 ? 1 : batch) {}

std::expected<ipc::decoder::ItemFeed, std::string> InputItemFeed::next() {
  if (ended_) {
    return std::unexpected("decoder plugin asked for items of input '" + input_ + "' after their last batch");
  }
  ipc::decoder::ItemFeed feed;
  feed.input = input_;
  feed.items.reserve(batch_);
  const auto take = [&feed](const decode::StreamItem& i) {
    feed.items.push_back({i.begin, i.end, i.value, i.kind, i.flags});
  };
  if (ahead_) {
    take(*ahead_);
    ahead_.reset();
  }
  while (feed.items.size() < batch_) {
    const std::optional<decode::StreamItem> item = source_.next();
    if (!item) break;
    take(*item);
  }
  if (feed.items.size() == batch_) ahead_ = source_.next();
  feed.last = !ahead_.has_value();
  ended_ = feed.last;
  fed_ += feed.items.size();
  return feed;
}

}

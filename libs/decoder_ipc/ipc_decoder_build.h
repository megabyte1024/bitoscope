/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <algorithm>
#include <cstdint>
#include <string_view>

#include <capnp/message.h>

#include "decoder.capnp.h"

namespace bitoscope::ipc::decoder {

inline wire::decoder::DecoderEvent::Builder eventRoot(capnp::MessageBuilder& message, uint64_t jobId,
                                                      uint64_t settled) {
  auto root = message.initRoot<wire::decoder::DecoderEvent>();
  root.setJobId(jobId);
  root.setSettled(settled);
  return root;
}

template <class Annotations, class ValueOf>
void buildAnnotationEvent(capnp::MessageBuilder& message, uint64_t jobId, uint64_t settled,
                          const Annotations& batch, const ValueOf& valueOf) {
  // WHY: Initialized even when empty: the list is what selects the union member.
  auto out = eventRoot(message, jobId, settled).initAnnotations(static_cast<unsigned>(batch.size()));
  for (unsigned i = 0; i < out.size(); ++i) {
    const auto& a = batch[i];
    auto element = out[i];
    element.setBegin(a.begin);
    element.setEnd(a.end);
    element.setRow(a.row);
    element.setCls(a.cls);
    element.setValue(valueOf(a));
  }
}

template <class Items>
void buildItems(capnp::List<wire::decoder::StreamItem>::Builder out, const Items& items) {
  for (unsigned i = 0; i < out.size(); ++i) {
    const auto& item = items[i];
    auto element = out[i];
    element.setBegin(item.begin);
    element.setEnd(item.end);
    element.setValue(item.value);
    element.setKind(item.kind);
    element.setFlags(item.flags);
  }
}

template <class Items>
void buildItemEvent(capnp::MessageBuilder& message, uint64_t jobId, uint64_t settled, std::string_view output,
                    const Items& items) {
  auto out = eventRoot(message, jobId, settled).initItems();
  if (!output.empty()) {
    auto name = out.initOutput(static_cast<unsigned>(output.size()));
    std::copy(output.begin(), output.end(), name.begin());
  }
  buildItems(out.initItems(static_cast<unsigned>(items.size())), items);
}

}

/*
 * SPDX-License-Identifier: MIT
 */

#include "decoder_plugin_pool.h"

#include <algorithm>
#include <mutex>
#include <utility>
#include <vector>

namespace bitoscope::decoder_host {

struct MemberPool::Home {
  struct Slot {
    Member member;
    bool busy = false;
  };

  Start start;
  Ended ended;
  size_t max_idle = 0;

  std::mutex mutex;
  std::vector<Slot> slots;
  uint64_t started = 0;
  bool closed = false;

  size_t idleCount() const {
    return static_cast<size_t>(std::count_if(slots.begin(), slots.end(), [](const Slot& slot) { return !slot.busy; }));
  }
};

MemberPool::Lease::Lease(std::shared_ptr<Home> home, Member member) : home_(std::move(home)), member_(std::move(member)) {}

MemberPool::Lease::~Lease() { giveBack(); }

MemberPool::Lease::Lease(Lease&& other) noexcept : home_(std::move(other.home_)), member_(std::move(other.member_)) {}

MemberPool::Lease& MemberPool::Lease::operator=(Lease&& other) noexcept {
  if (this != &other) {
    giveBack();
    home_ = std::move(other.home_);
    member_ = std::move(other.member_);
  }
  return *this;
}

void MemberPool::Lease::giveBack() {
  if (!member_) return;
  Member dropped;
  {
    std::lock_guard lock(home_->mutex);
    const auto slot = std::find_if(home_->slots.begin(), home_->slots.end(),
                                   [this](const Home::Slot& s) { return s.member == member_; });
    if (slot != home_->slots.end()) {
      slot->busy = false;
      if (home_->closed || home_->ended(member_) || home_->idleCount() > home_->max_idle) {
        dropped = std::move(slot->member);
        home_->slots.erase(slot);
      }
    }
  }
  member_.reset();
  home_.reset();
}

MemberPool::MemberPool(std::shared_ptr<Home> home) : home_(std::move(home)) {}

MemberPool::~MemberPool() { end(); }

MemberPool::MemberPool(MemberPool&& other) noexcept : home_(std::move(other.home_)) {}

MemberPool& MemberPool::operator=(MemberPool&& other) noexcept {
  if (this != &other) {
    end();
    home_ = std::move(other.home_);
  }
  return *this;
}

void MemberPool::end() {
  if (!home_) return;
  std::vector<Member> dropped;
  {
    std::lock_guard lock(home_->mutex);
    home_->closed = true;
    std::erase_if(home_->slots, [&dropped](Home::Slot& slot) {
      if (slot.busy) return false;
      dropped.push_back(std::move(slot.member));
      return true;
    });
  }
  home_.reset();
}

std::expected<MemberPool, std::string> MemberPool::create(Start start, Ended ended, size_t max_idle) {
  auto home = std::make_shared<Home>();
  home->start = std::move(start);
  home->ended = std::move(ended);
  home->max_idle = max_idle;
  auto first = home->start();
  if (!first) return std::unexpected(std::move(first.error()));
  home->slots.push_back({std::move(*first), false});
  home->started = 1;
  return MemberPool(std::move(home));
}

std::expected<MemberPool::Lease, std::string> MemberPool::take() {
  std::vector<Member> dropped;
  {
    std::lock_guard lock(home_->mutex);
    for (auto slot = home_->slots.begin(); slot != home_->slots.end();) {
      if (slot->busy) {
        ++slot;
        continue;
      }
      if (home_->ended(slot->member)) {
        dropped.push_back(std::move(slot->member));
        slot = home_->slots.erase(slot);
        continue;
      }
      slot->busy = true;
      return Lease(home_, slot->member);
    }
  }
  auto made = home_->start();
  if (!made) return std::unexpected(std::move(made.error()));
  std::lock_guard lock(home_->mutex);
  ++home_->started;
  home_->slots.push_back({*made, true});
  return Lease(home_, std::move(*made));
}

std::expected<MemberPool::Member, std::string> MemberPool::anyLive() {
  std::vector<Member> dropped;
  {
    std::lock_guard lock(home_->mutex);
    Member busy_one;
    for (auto slot = home_->slots.begin(); slot != home_->slots.end();) {
      const bool ended = home_->ended(slot->member);
      if (!slot->busy && ended) {
        dropped.push_back(std::move(slot->member));
        slot = home_->slots.erase(slot);
        continue;
      }
      if (!ended) {
        if (!slot->busy) return slot->member;
        if (!busy_one) busy_one = slot->member;
      }
      ++slot;
    }
    if (busy_one) return busy_one;
  }
  auto made = home_->start();
  if (!made) return std::unexpected(std::move(made.error()));
  std::lock_guard lock(home_->mutex);
  ++home_->started;
  home_->slots.push_back({*made, false});
  return std::move(*made);
}

PoolCounts MemberPool::counts() const {
  std::lock_guard lock(home_->mutex);
  const size_t idle = home_->idleCount();
  return PoolCounts{idle, home_->slots.size() - idle, home_->started};
}

std::string noProcessMessage(std::string_view decoder, std::string_view why) {
  return "decoder '" + std::string(decoder) + "': decoder plugin process could not be started: " + std::string(why);
}

std::shared_ptr<DecoderPluginClient> DecoderPluginPool::Lease::shared() const {
  return std::static_pointer_cast<DecoderPluginClient>(lease_.member());
}

DecoderPluginPool::DecoderPluginPool(MemberPool members, std::string name, ipc::decoder::DecoderFacetInfo info)
    : members_(std::move(members)), name_(std::move(name)), info_(std::move(info)) {}

std::expected<std::shared_ptr<DecoderPluginPool>, std::string> DecoderPluginPool::start(const std::filesystem::path& executable,
                                                                                        PoolOptions options) {
  const size_t max_idle = options.max_idle;
  return create([executable, client = std::move(options.client)] { return DecoderPluginClient::start(executable, client); },
                max_idle);
}

std::expected<std::shared_ptr<DecoderPluginPool>, std::string> DecoderPluginPool::create(ClientFactory factory,
                                                                                         size_t max_idle) {
  auto members = MemberPool::create(
      [factory = std::move(factory)]() -> std::expected<MemberPool::Member, std::string> {
        auto client = factory();
        if (!client) return std::unexpected(std::move(client.error()));
        return MemberPool::Member(std::move(*client));
      },
      [](const MemberPool::Member& member) {
        return static_cast<const DecoderPluginClient*>(member.get())->closedReason().has_value();
      },
      max_idle);
  if (!members) return std::unexpected(std::move(members.error()));
  auto first = members->anyLive();
  if (!first) return std::unexpected(std::move(first.error()));
  const auto& client = *static_cast<const DecoderPluginClient*>(first->get());
  return std::shared_ptr<DecoderPluginPool>(new DecoderPluginPool(std::move(*members), client.name(), client.info()));
}

const ipc::decoder::DecoderInfo* DecoderPluginPool::decoder(std::string_view name) const {
  for (const ipc::decoder::DecoderInfo& d : info_.decoders) {
    if (d.name == name) return &d;
  }
  return nullptr;
}

std::expected<DecoderPluginPool::Lease, std::string> DecoderPluginPool::take() {
  auto lease = members_.take();
  if (!lease) return std::unexpected(std::move(lease.error()));
  return Lease(std::move(*lease));
}

std::expected<ipc::decoder::CheckAnswer, std::string> DecoderPluginPool::checkJob(const ipc::decoder::JobSetup& setup) {
  auto member = members_.anyLive();
  if (!member) return std::unexpected(noProcessMessage(setup.decoder, member.error()));
  return static_cast<DecoderPluginClient*>(member->get())->checkJob(setup);
}

}

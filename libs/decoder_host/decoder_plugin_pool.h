/*
 * SPDX-License-Identifier: MIT
 */

#pragma once


#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <string_view>

#include "decoder_plugin_client.h"
#include "ipc_decoder.h"

namespace bitoscope::decoder_host {

inline constexpr size_t kMaxIdleDecoderProcesses = 8;

struct PoolCounts {
  size_t idle = 0;
  size_t busy = 0;
  uint64_t started = 0;
  bool operator==(const PoolCounts&) const = default;
};

class MemberPool {
  struct Home;

 public:
  using Member = std::shared_ptr<void>;
  using Start = std::function<std::expected<Member, std::string>()>;
  using Ended = std::function<bool(const Member& member)>;

  class Lease {
   public:
    Lease() = default;
    ~Lease();
    Lease(Lease&& other) noexcept;
    Lease& operator=(Lease&& other) noexcept;
    Lease(const Lease&) = delete;
    Lease& operator=(const Lease&) = delete;

    const Member& member() const { return member_; }

   private:
    friend class MemberPool;
    Lease(std::shared_ptr<Home> home, Member member);
    void giveBack();

    std::shared_ptr<Home> home_;
    Member member_;
  };

  static std::expected<MemberPool, std::string> create(Start start, Ended ended,
                                                       size_t max_idle = kMaxIdleDecoderProcesses);
  ~MemberPool();
  MemberPool(MemberPool&&) noexcept;
  MemberPool& operator=(MemberPool&&) noexcept;

  std::expected<Lease, std::string> take();
  std::expected<Member, std::string> anyLive();
  PoolCounts counts() const;

 private:
  explicit MemberPool(std::shared_ptr<Home> home);
  void end();

  std::shared_ptr<Home> home_;
};

std::string noProcessMessage(std::string_view decoder, std::string_view why);

struct PoolOptions {
  ClientOptions client;
  size_t max_idle = kMaxIdleDecoderProcesses;
};

class DecoderPluginPool {
 public:
  using ClientFactory = std::function<std::expected<std::shared_ptr<DecoderPluginClient>, std::string>()>;

  class Lease {
   public:
    Lease() = default;
    explicit operator bool() const { return static_cast<bool>(lease_.member()); }
    DecoderPluginClient& client() const { return *shared(); }
    std::shared_ptr<DecoderPluginClient> shared() const;

   private:
    friend class DecoderPluginPool;
    explicit Lease(MemberPool::Lease lease) : lease_(std::move(lease)) {}
    MemberPool::Lease lease_;
  };

  static std::expected<std::shared_ptr<DecoderPluginPool>, std::string> start(const std::filesystem::path& executable,
                                                                              PoolOptions options = {});
  static std::expected<std::shared_ptr<DecoderPluginPool>, std::string> create(
      ClientFactory factory, size_t max_idle = kMaxIdleDecoderProcesses);

  DecoderPluginPool(const DecoderPluginPool&) = delete;
  DecoderPluginPool& operator=(const DecoderPluginPool&) = delete;

  const std::string& name() const { return name_; }
  const ipc::decoder::DecoderFacetInfo& info() const { return info_; }
  const ipc::decoder::DecoderInfo* decoder(std::string_view name) const;

  std::expected<Lease, std::string> take();
  std::expected<ipc::decoder::CheckAnswer, std::string> checkJob(const ipc::decoder::JobSetup& setup);
  PoolCounts counts() const { return members_.counts(); }

 private:
  DecoderPluginPool(MemberPool members, std::string name, ipc::decoder::DecoderFacetInfo info);

  MemberPool members_;
  std::string name_;
  ipc::decoder::DecoderFacetInfo info_;
};

}

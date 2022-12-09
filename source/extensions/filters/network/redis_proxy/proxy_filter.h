#pragma once

#include <cstdint>
#include <list>
#include <memory>
#include <string>

#include "envoy/config/filter/network/redis_proxy/v2/redis_proxy.pb.h"
#include "envoy/network/drain_decision.h"
#include "envoy/network/filter.h"
#include "envoy/stats/scope.h"
#include "envoy/upstream/cluster_manager.h"

#include "common/buffer/buffer_impl.h"

#include "extensions/filters/network/common/redis/codec.h"
#include "extensions/filters/network/redis_proxy/command_splitter.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace RedisProxy {

/**
 * All redis proxy stats. @see stats_macros.h
 */
// clang-format off
#define ALL_REDIS_PROXY_STATS(COUNTER, GAUGE)                                                      \
  COUNTER(downstream_cx_rx_bytes_total)                                                            \
  GAUGE  (downstream_cx_rx_bytes_buffered)                                                         \
  COUNTER(downstream_cx_tx_bytes_total)                                                            \
  GAUGE  (downstream_cx_tx_bytes_buffered)                                                         \
  COUNTER(downstream_cx_protocol_error)                                                            \
  COUNTER(downstream_cx_total)                                                                     \
  GAUGE  (downstream_cx_active)                                                                    \
  COUNTER(downstream_cx_drain_close)                                                               \
  COUNTER(downstream_rq_total)                                                                     \
  GAUGE  (downstream_rq_active)
// clang-format on

/**
 * Struct definition for all redis proxy stats. @see stats_macros.h
 */
struct ProxyStats {
  ALL_REDIS_PROXY_STATS(GENERATE_COUNTER_STRUCT, GENERATE_GAUGE_STRUCT)
};

/**
 * Configuration for the redis proxy filter.
 */
class ProxyFilterConfig {
public:
  ProxyFilterConfig(const envoy::config::filter::network::redis_proxy::v2::RedisProxy& config,
                    Stats::Scope& scope, const Network::DrainDecision& drain_decision,
                    Runtime::Loader& runtime);

  const Network::DrainDecision& drain_decision_;
  Runtime::Loader& runtime_;
  const std::string stat_prefix_;
  const std::string redis_drain_close_runtime_key_{"redis.drain_close_enabled"};
  ProxyStats stats_;

private:
  static ProxyStats generateStats(const std::string& prefix, Stats::Scope& scope);
};

typedef std::shared_ptr<ProxyFilterConfig> ProxyFilterConfigSharedPtr;

/**
 * A redis multiplexing proxy filter. This filter will take incoming redis pipelined commands, and
 * multiplex them onto a consistently hashed connection pool of backend servers.
 */
class ProxyFilter : public Network::ReadFilter,
                    public Common::Redis::DecoderCallbacks,
                    public Network::ConnectionCallbacks {
public:
  ProxyFilter(Common::Redis::DecoderFactory& factory, Common::Redis::EncoderPtr&& encoder,
              CommandSplitter::Instance& splitter, ProxyFilterConfigSharedPtr config);
  ~ProxyFilter();

  // Network::ReadFilter
  void initializeReadFilterCallbacks(Network::ReadFilterCallbacks& callbacks) override;
  Network::FilterStatus onData(Buffer::Instance& data, bool end_stream) override;
  Network::FilterStatus onNewConnection() override { return Network::FilterStatus::Continue; }

  // Network::ConnectionCallbacks
  void onEvent(Network::ConnectionEvent event) override;
  void onAboveWriteBufferHighWatermark() override {}
  void onBelowWriteBufferLowWatermark() override {}

  // Common::Redis::DecoderCallbacks
  void onRespValue(Common::Redis::RespValuePtr&& value) override;

private:
  struct PendingRequest : public CommandSplitter::SplitCallbacks {
    PendingRequest(ProxyFilter& parent);
    ~PendingRequest();

    // RedisProxy::CommandSplitter::SplitCallbacks
    void onResponse(Common::Redis::RespValuePtr&& value) override {
      parent_.onResponse(*this, std::move(value));
    }

    ProxyFilter& parent_;
    Common::Redis::RespValuePtr pending_response_;
    CommandSplitter::SplitRequestPtr request_handle_;
  };

  void onResponse(PendingRequest& request, Common::Redis::RespValuePtr&& value);

  Common::Redis::DecoderPtr decoder_;
  Common::Redis::EncoderPtr encoder_;
  CommandSplitter::Instance& splitter_;
  ProxyFilterConfigSharedPtr config_;
  Buffer::OwnedImpl encoder_buffer_;
  Network::ReadFilterCallbacks* callbacks_{};
  std::list<PendingRequest> pending_requests_;
};

} // namespace RedisProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
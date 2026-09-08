#pragma once

#include <cstdint>
#include <cstddef>
#include <array>
#include <algorithm>
#include <cstring>

namespace luv {

enum class DataCenterRegion : uint8_t {
    NY4 = 0, // Secaucus, New Jersey (US Equities & FX)
    LD4 = 1, // Slough, London (FX & European Equities)
    TY3 = 2, // Tokyo, Japan (Asian Derivatives & FX)
    FR2 = 3, // Frankfurt, Germany (Eurex & European Derivatives)
    SG1 = 4  // Singapore (SGX & Southeast Asian Markets)
};

enum class CrossDCSyncState : uint8_t {
    FullySynchronized = 0,
    DegradedLatency = 1,
    PartitionedSevered = 2
};

struct CrossDCLogEntry {
    uint64_t term{1};
    uint64_t log_index{0};
    DataCenterRegion origin_region{DataCenterRegion::NY4};
    uint64_t origin_timestamp_ns{0};
    uint64_t hybrid_logical_clock{0};
    uint64_t order_id{0};
    uint32_t event_type{0}; // 1 = New, 2 = Cancel, 3 = Fill
    uint64_t price{0};
    uint64_t quantity{0};
};

struct RegionNodeStatus {
    DataCenterRegion region{DataCenterRegion::NY4};
    uint64_t last_heartbeat_ns{0};
    uint64_t round_trip_latency_ns{0};
    int64_t clock_offset_ns{0};
    bool is_reachable{true};
    uint64_t acked_log_index{0};
};

struct ClusterQuorumResult {
    bool has_global_quorum{true};
    bool has_local_quorum{true};
    size_t active_node_count{0};
    uint64_t global_commit_index{0};
    DataCenterRegion current_leader_region{DataCenterRegion::NY4};
    CrossDCSyncState sync_state{CrossDCSyncState::FullySynchronized};
};

class MultiRegionRaftCluster {
public:
    static constexpr size_t kMaxRegions = 5;
    static constexpr size_t kLogCapacity = 512;
    static constexpr uint64_t kHeartbeatTimeoutNs = 200'000'000ULL; // 200ms WAN heartbeat timeout

    // Standard cross-DC one-way speed of light latencies
    static constexpr uint64_t kLatencyNY4_LD4_Ns = 35'000'000ULL;  // 35 ms
    static constexpr uint64_t kLatencyNY4_TY3_Ns = 65'000'000ULL;  // 65 ms
    static constexpr uint64_t kLatencyLD4_TY3_Ns = 105'000'000ULL; // 105 ms

    MultiRegionRaftCluster(DataCenterRegion local_region = DataCenterRegion::NY4) noexcept
        : local_region_(local_region), leader_region_(DataCenterRegion::NY4), current_term_(1),
          log_count_(0), local_commit_index_(0), global_commit_index_(0), hlc_counter_(0)
    {
        init_nodes();
    }

    DataCenterRegion local_region() const noexcept { return local_region_; }
    DataCenterRegion leader_region() const noexcept { return leader_region_; }
    uint64_t current_term() const noexcept { return current_term_; }
    uint64_t local_commit_index() const noexcept { return local_commit_index_; }
    uint64_t global_commit_index() const noexcept { return global_commit_index_; }

    void on_peer_heartbeat(DataCenterRegion from_region, uint64_t now_ns, uint64_t rtt_ns, 
                           int64_t clock_offset_ns, uint64_t peer_acked_index) noexcept 
    {
        size_t idx = static_cast<size_t>(from_region);
        if (idx >= kMaxRegions) return;

        nodes_[idx].last_heartbeat_ns = now_ns;
        nodes_[idx].round_trip_latency_ns = rtt_ns;
        nodes_[idx].clock_offset_ns = clock_offset_ns;
        nodes_[idx].is_reachable = true;
        nodes_[idx].acked_log_index = peer_acked_index;

        update_global_commit_index();
    }

    void on_peer_timeout(DataCenterRegion from_region) noexcept {
        size_t idx = static_cast<size_t>(from_region);
        if (idx < kMaxRegions) {
            nodes_[idx].is_reachable = false;
        }
    }

    bool append_order_event(uint64_t order_id, uint32_t event_type, uint64_t price, uint64_t qty, 
                            uint64_t now_ns, CrossDCLogEntry& out_entry) noexcept 
    {
        if (log_count_ >= kLogCapacity) return false;

        uint64_t next_idx = local_commit_index_ + 1;
        hlc_counter_ = std::max(hlc_counter_ + 1, now_ns);

        out_entry = CrossDCLogEntry{
            current_term_,
            next_idx,
            local_region_,
            now_ns,
            hlc_counter_,
            order_id,
            event_type,
            price,
            qty
        };

        log_[next_idx & (kLogCapacity - 1)] = out_entry;
        local_commit_index_ = next_idx;
        nodes_[static_cast<size_t>(local_region_)].acked_log_index = next_idx;
        ++log_count_;

        update_global_commit_index();
        return true;
    }

    bool replicate_from_remote(const CrossDCLogEntry& entry, uint64_t now_ns) noexcept {
        if (entry.term < current_term_) return false;
        if (entry.term > current_term_) {
            current_term_ = entry.term;
            leader_region_ = entry.origin_region;
        }

        // HLC causality update
        hlc_counter_ = std::max(std::max(hlc_counter_, now_ns), entry.hybrid_logical_clock) + 1;

        if (entry.log_index == local_commit_index_ + 1) {
            log_[entry.log_index & (kLogCapacity - 1)] = entry;
            local_commit_index_ = entry.log_index;
            nodes_[static_cast<size_t>(local_region_)].acked_log_index = entry.log_index;
            nodes_[static_cast<size_t>(entry.origin_region)].acked_log_index = entry.log_index;
            ++log_count_;
            update_global_commit_index();
            return true;
        }

        return false;
    }

    ClusterQuorumResult evaluate_cluster_health(uint64_t now_ns) const noexcept {
        ClusterQuorumResult res{};
        res.current_leader_region = leader_region_;
        res.has_local_quorum = true; // Local DC always has sub-quorum

        size_t reachable_count = 0;
        for (size_t i = 0; i < kMaxRegions; ++i) {
            if (nodes_[i].is_reachable && (now_ns >= nodes_[i].last_heartbeat_ns) && 
                (now_ns - nodes_[i].last_heartbeat_ns) <= kHeartbeatTimeoutNs) 
            {
                ++reachable_count;
            }
        }
        res.active_node_count = reachable_count;

        // Quorum of 5 regions requires at least 3 active nodes (NY4, LD4, TY3)
        res.has_global_quorum = (reachable_count >= 3);
        res.global_commit_index = global_commit_index_;

        if (reachable_count == kMaxRegions) {
            res.sync_state = CrossDCSyncState::FullySynchronized;
        } else if (reachable_count >= 3) {
            res.sync_state = CrossDCSyncState::DegradedLatency;
        } else {
            res.sync_state = CrossDCSyncState::PartitionedSevered;
        }

        return res;
    }

private:
    DataCenterRegion local_region_{DataCenterRegion::NY4};
    DataCenterRegion leader_region_{DataCenterRegion::NY4};
    uint64_t current_term_{1};

    std::array<RegionNodeStatus, kMaxRegions> nodes_{};
    std::array<CrossDCLogEntry, kLogCapacity> log_{};
    size_t log_count_{0};

    uint64_t local_commit_index_{0};
    uint64_t global_commit_index_{0};
    uint64_t hlc_counter_{0};

    void init_nodes() noexcept {
        for (size_t i = 0; i < kMaxRegions; ++i) {
            nodes_[i].region = static_cast<DataCenterRegion>(i);
            nodes_[i].is_reachable = true;
            nodes_[i].last_heartbeat_ns = 0;
            nodes_[i].acked_log_index = 0;
        }
    }

    void update_global_commit_index() noexcept {
        // Collect acked indices across all reachable regions
        std::array<uint64_t, kMaxRegions> acks{};
        for (size_t i = 0; i < kMaxRegions; ++i) {
            acks[i] = nodes_[i].acked_log_index;
        }
        // Median of 5 elements gives the 3-of-5 majority quorum commit index
        std::sort(acks.begin(), acks.end());
        global_commit_index_ = acks[2]; // Index 2 is the median in 5-element array
    }
};

} // namespace luv

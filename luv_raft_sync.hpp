#pragma once

#include <cstdint>
#include <array>
#include <atomic>
#include <cstring>
#include "luv_execution.hpp"

namespace luv {
namespace ha {

enum class NodeRole : uint8_t {
    kFollower = 0,
    kCandidate = 1,
    kLeader = 2
};

enum class SyncEventType : uint8_t {
    kHeartbeat = 0,
    kOrderInserted = 1,
    kOrderCanceled = 2,
    kTradeExecuted = 3
};

struct alignas(64) RaftLogEntry {
    uint64_t term = 1;
    uint64_t log_index = 0;
    SyncEventType event_type = SyncEventType::kHeartbeat;
    uint16_t symbol_idx = 0;
    uint8_t side = exec::kBuy;
    uint64_t order_id = 0;
    int64_t price = 0;
    int64_t qty = 0;
    uint64_t event_timestamp_ns = 0;
};

class HotStandbyReplica {
public:
    static constexpr size_t kMaxLogCapacity = 4096;
    static constexpr uint64_t kHeartbeatTimeoutNs = 20'000'000; // 20ms failover timeout

    explicit HotStandbyReplica(NodeRole initial_role = NodeRole::kFollower) noexcept
        : role_(initial_role) {}

    NodeRole role() const noexcept { return role_; }
    uint64_t current_term() const noexcept { return current_term_; }
    uint64_t commit_index() const noexcept { return commit_index_; }

    // Leader generates and records log entry
    bool leader_append(
        SyncEventType type,
        uint16_t symbol_idx,
        uint8_t side,
        uint64_t order_id,
        int64_t price,
        int64_t qty,
        uint64_t ts_ns,
        RaftLogEntry& out_entry) noexcept
    {
        if (role_ != NodeRole::kLeader) return false;

        const uint64_t next_idx = commit_index_ + 1;
        out_entry = RaftLogEntry{
            .term = current_term_,
            .log_index = next_idx,
            .event_type = type,
            .symbol_idx = symbol_idx,
            .side = side,
            .order_id = order_id,
            .price = price,
            .qty = qty,
            .event_timestamp_ns = ts_ns
        };

        log_[next_idx & (kMaxLogCapacity - 1)] = out_entry;
        commit_index_ = next_idx;
        last_heartbeat_ts_ns_ = ts_ns;
        return true;
    }

    // Follower ingests replicated log entry from Leader
    bool follower_apply(const RaftLogEntry& entry, uint64_t now_ns) noexcept
    {
        if (entry.term < current_term_) {
            return false; // Stale term from old leader
        }

        if (entry.term > current_term_) {
            current_term_ = entry.term;
            role_ = NodeRole::kFollower;
        }

        last_heartbeat_ts_ns_ = now_ns;

        if (entry.event_type == SyncEventType::kHeartbeat) {
            return true;
        }

        // Apply in sequence
        if (entry.log_index == commit_index_ + 1) {
            log_[entry.log_index & (kMaxLogCapacity - 1)] = entry;
            commit_index_ = entry.log_index;
            applied_count_++;
            return true;
        }

        return false; // Gap or duplicate
    }

    // Follower evaluates failover if Leader heartbeat is overdue
    bool check_failover(uint64_t now_ns) noexcept {
        if (role_ == NodeRole::kLeader) return false;

        if (now_ns > last_heartbeat_ts_ns_ + kHeartbeatTimeoutNs) {
            // Heartbeat expired -> promote to Leader
            role_ = NodeRole::kLeader;
            current_term_++;
            return true; // Failover triggered!
        }

        return false;
    }

    uint64_t applied_count() const noexcept { return applied_count_; }

private:
    NodeRole role_{NodeRole::kFollower};
    uint64_t current_term_{1};
    uint64_t commit_index_{0};
    uint64_t last_heartbeat_ts_ns_{0};
    uint64_t applied_count_{0};
    std::array<RaftLogEntry, kMaxLogCapacity> log_{};
};

} // namespace ha
} // namespace luv

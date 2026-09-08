#include "luv_raft_sync.hpp"
#include <cassert>
#include <cstdio>

void test_leader_append_and_follower_apply() {
    luv::ha::HotStandbyReplica leader(luv::ha::NodeRole::kLeader);
    luv::ha::HotStandbyReplica follower(luv::ha::NodeRole::kFollower);

    assert(leader.role() == luv::ha::NodeRole::kLeader);
    assert(follower.role() == luv::ha::NodeRole::kFollower);

    // Leader generates 3 log entries
    luv::ha::RaftLogEntry entry1, entry2, entry3;
    assert(leader.leader_append(luv::ha::SyncEventType::kOrderInserted, 1, luv::exec::kBuy, 1001, 15000, 100, 1000, entry1));
    assert(leader.leader_append(luv::ha::SyncEventType::kOrderInserted, 1, luv::exec::kSell, 1002, 15010, 200, 2000, entry2));
    assert(leader.leader_append(luv::ha::SyncEventType::kTradeExecuted, 1, luv::exec::kBuy, 1001, 15000, 100, 3000, entry3));

    assert(leader.commit_index() == 3);

    // Follower applies in order
    assert(follower.follower_apply(entry1, 1000));
    assert(follower.follower_apply(entry2, 2000));
    assert(follower.follower_apply(entry3, 3000));
    assert(follower.commit_index() == 3);
    assert(follower.applied_count() == 3);

    // Duplicate rejection
    assert(!follower.follower_apply(entry2, 3000));

    std::printf("[PASS] test_leader_append_and_follower_apply\n");
}

void test_follower_heartbeat_and_failover() {
    luv::ha::HotStandbyReplica follower(luv::ha::NodeRole::kFollower);
    uint64_t ts = 100'000'000;

    luv::ha::RaftLogEntry hb{
        .term = 1,
        .log_index = 0,
        .event_type = luv::ha::SyncEventType::kHeartbeat,
        .event_timestamp_ns = ts
    };
    assert(follower.follower_apply(hb, ts));

    // Time advances 10ms (< 20ms timeout) -> No failover
    assert(!follower.check_failover(ts + 10'000'000));
    assert(follower.role() == luv::ha::NodeRole::kFollower);

    // Time advances 25ms (> 20ms timeout) -> Triggers failover election!
    assert(follower.check_failover(ts + 25'000'000));
    assert(follower.role() == luv::ha::NodeRole::kLeader);
    assert(follower.current_term() == 2);

    std::printf("[PASS] test_follower_heartbeat_and_failover\n");
}

int main() {
    test_leader_append_and_follower_apply();
    test_follower_heartbeat_and_failover();
    std::printf("All Hot-Standby Raft synchronization tests passed successfully.\n");
    return 0;
}

#include "luv_multiregion_raft_cluster.hpp"
#include <cassert>
#include <iostream>

using namespace luv;

void test_cross_dc_quorum_and_replication() {
    MultiRegionRaftCluster ny4_node(DataCenterRegion::NY4);
    uint64_t base_time_ns = 1'000'000'000ULL;

    // Simulate heartbeats from LD4 and TY3 to establish 3-of-5 majority
    ny4_node.on_peer_heartbeat(DataCenterRegion::NY4, base_time_ns, 0, 0, 0);
    ny4_node.on_peer_heartbeat(DataCenterRegion::LD4, base_time_ns, MultiRegionRaftCluster::kLatencyNY4_LD4_Ns, 500, 0);
    ny4_node.on_peer_heartbeat(DataCenterRegion::TY3, base_time_ns, MultiRegionRaftCluster::kLatencyNY4_TY3_Ns, -1200, 0);

    auto health = ny4_node.evaluate_cluster_health(base_time_ns);
    assert(health.has_global_quorum == true);
    assert(health.active_node_count >= 3);

    // Append order in NY4
    CrossDCLogEntry entry{};
    assert(ny4_node.append_order_event(1001, 1, 150'00, 100, base_time_ns + 1000, entry));
    assert(ny4_node.local_commit_index() == 1);

    // Replicate ACK from LD4
    ny4_node.on_peer_heartbeat(DataCenterRegion::LD4, base_time_ns + 2000, 35'000'000, 500, 1);
    // Global commit is not yet 1 because TY3 hasn't ACKed (only 2 nodes at index 1: NY4 & LD4)
    assert(ny4_node.global_commit_index() == 0);

    // Replicate ACK from TY3
    ny4_node.on_peer_heartbeat(DataCenterRegion::TY3, base_time_ns + 3000, 65'000'000, -1200, 1);
    // Now 3 nodes (NY4, LD4, TY3) have index 1 -> Global Quorum achieved!
    assert(ny4_node.global_commit_index() == 1);
}

void test_partition_tolerance_and_island_mode() {
    MultiRegionRaftCluster ny4_node(DataCenterRegion::NY4);
    uint64_t base_time_ns = 2'000'000'000ULL;

    ny4_node.on_peer_heartbeat(DataCenterRegion::NY4, base_time_ns, 0, 0, 0);

    // Oceanic partition: only NY4 reachable, LD4 and TY3 time out
    uint64_t timeout_time_ns = base_time_ns + MultiRegionRaftCluster::kHeartbeatTimeoutNs + 10'000'000;
    auto health = ny4_node.evaluate_cluster_health(timeout_time_ns);

    assert(health.has_global_quorum == false);
    assert(health.sync_state == CrossDCSyncState::PartitionedSevered);
}

int main() {
    test_cross_dc_quorum_and_replication();
    test_partition_tolerance_and_island_mode();
    std::cout << "Multi-Region Cross-Data Center Raft Cluster tests passed.\n";
    return 0;
}

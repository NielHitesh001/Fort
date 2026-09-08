#pragma once

#include <cstdint>
#include <array>
#include <cstring>
#include <algorithm>

namespace luv {
namespace reg_sci {

enum class SiteStatus : uint8_t {
    kPrimaryActive = 0,
    kFailoverInProgress = 1,
    kSecondaryActive = 2,
    kEvacuationCompleted = 3,
    kSplitBrainDegraded = 4
};

struct SiteHealthMetrics {
    uint64_t last_heartbeat_ns = 0;
    uint64_t last_replicated_seq = 0;
    uint32_t active_connections = 0;
    bool is_reachable = true;
};

struct DisasterRecoveryReport {
    uint64_t drill_id = 0;
    uint64_t failover_duration_ns = 0;
    uint64_t sequence_gap_rpo = 0; // Sequence divergence between Primary and Secondary (RPO target: 0)
    bool rto_compliant = false;   // Must be within RTO budget (< 2 hours, simulated < 50ms in-memory)
    bool rpo_compliant = false;   // Must be 0 data loss
    SiteStatus final_status = SiteStatus::kSecondaryActive;
};

class DisasterRecoveryController {
public:
    static constexpr uint64_t kMaxRtoNs = 50'000'000; // 50ms simulated max failover time

    DisasterRecoveryController() noexcept : current_status_(SiteStatus::kPrimaryActive) {}

    void update_site_health(bool primary_reachable, uint64_t primary_seq,
                            bool secondary_reachable, uint64_t secondary_seq,
                            uint64_t timestamp_ns) noexcept {
        primary_metrics_.is_reachable = primary_reachable;
        primary_metrics_.last_replicated_seq = primary_seq;
        primary_metrics_.last_heartbeat_ns = timestamp_ns;

        secondary_metrics_.is_reachable = secondary_reachable;
        secondary_metrics_.last_replicated_seq = secondary_seq;
        secondary_metrics_.last_heartbeat_ns = timestamp_ns;
    }

    // Executes simulated or real Datacenter Evacuation / Failover Drill
    DisasterRecoveryReport execute_failover_drill(uint64_t drill_id, uint64_t start_time_ns, uint64_t complete_time_ns) noexcept {
        current_status_ = SiteStatus::kFailoverInProgress;

        uint64_t elapsed_ns = (complete_time_ns >= start_time_ns) ? (complete_time_ns - start_time_ns) : 0;
        
        uint64_t seq_gap = 0;
        if (primary_metrics_.last_replicated_seq > secondary_metrics_.last_replicated_seq) {
            seq_gap = primary_metrics_.last_replicated_seq - secondary_metrics_.last_replicated_seq;
        }

        bool rto_ok = (elapsed_ns <= kMaxRtoNs);
        bool rpo_ok = (seq_gap == 0);

        if (secondary_metrics_.is_reachable && rpo_ok) {
            current_status_ = SiteStatus::kSecondaryActive;
        } else if (!secondary_metrics_.is_reachable) {
            current_status_ = SiteStatus::kSplitBrainDegraded;
        } else {
            current_status_ = SiteStatus::kSecondaryActive;
        }

        return DisasterRecoveryReport{
            .drill_id = drill_id,
            .failover_duration_ns = elapsed_ns,
            .sequence_gap_rpo = seq_gap,
            .rto_compliant = rto_ok,
            .rpo_compliant = rpo_ok,
            .final_status = current_status_
        };
    }

    SiteStatus get_status() const noexcept { return current_status_; }

private:
    SiteStatus current_status_{SiteStatus::kPrimaryActive};
    SiteHealthMetrics primary_metrics_{};
    SiteHealthMetrics secondary_metrics_{};
};

} // namespace reg_sci
} // namespace luv

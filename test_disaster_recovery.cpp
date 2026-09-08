#include "luv_disaster_recovery.hpp"
#include <cassert>
#include <cstdio>

void test_reg_sci_disaster_recovery_failover() {
    luv::reg_sci::DisasterRecoveryController dr;

    // Healthy state: Primary at seq 50000, Secondary at seq 50000
    dr.update_site_health(true, 50000, true, 50000, 1'000'000);
    assert(dr.get_status() == luv::reg_sci::SiteStatus::kPrimaryActive);

    // Run failover drill: Primary evacuated, Secondary promoted within 5ms (5,000,000 ns)
    auto report = dr.execute_failover_drill(1001, 1'000'000, 6'000'000);

    assert(report.drill_id == 1001);
    assert(report.failover_duration_ns == 5'000'000);
    assert(report.sequence_gap_rpo == 0);
    assert(report.rto_compliant == true);
    assert(report.rpo_compliant == true);
    assert(report.final_status == luv::reg_sci::SiteStatus::kSecondaryActive);
    assert(dr.get_status() == luv::reg_sci::SiteStatus::kSecondaryActive);

    std::printf("[PASS] test_reg_sci_disaster_recovery_failover (Drill #%llu RTO: %lluns, RPO gap: %llu)\n",
        static_cast<unsigned long long>(report.drill_id),
        static_cast<unsigned long long>(report.failover_duration_ns),
        static_cast<unsigned long long>(report.sequence_gap_rpo));
}

int main() {
    test_reg_sci_disaster_recovery_failover();
    std::printf("All disaster recovery drill tests passed successfully.\n");
    return 0;
}

#include "luv_rule_606.hpp"
#include <cassert>
#include <cstdio>
#include <cmath>

void test_sec_rule_606_quarterly_reporting() {
    luv::compliance::Rule606Reporter reporter;

    // Venue 1 (Citadel Securities): 60 non-directed orders, received $120 PFOF (12000 cents)
    for (int i = 0; i < 60; ++i) {
        assert(reporter.record_venue_order(1, true, false, 200)); // 200 cents per order
    }

    // Venue 2 (Virtu Financial): 30 non-directed orders, received $45 PFOF (4500 cents)
    for (int i = 0; i < 30; ++i) {
        assert(reporter.record_venue_order(2, false, true, 150));
    }

    // Venue 3 (NASDAQ Direct): 10 non-directed orders, paid $20 routing fee (-2000 cents)
    for (int i = 0; i < 10; ++i) {
        assert(reporter.record_venue_order(3, false, false, -200));
    }

    auto summary = reporter.generate_summary();

    assert(summary.total_non_directed_orders == 100);
    assert(summary.top_venue_id == 1);
    assert(std::fabs(summary.top_venue_pct - 60.0) < 1e-4);
    // Total PFOF = 12000 + 4500 - 2000 = 14500 cents ($145.00)
    assert(summary.total_net_pfof_cents == 14500);

    std::printf("[PASS] test_sec_rule_606_quarterly_reporting (Total Orders: %lld, Top Venue: %u [%.1f%%], Net PFOF: $%.2f)\n",
        static_cast<long long>(summary.total_non_directed_orders),
        summary.top_venue_id,
        summary.top_venue_pct,
        static_cast<double>(summary.total_net_pfof_cents) / 100.0);
}

int main() {
    test_sec_rule_606_quarterly_reporting();
    std::printf("All SEC Rule 606 reporting tests passed successfully.\n");
    return 0;
}

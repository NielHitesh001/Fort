#include "luv_ats_n.hpp"
#include <cassert>
#include <cstdio>

void test_form_ats_n_transparency_and_tiering() {
    luv::ats_n::AtsTransparencyEngine ats;

    // Subscriber 1: Institutional Trader (opts out of retail interaction)
    assert(ats.register_subscriber(luv::ats_n::SubscriberProfile{
        .subscriber_id = 101,
        .tier = luv::ats_n::SubscriberTier::kInstitutional,
        .opt_out_internalization = false,
        .opt_out_retail_interaction = true
    }));

    // Subscriber 2: Retail Flow
    assert(ats.register_subscriber(luv::ats_n::SubscriberProfile{
        .subscriber_id = 102,
        .tier = luv::ats_n::SubscriberTier::kRetail,
        .opt_out_internalization = false,
        .opt_out_retail_interaction = false
    }));

    // Subscriber 3: Institutional Trader (willing to trade with anyone)
    assert(ats.register_subscriber(luv::ats_n::SubscriberProfile{
        .subscriber_id = 103,
        .tier = luv::ats_n::SubscriberTier::kInstitutional,
        .opt_out_internalization = false,
        .opt_out_retail_interaction = false
    }));

    // Test 1: Sub 101 (Institutional opt-out) vs Sub 102 (Retail) in Public Pool -> Forbidden
    assert(!ats.can_interact(101, 102, luv::ats_n::PoolSegmentation::kPublicAll));

    // Test 2: Sub 103 (Institutional) vs Sub 102 (Retail) in Public Pool -> Allowed
    assert(ats.can_interact(103, 102, luv::ats_n::PoolSegmentation::kPublicAll));

    // Test 3: Sub 101 vs Sub 103 in Institutional Only Pool -> Allowed
    assert(ats.can_interact(101, 103, luv::ats_n::PoolSegmentation::kInstitutionalOnly));

    // Test 4: Sub 102 (Retail) in Institutional Only Pool -> Forbidden
    assert(!ats.can_interact(102, 103, luv::ats_n::PoolSegmentation::kInstitutionalOnly));

    std::printf("[PASS] test_form_ats_n_transparency_and_tiering\n");
}

int main() {
    test_form_ats_n_transparency_and_tiering();
    std::printf("All SEC Form ATS-N transparency tests passed successfully.\n");
    return 0;
}

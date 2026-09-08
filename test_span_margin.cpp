#include "luv_span_margin.hpp"
#include <cassert>
#include <cstdio>

void test_cme_span_margin_risk_evaluation() {
    luv::risk::SpanMarginEngine engine;

    // Register S&P 500 E-mini futures contract (ID 1)
    // Scenario losses across 16 price x vol shock states
    luv::risk::SpanContractRiskArray es_futures{};
    es_futures.contract_id = 1;
    // Base losses: range from -$500 to +$3000 per contract
    es_futures.loss_matrix = {
        3000, 2500, 2000, 1500, 1000, 500, 0, -500,
        -1000, -1500, -2000, -2500, 3200, 2800, 1800, 500
    };
    assert(engine.register_contract_span_array(es_futures));

    // Position: Long 2 contracts
    assert(engine.set_position(1, 2));

    // Worst-case loss scenario across the 16 scenarios is scenario 12 (loss 3200 * 2 = 6400)
    int64_t scanning_req = engine.compute_span_scanning_requirement();
    assert(scanning_req == 6400);

    // Position flipped: Short 1 contract -> worst-case loss is scenario 11 (-2500 * -1 = +2500)
    assert(engine.set_position(1, -1));
    scanning_req = engine.compute_span_scanning_requirement();
    assert(scanning_req == 2500);

    std::printf("[PASS] test_cme_span_margin_risk_evaluation (Long 2 scanning req: $6400, Short 1: $2500)\n");
}

int main() {
    test_cme_span_margin_risk_evaluation();
    std::printf("All SPAN margin tests passed successfully.\n");
    return 0;
}

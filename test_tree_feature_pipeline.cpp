#include "luv_tree_feature_pipeline.hpp"
#include <cassert>
#include <iostream>

using namespace luv;

void test_tree_inference_bullish_order_flow() {
    TreeFeaturePipelineEngine engine;

    // Simulate heavy buying order book updates
    // Bid jumps up with large size, ask size is small
    engine.on_depth_update(100'00, 1000, 100'10, 100, 5000, 500);
    engine.on_depth_update(100'05, 1200, 100'10, 80, 6000, 400);

    // Large buyer-initiated trades
    engine.on_trade(true, 500);
    engine.on_trade(true, 700);

    auto pred = engine.predict();
    assert(pred.valid);
    assert(pred.directional_alpha > 0.0f); // Positive alpha predicted
    assert(pred.execution_urgency > 1.0f);
    assert(pred.adverse_selection_prob >= 0.0f && pred.adverse_selection_prob <= 1.0f);

    const auto& feats = engine.features();
    assert(feats.book_imbalance_l1 > 0.0f);
    assert(feats.trade_flow_imbalance > 0.0f);
    assert(feats.order_flow_imbalance > 0.0f);
}

void test_tree_inference_bearish_order_flow() {
    TreeFeaturePipelineEngine engine;

    // Simulate heavy selling pressure
    engine.on_depth_update(100'00, 100, 100'10, 1000, 500, 5000);
    engine.on_depth_update(99'95, 80, 100'05, 1200, 400, 6000);

    // Large seller-initiated trades
    engine.on_trade(false, 800);
    engine.on_trade(false, 600);

    auto pred = engine.predict();
    assert(pred.valid);
    assert(pred.directional_alpha < 0.0f); // Negative alpha predicted
    assert(pred.execution_urgency > 1.0f);
}

int main() {
    test_tree_inference_bullish_order_flow();
    test_tree_inference_bearish_order_flow();
    std::cout << "LOB Live Tree-Based ML Inference Pipeline tests passed.\n";
    return 0;
}

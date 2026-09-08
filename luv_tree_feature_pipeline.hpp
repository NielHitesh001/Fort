#pragma once

#include <cstdint>
#include <cstddef>
#include <array>
#include <cmath>
#include <algorithm>

namespace luv {

struct alignas(32) LOBTreeFeatures {
    float micro_price_delta{0.0f};      // Micro-price - Mid-price
    float order_flow_imbalance{0.0f};   // Multi-level OFI
    float spread_bps{0.0f};             // Top spread in basis points
    float book_imbalance_l1{0.0f};      // L1 (BidQty - AskQty) / Total
    float book_imbalance_l5{0.0f};      // L1-L5 aggregate book imbalance
    float trade_flow_imbalance{0.0f};   // Signed trade volume imbalance
    float rolling_micro_volatility{0.0f}; // Short-window price variance
    float depth_ratio_l1_to_total{0.0f}; // L1 liquidity concentration
};

struct alignas(16) TreeNode {
    uint16_t split_feature_idx{0}; // Index into LOBTreeFeatures array (0 to 7)
    bool is_leaf{false};
    float threshold{0.0f};
    uint16_t left_child_idx{0};
    uint16_t right_child_idx{0};
    float leaf_value{0.0f};
};

struct TreePredictionResult {
    float directional_alpha{0.0f};     // Predicted price drift (e.g. +1.0 = strong up, -1.0 = strong down)
    float adverse_selection_prob{0.0f}; // Probability of informed toxic flow [0, 1]
    float execution_urgency{0.0f};      // Urgency multiplier [0, 2.0]
    bool valid{false};
};

class TreeFeaturePipelineEngine {
public:
    static constexpr size_t kMaxNodesPerTree = 128;
    static constexpr size_t kMaxTrees = 16;
    static constexpr size_t kFeatureCount = 8;

    TreeFeaturePipelineEngine() noexcept : tree_count_(0) {
        init_default_tree_ensemble();
    }

    void on_depth_update(uint64_t bid_p1, uint64_t bid_q1, uint64_t ask_p1, uint64_t ask_q1,
                         uint64_t bid_depth_l5, uint64_t ask_depth_l5) noexcept 
    {
        if (bid_p1 == 0 || ask_p1 == 0 || (bid_q1 + ask_q1) == 0) return;

        float bp1 = static_cast<float>(bid_p1);
        float bq1 = static_cast<float>(bid_q1);
        float ap1 = static_cast<float>(ask_p1);
        float aq1 = static_cast<float>(ask_q1);
        float mid = 0.5f * (bp1 + ap1);

        // 1. Micro-price delta
        float micro = (bp1 * aq1 + ap1 * bq1) / (bq1 + aq1);
        features_.micro_price_delta = micro - mid;

        // 2. Spread in BPS
        if (mid > 0.0f) {
            features_.spread_bps = ((ap1 - bp1) / mid) * 10000.0f;
        }

        // 3. L1 Book Imbalance
        features_.book_imbalance_l1 = (bq1 - aq1) / (bq1 + aq1);

        // 4. L5 Book Imbalance
        float b_total = static_cast<float>(bid_depth_l5);
        float a_total = static_cast<float>(ask_depth_l5);
        if (b_total + a_total > 0.0f) {
            features_.book_imbalance_l5 = (b_total - a_total) / (b_total + a_total);
            features_.depth_ratio_l1_to_total = (bq1 + aq1) / (b_total + a_total);
        }

        // 5. OFI calculation
        if (prev_bid_p1_ > 0 && prev_ask_p1_ > 0) {
            float delta_b = (bid_p1 > prev_bid_p1_) ? bq1 : ((bid_p1 == prev_bid_p1_) ? (bq1 - static_cast<float>(prev_bid_q1_)) : -static_cast<float>(prev_bid_q1_));
            float delta_a = (ask_p1 < prev_ask_p1_) ? aq1 : ((ask_p1 == prev_ask_p1_) ? (aq1 - static_cast<float>(prev_ask_q1_)) : -static_cast<float>(prev_ask_q1_));
            features_.order_flow_imbalance = delta_b - delta_a;
        }

        prev_bid_p1_ = bid_p1;
        prev_bid_q1_ = bid_q1;
        prev_ask_p1_ = ask_p1;
        prev_ask_q1_ = ask_q1;
    }

    void on_trade(bool is_buy, uint64_t size) noexcept {
        float sz = static_cast<float>(size);
        if (is_buy) {
            cum_buy_vol_ += sz;
        } else {
            cum_sell_vol_ += sz;
        }

        float total = cum_buy_vol_ + cum_sell_vol_;
        if (total > 0.0f) {
            features_.trade_flow_imbalance = (cum_buy_vol_ - cum_sell_vol_) / total;
        }
    }

    TreePredictionResult predict() const noexcept {
        TreePredictionResult res{};
        if (tree_count_ == 0) return res;

        // Flatten features for vector tree indexing
        const float* raw_features = reinterpret_cast<const float*>(&features_);

        float ensemble_sum = 0.0f;
        for (size_t t = 0; t < tree_count_; ++t) {
            ensemble_sum += evaluate_single_tree(t, raw_features);
        }

        res.directional_alpha = std::clamp(ensemble_sum, -1.0f, 1.0f);
        
        // Adverse selection probability via sigmoid: 1 / (1 + exp(-4 * |alpha| - 2 * OFI_norm))
        float ofi_norm = std::clamp(std::abs(features_.order_flow_imbalance) / 1000.0f, 0.0f, 2.0f);
        float logit = 3.0f * std::abs(res.directional_alpha) + 1.5f * ofi_norm - 1.5f;
        res.adverse_selection_prob = 1.0f / (1.0f + std::exp(-logit));
        res.adverse_selection_prob = std::clamp(res.adverse_selection_prob, 0.0f, 1.0f);

        // Execution urgency multiplier: 1.0 + |alpha| + 0.5 * BookImbalance
        res.execution_urgency = std::clamp(1.0f + std::abs(res.directional_alpha) + 0.5f * std::abs(features_.book_imbalance_l1), 0.5f, 2.5f);
        res.valid = true;

        return res;
    }

    const LOBTreeFeatures& features() const noexcept { return features_; }

private:
    std::array<std::array<TreeNode, kMaxNodesPerTree>, kMaxTrees> trees_{};
    size_t tree_count_{0};

    LOBTreeFeatures features_{};
    uint64_t prev_bid_p1_{0};
    uint64_t prev_bid_q1_{0};
    uint64_t prev_ask_p1_{0};
    uint64_t prev_ask_q1_{0};
    float cum_buy_vol_{0.0f};
    float cum_sell_vol_{0.0f};

    float evaluate_single_tree(size_t tree_idx, const float* features) const noexcept {
        const auto& nodes = trees_[tree_idx];
        uint16_t curr = 0;

        // Traverse tree up to maximum depth without recursion or heap allocation
        for (size_t depth = 0; depth < 16; ++depth) {
            const auto& node = nodes[curr];
            if (node.is_leaf) {
                return node.leaf_value;
            }
            float val = features[node.split_feature_idx];
            if (val <= node.threshold) {
                curr = node.left_child_idx;
            } else {
                curr = node.right_child_idx;
            }
        }
        return 0.0f;
    }

    void init_default_tree_ensemble() noexcept {
        tree_count_ = 3;

        // Tree 0: Micro-price delta & L1 Imbalance
        trees_[0][0] = TreeNode{3, false, 0.0f, 1, 2, 0.0f};
        trees_[0][1] = TreeNode{0, false, 0.0f, 3, 4, 0.0f};
        trees_[0][2] = TreeNode{0, false, 0.0f, 5, 6, 0.0f};
        trees_[0][3] = TreeNode{0, true, 0.0f, 0, 0, -0.30f}; // Bearish
        trees_[0][4] = TreeNode{0, true, 0.0f, 0, 0, -0.10f};
        trees_[0][5] = TreeNode{0, true, 0.0f, 0, 0, +0.10f};
        trees_[0][6] = TreeNode{0, true, 0.0f, 0, 0, +0.30f}; // Bullish

        // Tree 1: OFI & Trade Flow Imbalance
        trees_[1][0] = TreeNode{1, false, 0.0f, 1, 2, 0.0f};
        trees_[1][1] = TreeNode{5, false, 0.0f, 3, 4, 0.0f};
        trees_[1][2] = TreeNode{5, false, 0.0f, 5, 6, 0.0f};
        trees_[1][3] = TreeNode{0, true, 0.0f, 0, 0, -0.35f};
        trees_[1][4] = TreeNode{0, true, 0.0f, 0, 0, -0.15f};
        trees_[1][5] = TreeNode{0, true, 0.0f, 0, 0, +0.15f};
        trees_[1][6] = TreeNode{0, true, 0.0f, 0, 0, +0.35f};

        // Tree 2: L5 Imbalance & Spread BPS
        trees_[2][0] = TreeNode{4, false, 0.0f, 1, 2, 0.0f};
        trees_[2][1] = TreeNode{2, false, 5.0f, 3, 4, 0.0f};
        trees_[2][2] = TreeNode{2, false, 5.0f, 5, 6, 0.0f};
        trees_[2][3] = TreeNode{0, true, 0.0f, 0, 0, -0.25f};
        trees_[2][4] = TreeNode{0, true, 0.0f, 0, 0, -0.10f};
        trees_[2][5] = TreeNode{0, true, 0.0f, 0, 0, +0.10f};
        trees_[2][6] = TreeNode{0, true, 0.0f, 0, 0, +0.25f};
    }
};

} // namespace luv
